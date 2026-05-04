#include "runtime_internal.h"
#include "export.h"
#include "stack_plan.h"

#include <string.h>

static GHashTable *clone_variant_hash(GHashTable *src)
{
	GHashTable *dst;
	GHashTableIter iter;
	gpointer key, value;

	dst = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
				     (GDestroyNotify)g_variant_unref);
	if (!src)
		return dst;

	g_hash_table_iter_init(&iter, src);
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		g_hash_table_insert(dst, g_strdup((const char *)key),
				    g_variant_ref((GVariant *)value));
	}

	return dst;
}

static int apply_root_channel_indices(struct decode_stack_runtime *stack,
				      struct srd_decoder_inst *di,
				      GHashTable *channel_indices)
{
	GHashTable *bound_indices;
	int rc;

	bound_indices = clone_variant_hash(channel_indices);
	rc = srd_inst_channel_set_all(di, bound_indices);
	g_hash_table_destroy(bound_indices);

	if (rc != SRD_OK) {
		cli_decode_stack_runtime_set_error(stack, "failed to bind decoder channels");
		return -1;
	}

	return 0;
}

static int instantiate_decode_steps(struct decode_stack_runtime *stack,
				    const struct decode_stack_plan *plan)
{
	struct srd_decoder_inst *prev_di = NULL;

	for (guint i = 0; i < plan->decoder_steps->len; i++) {
		struct decode_decoder_step_plan *step =
		    (struct decode_decoder_step_plan *)g_ptr_array_index(plan->decoder_steps, i);
		GHashTable *options;
		struct srd_decoder_inst *di;

		options = clone_variant_hash(step->options);
		di = srd_inst_new(stack->session, step->decoder_id, options);
		g_hash_table_destroy(options);
		if (!di) {
			cli_decode_stack_runtime_set_error(stack, "failed to instantiate decoder: %s",
					     step->decoder_id);
			return -1;
		}

		if (i == 0) {
			if (apply_root_channel_indices(stack, di,
						       step->channel_indices) != 0)
				return -1;
			stack->logic_di = di;
		}

		if (prev_di && srd_inst_stack(stack->session, prev_di, di) != SRD_OK) {
			cli_decode_stack_runtime_set_error(stack, "failed to stack decoder '%s'",
					     step->decoder_id);
			return -1;
		}

		prev_di = di;
	}

	return stack->logic_di ? 0 : -1;
}

static int start_decode_stack_runtime(struct decode_runtime *runtime,
				      struct decode_stack_runtime *stack,
				      const struct decode_stack_plan *plan)
{
	char *error = NULL;

	if (srd_session_new(&stack->session) != SRD_OK || !stack->session) {
		cli_decode_stack_runtime_set_error(stack,
				     "failed to create decode session for stack #%u",
				     stack->index);
		goto fail;
	}
	if (instantiate_decode_steps(stack, plan) != 0)
		goto fail;
	if (srd_session_metadata_set(stack->session, SRD_CONF_SAMPLERATE,
				     g_variant_new_uint64(runtime->samplerate)) != SRD_OK) {
		cli_decode_stack_runtime_set_error(stack,
				     "failed to set decode samplerate for stack #%u",
				     stack->index);
		goto fail;
	}
	if (srd_pd_output_callback_add(stack->session, SRD_OUTPUT_ANN,
				       cli_decode_runtime_annotation_callback, stack) != SRD_OK) {
		cli_decode_stack_runtime_set_error(stack,
				     "failed to register decode callback for stack #%u",
				     stack->index);
		goto fail;
	}
	if (srd_session_start(stack->session, &error) != SRD_OK) {
		cli_decode_stack_runtime_set_error(stack, "%s",
				     error ? error : "failed to start decode session");
		g_free(error);
		goto fail;
	}

	return 0;

fail:
	cli_decode_runtime_set_error(runtime, "decode stack #%u (%s): %s",
			      stack->index, stack->stack_spec,
			      stack->error_text ? stack->error_text :
			      "failed to start decode stack");
	return -1;
}

static struct decode_stack_runtime *create_decode_stack_runtime(
	struct decode_runtime *runtime,
	const struct decode_stack_plan *plan)
{
	struct decode_stack_runtime *stack;

	stack = g_new0(struct decode_stack_runtime, 1);
	stack->runtime = runtime;
	stack->index = plan->index;
	stack->stack_spec = g_strdup(plan->stack_spec);
	stack->output_path = g_strdup(plan->output_path);
	stack->output_format_name = g_strdup(plan->output_format_name);
	stack->root_dec = plan->root_decoder;
	stack->export_row = plan->export_row;
	stack->row_title = g_strdup(plan->row_title);
	stack->records = g_ptr_array_new_with_free_func(cli_decode_export_free_record);
	stack->rows_written = 0;
	stack->annotations_emitted = 0;
	stack->next_seq = 0;
	stack->success = FALSE;
	return stack;
}

int cli_decode_runtime_prepare_stacks(struct decode_runtime *runtime)
{
	if (!runtime || !runtime->stack_plans || runtime->stack_plans->len == 0) {
		cli_decode_runtime_set_error(runtime,
				      "no protocol decoder stack plans were prepared");
		return -1;
	}

	runtime->stacks =
	    g_ptr_array_new_with_free_func(cli_decode_stack_runtime_free);

	for (guint i = 0; i < runtime->stack_plans->len; i++) {
		struct decode_stack_plan *plan =
		    (struct decode_stack_plan *)g_ptr_array_index(runtime->stack_plans, i);
		struct decode_stack_runtime *stack =
		    create_decode_stack_runtime(runtime, plan);

		if (start_decode_stack_runtime(runtime, stack, plan) != 0) {
			cli_decode_stack_runtime_free(stack);
			return -1;
		}

		g_ptr_array_add(runtime->stacks, stack);
	}

	return 0;
}
