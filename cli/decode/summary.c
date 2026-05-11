#include "json.h"
#include "stack_runtime.h"
#include "summary.h"

static struct cli_support_json_value *build_stack_summary_value(
	const struct decode_stack_runtime *stack)
{
	struct cli_support_json_value *json = cli_support_json_new_object();

	if (!json)
		return NULL;

	cli_support_json_object_set_uint64(json, "index",
					   stack ? stack->index : 0U);
	cli_support_json_object_set_string(json, "stack_spec",
					   stack ? stack->stack_spec : "");
	cli_support_json_object_set_string(
		json, "root_decoder",
		(stack && stack->root_dec && stack->root_dec->id) ?
			stack->root_dec->id : "");
	cli_support_json_object_set_string(json, "output_file",
					   stack ? stack->output_path : "");
	cli_support_json_object_set_string(
		json, "output_format",
		stack ? stack->output_format_name : "");
	cli_support_json_object_set_uint64(json, "rows_written",
					   stack ? stack->rows_written : 0U);
	cli_support_json_object_set_uint64(
		json, "annotations_emitted",
		stack ? stack->annotations_emitted : 0U);
	cli_support_json_object_set_bool(json, "success",
					 stack ? stack->success : FALSE);
	if (stack && stack->row_title)
		cli_support_json_object_set_string(json, "row_title",
							   stack->row_title);
	if (stack && stack->error_text)
		cli_support_json_object_set_string(json, "error",
							   stack->error_text);
	return json;
}

static struct cli_support_json_value *build_decode_summary_result_value(
	gboolean source_is_live, const char *source_label, GPtrArray *stacks,
	guint64 total_rows, guint64 total_annotations,
	gboolean has_decode_window,
	uint64_t decode_start_sample, uint64_t decode_end_sample,
	uint64_t samplerate)
{
	struct cli_support_json_value *result = cli_support_json_new_object();
	struct cli_support_json_value *stack_array;

	if (!result)
		return NULL;

	if (source_is_live) {
		cli_support_json_object_set_string(result, "input_source",
							   "live");
		cli_support_json_object_set_string(result, "input_label",
							   source_label);
	} else {
		cli_support_json_object_set_string(result, "input_file",
							   source_label);
	}
	cli_support_json_object_set_uint64(result, "stack_count",
					   stacks ? stacks->len : 0U);
	cli_support_json_object_set_uint64(result, "rows_written", total_rows);
	cli_support_json_object_set_uint64(result, "annotations_emitted",
					   total_annotations);

	stack_array = cli_support_json_new_array();
	if (!stack_array) {
		cli_support_json_value_free(result);
		return NULL;
	}

	for (guint i = 0; stacks && i < stacks->len; i++) {
		struct decode_stack_runtime *stack =
		    (struct decode_stack_runtime *)g_ptr_array_index(stacks, i);
		struct cli_support_json_value *stack_json =
		    build_stack_summary_value(stack);

		if (!stack_json) {
			cli_support_json_value_free(stack_array);
			cli_support_json_value_free(result);
			return NULL;
		}
		cli_support_json_array_append(stack_array, stack_json);
	}

	cli_support_json_object_set(result, "stacks", stack_array);

	if (has_decode_window && samplerate > 0) {
		cli_support_json_object_set_uint64(result,
			"decode_window_start_sample", decode_start_sample);
		cli_support_json_object_set_uint64(result,
			"decode_window_end_sample", decode_end_sample);
		cli_support_json_object_set_uint64(result,
			"decode_window_start_time_ns",
			decode_start_sample * 1000000000ULL / samplerate);
		cli_support_json_object_set_uint64(result,
			"decode_window_end_time_ns",
			decode_end_sample * 1000000000ULL / samplerate);
	}

	return result;
}

int cli_decode_summary_write_json(const char *json_path,
			      gboolean source_is_live,
			      const char *source_label,
			      GPtrArray *stacks,
			      guint64 total_rows,
			      guint64 total_annotations,
			      gboolean has_decode_window,
			      uint64_t decode_start_sample,
			      uint64_t decode_end_sample,
			      uint64_t samplerate)
{
	struct cli_support_json_value *result;
	int rc;

	result = build_decode_summary_result_value(source_is_live, source_label,
						      stacks, total_rows,
						      total_annotations,
						      has_decode_window,
						      decode_start_sample,
						      decode_end_sample,
						      samplerate);
	if (!result)
		return -1;

	rc = cli_support_json_write_envelope(json_path, "decode", TRUE, result,
						 NULL);
	cli_support_json_value_free(result);
	return rc;
}
