#include "runtime.h"
#include "runtime_internal.h"
#include "export.h"
#include "stack_plan.h"
#include "summary.h"
#include "logic_source.h"
#include "command_result.h"
#include "shape.h"
#include "runtime_layout.h"
#include "waveform_session.h"

#include <errno.h>
#include <string.h>
#include <stdarg.h>

struct decode_runtime *cli_decode_runtime_create(void)
{
	struct decode_runtime *runtime;

	runtime = g_new0(struct decode_runtime, 1);
	if (!runtime)
		return NULL;

	cli_decode_runtime_init(runtime);
	return runtime;
}

static void reset_runtime_for_decode(struct decode_runtime *runtime)
{
	cli_decode_runtime_reset_objects(runtime);
	cli_decode_runtime_reset_state_data(runtime);
}

int cli_decode_runtime_run_offline_command(const struct cli_command_shape *shape)
{
	struct cli_logic_source logic_source;
	struct decode_runtime *runtime = NULL;
	const char *source_error = NULL;
	int rc = 1;

	if (!shape) {
		cli_support_command_result_write_error_json(
			NULL, "decode", "missing command shape");
		return 1;
	}

	runtime = cli_decode_runtime_create();
	if (!runtime) {
		cli_support_command_result_write_error_json(
			shape->json_file, "decode",
			"failed to allocate decode runtime");
		return 1;
	}

	cli_source_logic_init(&logic_source);
	if (cli_source_logic_open_input(&logic_source, shape, NULL, TRUE,
					&source_error) != 0) {
		cli_support_command_result_write_error_json(
			shape->json_file, "decode",
			source_error ? source_error :
			"failed to prepare logic source");
		goto done;
	}
	if (cli_decode_runtime_run_offline(runtime, shape, logic_source.channels,
					   logic_source.samplerate,
					   logic_source.source_label) != 0) {
		cli_support_command_result_write_error_json(
			shape->json_file, "decode",
			cli_decode_runtime_error_text(runtime));
		goto done;
	}

	printf("Decoded %u stacks to %u files.\n",
	       shape->pd_stack_count, shape->decode_output_count);
	rc = 0;

done:
	cli_source_logic_close(&logic_source);
	cli_decode_runtime_destroy(runtime);
	return rc;
}

int cli_decode_runtime_prepare_live(struct decode_runtime *runtime,
				    const struct cli_command_shape *shape,
				    GSList *channels, uint64_t samplerate,
				    const char *source_label)
{
	reset_runtime_for_decode(runtime);
	runtime->shape = shape;

	return cli_decode_runtime_begin_session(runtime, channels, samplerate, TRUE,
				    source_label);
}

static int wait_for_offline_decode(struct decode_runtime *runtime)
{
	struct timespec deadline;

	pthread_mutex_lock(&runtime->mutex);
	clock_gettime(CLOCK_REALTIME, &deadline);
	deadline.tv_sec += 120;
	while (!runtime->done) {
		if (pthread_cond_timedwait(&runtime->cond, &runtime->mutex,
					   &deadline) == ETIMEDOUT) {
			runtime->error = 1;
			cli_decode_runtime_set_error(runtime, "decode timed out");
			runtime->done = 1;
			break;
		}
	}
	pthread_mutex_unlock(&runtime->mutex);
	return runtime->error ? -1 : 0;
}

static int start_offline_decode_stream(struct decode_runtime *runtime)
{
	cli_waveform_session_bind_decode_runtime(runtime);
	cli_waveform_session_set_stream_mode(CLI_STREAM_DECODE);
	if (ds_start_collect() != SR_OK) {
		cli_waveform_session_unbind_decode_runtime();
		return -1;
	}
	return 0;
}

int cli_decode_runtime_run_offline(struct decode_runtime *runtime,
				   const struct cli_command_shape *shape,
				   GSList *channels, uint64_t samplerate,
				   const char *source_label)
{
	if (!runtime)
		return -1;

	reset_runtime_for_decode(runtime);
	runtime->shape = shape;

	if (cli_decode_runtime_begin_session(runtime, channels, samplerate, FALSE,
					     source_label) != 0)
		return -1;

	if (start_offline_decode_stream(runtime) != 0) {
		cli_decode_runtime_set_error(runtime,
				     "failed to start offline session playback");
		return -1;
	}

	wait_for_offline_decode(runtime);
	ds_stop_collect();
	cli_waveform_session_unbind_decode_runtime();

	if (runtime->error) {
		if (!runtime->error_text)
			cli_decode_runtime_set_error(runtime, "decode failed");
		return -1;
	}

	return cli_decode_runtime_finish_session(runtime);
}

int cli_decode_runtime_finalize_live(struct decode_runtime *runtime)
{
	return cli_decode_runtime_finish_session(runtime);
}

static void cleanup_decode_runtime_state(struct decode_runtime *runtime)
{
	cli_waveform_session_unbind_decode_runtime();

	cli_decode_runtime_reset_objects(runtime);
	cli_decode_runtime_reset_state_data(runtime);
}

void cli_decode_runtime_destroy(struct decode_runtime *runtime)
{
	if (!runtime)
		return;

	cleanup_decode_runtime_state(runtime);
	g_free(runtime);
}

const char *cli_decode_runtime_error_text(const struct decode_runtime *runtime)
{
	return runtime && runtime->error_text ? runtime->error_text : "decode failed";
}

void cli_decode_runtime_init(struct decode_runtime *runtime)
{
	if (!runtime)
		return;

	memset(runtime, 0, sizeof(*runtime));
	pthread_mutex_init(&runtime->mutex, NULL);
	pthread_cond_init(&runtime->cond, NULL);
	cli_decode_runtime_reset_state_data(runtime);
}

void cli_decode_stack_runtime_free(gpointer data)
{
	struct decode_stack_runtime *stack =
	    (struct decode_stack_runtime *)data;

	if (!stack)
		return;
	if (stack->session)
		srd_session_destroy(stack->session);
	if (stack->records)
		g_ptr_array_free(stack->records, TRUE);
	g_free(stack->row_title);
	g_free(stack->error_text);
	g_free(stack->stack_spec);
	g_free(stack->output_path);
	g_free(stack->output_format_name);
	g_free(stack);
}

void cli_decode_runtime_clear_error(struct decode_runtime *runtime)
{
	if (!runtime)
		return;
	g_free(runtime->error_text);
	runtime->error_text = NULL;
}

void cli_decode_runtime_set_error(struct decode_runtime *runtime, const char *fmt, ...)
{
	va_list ap;

	if (!runtime)
		return;
	cli_decode_runtime_clear_error(runtime);
	va_start(ap, fmt);
	runtime->error_text = g_strdup_vprintf(fmt, ap);
	va_end(ap);
}

void cli_decode_stack_runtime_set_error(struct decode_stack_runtime *stack,
			  const char *fmt, ...)
{
	va_list ap;

	if (!stack)
		return;
	g_free(stack->error_text);
	stack->error_text = NULL;
	va_start(ap, fmt);
	stack->error_text = g_strdup_vprintf(fmt, ap);
	va_end(ap);
}

void cli_decode_runtime_mark_done(struct decode_runtime *runtime, gboolean failed,
		      const char *fmt, ...)
{
	va_list ap;

	if (!runtime)
		return;

	pthread_mutex_lock(&runtime->mutex);
	runtime->done = 1;
	if (failed) {
		runtime->error = 1;
		cli_decode_runtime_clear_error(runtime);
		if (fmt) {
			va_start(ap, fmt);
			runtime->error_text = g_strdup_vprintf(fmt, ap);
			va_end(ap);
		}
	}
	pthread_cond_signal(&runtime->cond);
	pthread_mutex_unlock(&runtime->mutex);

	cli_waveform_session_notify_decode_done(failed);
}

void cli_decode_runtime_reset_state_data(struct decode_runtime *runtime)
{
	if (!runtime)
		return;

	runtime->done = 0;
	runtime->error = 0;
	runtime->shape = NULL;
	cli_decode_runtime_clear_error(runtime);
	runtime->rows_written = 0;
	runtime->annotations_emitted = 0;
	runtime->samplerate = 0;
	runtime->samples_sent = 0;
	if (runtime->stack_plans) {
		g_ptr_array_free(runtime->stack_plans, TRUE);
		runtime->stack_plans = NULL;
	}
	if (runtime->stacks) {
		g_ptr_array_free(runtime->stacks, TRUE);
		runtime->stacks = NULL;
	}

	g_free(runtime->channel_order_by_index);
	runtime->channel_order_by_index = NULL;
	runtime->channel_order_len = 0;
	runtime->signal_count = 0;
	runtime->cross_group_bytes = 0;

	g_free(runtime->cross_leftover);
	runtime->cross_leftover = NULL;
	runtime->cross_leftover_len = 0;

	g_free(runtime->source_label);
	runtime->source_label = NULL;
	runtime->source_is_live = FALSE;
}

void cli_decode_runtime_reset_objects(struct decode_runtime *runtime)
{
	if (!runtime)
		return;

	if (runtime->stacks) {
		g_ptr_array_free(runtime->stacks, TRUE);
		runtime->stacks = NULL;
	}
	if (runtime->stack_plans) {
		g_ptr_array_free(runtime->stack_plans, TRUE);
		runtime->stack_plans = NULL;
	}
	if (runtime->runtime_ready) {
		srd_exit();
		runtime->runtime_ready = FALSE;
	}
}

static int init_decode_runtime(struct decode_runtime *runtime)
{
	struct cli_runtime_layout layout;

	cli_runtime_layout_resolve(&layout);

#ifdef _WIN32
	if (layout.python_home_dir[0]) {
		wchar_t *pyhome = g_utf8_to_utf16(layout.python_home_dir, -1,
						 NULL, NULL, NULL);
		if (pyhome) {
			srd_set_python_home(pyhome);
			g_free(pyhome);
		}
	}
#endif

	if (!layout.decoder_script_dir[0]) {
		cli_decode_runtime_set_error(runtime,
				     "decoder script directory not found");
		return -1;
	}

	if (srd_init(layout.decoder_script_dir) != SRD_OK) {
		cli_decode_runtime_set_error(runtime, "libsigrokdecode init failed");
		return -1;
	}
	if (srd_decoder_load_all() != SRD_OK) {
		srd_exit();
		cli_decode_runtime_set_error(runtime,
				     "loading protocol decoders failed");
		return -1;
	}

	runtime->runtime_ready = TRUE;
	return 0;
}

static int finalize_decode_output(struct decode_runtime *runtime)
{
	return cli_decode_summary_write_json(runtime->shape ?
					 runtime->shape->json_file : NULL,
					 runtime->source_is_live,
					 runtime->source_label,
					 runtime->stacks,
					 runtime->rows_written,
					 runtime->annotations_emitted,
					 runtime->has_decode_window,
					 runtime->decode_start_sample,
					 runtime->decode_end_sample,
					 runtime->samplerate);
}

static int build_decode_stack_plans(struct decode_runtime *runtime,
				    GSList *channels)
{
	GPtrArray *plans = NULL;
	char *error_text = NULL;

	if (cli_decode_stack_plan_build(runtime ? runtime->shape : NULL,
					channels, &plans, &error_text) != 0) {
		cli_decode_runtime_set_error(runtime, "%s",
				      error_text ? error_text :
				      "failed to build decode stack plans");
		g_free(error_text);
		return -1;
	}

	runtime->stack_plans = plans;
	return 0;
}

static int normalize_decode_window(struct decode_runtime *runtime)
{
	const struct cli_command_shape *shape = runtime->shape;
	const char *decode_start;
	const char *decode_end;
	uint64_t start_ms = 0;
	uint64_t end_ms = 0;
	gboolean has_start = FALSE;
	gboolean has_end = FALSE;

	if (!shape)
		return 0;

	decode_start = shape->decode_start;
	decode_end = shape->decode_end;

	if (decode_start && decode_start[0]) {
		start_ms = sr_parse_timestring(decode_start);
		if (!start_ms && g_ascii_strcasecmp(decode_start, "0") != 0 &&
		    decode_start[0] != '0') {
			cli_decode_runtime_set_error(runtime,
				"invalid --decode-start value \"%s\"", decode_start);
			return -1;
		}
		has_start = TRUE;
	}

	if (decode_end && decode_end[0]) {
		end_ms = sr_parse_timestring(decode_end);
		if (!end_ms && g_ascii_strcasecmp(decode_end, "0") != 0 &&
		    decode_end[0] != '0') {
			cli_decode_runtime_set_error(runtime,
				"invalid --decode-end value \"%s\"", decode_end);
			return -1;
		}
		has_end = TRUE;
	}

	if (!has_start && !has_end)
		return 0;

	if (has_start)
		runtime->decode_start_sample =
			(start_ms * runtime->samplerate) / 1000ULL;
	if (has_end)
		runtime->decode_end_sample =
			(end_ms * runtime->samplerate) / 1000ULL;

	if (has_start && has_end &&
	    runtime->decode_start_sample >= runtime->decode_end_sample) {
		cli_decode_runtime_set_error(runtime,
			"--decode-start must be before --decode-end");
		return -1;
	}

	runtime->decode_start_sample &= ~63ULL;
	if (runtime->decode_end_sample & 63ULL) {
		runtime->decode_end_sample =
			(runtime->decode_end_sample + 64ULL) & ~63ULL;
	}

	runtime->has_decode_window = TRUE;
	return 0;
}

int cli_decode_runtime_begin_session(struct decode_runtime *runtime, GSList *channels,
			 uint64_t samplerate, gboolean source_is_live,
			 const char *source_label)
{
	if (!runtime)
		return -1;

	runtime->source_is_live = source_is_live;
	runtime->source_label = g_strdup(source_label ? source_label : "");
	runtime->samplerate = samplerate;

	if (!runtime->samplerate) {
		cli_decode_runtime_set_error(runtime, "input stream has invalid samplerate");
		return -1;
	}

	if (normalize_decode_window(runtime) != 0)
		return -1;

	if (init_decode_runtime(runtime) != 0) {
		if (!runtime->error_text)
			cli_decode_runtime_set_error(runtime,
					     "failed to initialize protocol decoders");
		return -1;
	}

	if (cli_decode_runtime_build_channel_order_map(runtime, channels) != 0) {
		cli_decode_runtime_set_error(runtime, "failed to build logic channel map");
		return -1;
	}

	if (build_decode_stack_plans(runtime, channels) != 0)
		return -1;

	if (cli_decode_runtime_prepare_stacks(runtime) != 0)
		return -1;

	return 0;
}

int cli_decode_runtime_finish_session(struct decode_runtime *runtime)
{
	char *error = NULL;
	guint64 total_rows = 0;
	guint64 total_annotations = 0;

	if (!runtime)
		return -1;

	if (runtime->error) {
		if (!runtime->error_text)
			cli_decode_runtime_set_error(runtime, "decode failed");
		return -1;
	}

	for (guint i = 0; runtime->stacks && i < runtime->stacks->len; i++) {
		struct decode_stack_runtime *stack =
		    (struct decode_stack_runtime *)g_ptr_array_index(runtime->stacks, i);

		if (srd_session_end(stack->session, &error) != SRD_OK) {
			cli_decode_stack_runtime_set_error(stack, "%s",
					     error ? error :
					     "failed to finalize decode session");
			cli_decode_runtime_set_error(runtime, "decode stack #%u (%s): %s",
					      stack->index, stack->stack_spec,
					      stack->error_text);
			g_free(error);
			return -1;
		}
		if (cli_decode_export_write_table_for_stack(stack, runtime->samplerate) != 0) {
			cli_decode_stack_runtime_set_error(stack, "failed to write decode output");
			cli_decode_runtime_set_error(runtime, "decode stack #%u (%s): %s",
					      stack->index, stack->stack_spec,
					      stack->error_text);
			return -1;
		}
		stack->success = TRUE;
		total_rows += stack->rows_written;
		total_annotations += stack->annotations_emitted;
	}
	runtime->rows_written = total_rows;
	runtime->annotations_emitted = total_annotations;

	if (finalize_decode_output(runtime) != 0)
		return -1;

	return 0;
}
