/*
 * waveform_session.c - shared waveform session execution for dsview-cli.
 */

#include "channel_selection_state.h"
#include "command_result.h"
#include "shape.h"
#include "runtime.h"
#include "device_config.h"
#include "logic_source.h"
#include "device_selected.h"
#include "srzip_session_conversion.h"
#include "waveform_archive_output.h"
#include "waveform_session.h"

#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <time.h>

struct capture_runtime_state {
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	volatile int done;
	volatile int error;
	uint64_t sample_bytes;
	int unitsize;
	uint64_t limit_samples;
	int hw_nch;
	const struct channel_selection_state *channel_state;
	uint8_t cross_leftover[MAX_CH * 8];
	size_t cross_leftover_len;
};

struct capture_callback_bridge {
	struct capture_runtime_state *runtime;
	struct cli_waveform_archive *archive;
	struct decode_runtime *decode_runtime;
	enum cli_stream_mode stream_mode;
};

enum {
	PARALLEL_EMIT_BATCH_GROUPS = 1024
};

static struct capture_callback_bridge capture_bridge = {
	.stream_mode = CLI_STREAM_CAPTURE,
};

static int write_command_error(const char *path, const char *command,
			       const char *error_text)
{
	cli_support_command_result_write_error_json(
		path, command, error_text ? error_text : "command failed");
	return 1;
}

static void write_capture_success(const char *json_path, const char *meta_file,
				  const char *format_id,
				  const char *output_file,
				  const struct channel_selection_state *channel_state,
				  uint64_t samplerate, uint64_t n_samples,
				  int unitsize)
{
	struct cli_capture_result capture_result;

	if (meta_file && *meta_file)
		cli_waveform_archive_write_metadata(LOGIC, channel_state,
						    meta_file, samplerate,
						    n_samples, unitsize);

	printf("Captured %llu logic samples to %s\n",
	       (unsigned long long)n_samples, output_file);
	if (meta_file && *meta_file)
		printf("Metadata: %s\n", meta_file);

	memset(&capture_result, 0, sizeof(capture_result));
	capture_result.mode = "logic";
	capture_result.output_format = format_id;
	capture_result.samples = n_samples;
	capture_result.samplerate = samplerate;
	capture_result.unitsize = unitsize;
	capture_result.file = output_file;
	capture_result.meta = meta_file;
	cli_support_command_result_write_capture_json(json_path, &capture_result);
}

static void write_export_success(const char *json_path, const char *input_file,
				 const char *format_id, const char *output_file,
				 uint64_t samplerate, uint64_t n_samples,
				 int unitsize)
{
	struct cli_export_result export_result;

	printf("Exported %llu logic samples to %s\n",
	       (unsigned long long)n_samples, output_file);

	memset(&export_result, 0, sizeof(export_result));
	export_result.mode = "logic";
	export_result.input_file = input_file;
	export_result.output_format = format_id;
	export_result.samples = n_samples;
	export_result.samplerate = samplerate;
	export_result.unitsize = unitsize;
	export_result.file = output_file;
	cli_support_command_result_write_export_json(json_path, &export_result);
}

static void capture_state_reset_runtime(struct capture_runtime_state *state)
{
	if (!state)
		return;

	state->done = 0;
	state->error = 0;
	state->sample_bytes = 0;
	state->unitsize = 2;
	state->limit_samples = 0;
	state->hw_nch = 0;
	state->channel_state = NULL;
	memset(state->cross_leftover, 0, sizeof(state->cross_leftover));
	state->cross_leftover_len = 0;
}

static void capture_state_init(struct capture_runtime_state *state)
{
	if (!state)
		return;

	memset(state, 0, sizeof(*state));
	pthread_mutex_init(&state->mutex, NULL);
	pthread_cond_init(&state->cond, NULL);
	capture_state_reset_runtime(state);
}

static void bind_capture_runtime(struct capture_runtime_state *runtime,
				 struct cli_waveform_archive *archive,
				 struct decode_runtime *decode_runtime)
{
	capture_bridge.runtime = runtime;
	capture_bridge.archive = archive;
	capture_bridge.decode_runtime = decode_runtime;
}

void cli_waveform_session_bind_decode_runtime(
	struct decode_runtime *decode_runtime)
{
	bind_capture_runtime(NULL, NULL, decode_runtime);
}

void cli_waveform_session_unbind_decode_runtime(void)
{
	capture_bridge.runtime = NULL;
	capture_bridge.archive = NULL;
	capture_bridge.decode_runtime = NULL;
	capture_bridge.stream_mode = CLI_STREAM_CAPTURE;
}

void cli_waveform_session_set_stream_mode(enum cli_stream_mode mode)
{
	capture_bridge.stream_mode = mode;
}

void cli_waveform_session_notify_decode_done(gboolean failed)
{
	struct capture_runtime_state *runtime = capture_bridge.runtime;

	if (!runtime)
		return;

	pthread_mutex_lock(&runtime->mutex);
	runtime->done = 1;
	if (failed)
		runtime->error = 1;
	pthread_cond_signal(&runtime->cond);
	pthread_mutex_unlock(&runtime->mutex);
}

static void timespec_add_msec(struct timespec *ts, uint64_t msec)
{
	ts->tv_sec += (time_t)(msec / 1000ULL);
	ts->tv_nsec += (long)((msec % 1000ULL) * 1000000ULL);
	if (ts->tv_nsec >= 1000000000L) {
		ts->tv_sec += 1;
		ts->tv_nsec -= 1000000000L;
	}
}

static int timespec_cmp(const struct timespec *lhs,
			const struct timespec *rhs)
{
	if (lhs->tv_sec < rhs->tv_sec)
		return -1;
	if (lhs->tv_sec > rhs->tv_sec)
		return 1;
	if (lhs->tv_nsec < rhs->tv_nsec)
		return -1;
	if (lhs->tv_nsec > rhs->tv_nsec)
		return 1;
	return 0;
}

static void capture_event_callback(struct capture_runtime_state *runtime,
				   int event)
{
	switch (event) {
	case DS_EV_COLLECT_TASK_START:
		break;
	case DS_EV_COLLECT_TASK_END:
	case DS_EV_COLLECT_TASK_END_BY_DETACHED:
		pthread_mutex_lock(&runtime->mutex);
		runtime->done = 1;
		pthread_cond_signal(&runtime->cond);
		pthread_mutex_unlock(&runtime->mutex);
		break;
	case DS_EV_COLLECT_TASK_END_BY_ERROR:
		pthread_mutex_lock(&runtime->mutex);
		runtime->done = 1;
		runtime->error = 1;
		pthread_cond_signal(&runtime->cond);
		pthread_mutex_unlock(&runtime->mutex);
		break;
	default:
		break;
	}
}

static int emit_logic_payload(struct capture_runtime_state *runtime,
			      struct cli_waveform_archive *archive,
			      const uint8_t *data,
			      uint64_t length,
			      uint16_t unitsize)
{
	uint64_t remaining_bytes;
	uint64_t max_bytes;
	uint64_t emit_len;
	int ret;

	if (!archive || !data || length == 0)
		return SR_ERR_ARG;
	if (runtime->done)
		return SR_OK;

	emit_len = length;
	if (runtime->limit_samples > 0) {
		max_bytes = runtime->limit_samples * (uint64_t)unitsize;
		if (runtime->sample_bytes >= max_bytes) {
			pthread_mutex_lock(&runtime->mutex);
			runtime->done = 1;
			pthread_cond_signal(&runtime->cond);
			pthread_mutex_unlock(&runtime->mutex);
			return SR_OK;
		}
		remaining_bytes = max_bytes - runtime->sample_bytes;
		if (emit_len > remaining_bytes)
			emit_len = remaining_bytes;
	}

	ret = cli_waveform_archive_write_logic(archive, data, emit_len, unitsize);
	if (ret == SR_OK) {
		runtime->sample_bytes += emit_len;
		if (runtime->limit_samples > 0 &&
		    runtime->sample_bytes >=
			runtime->limit_samples * (uint64_t)unitsize) {
			pthread_mutex_lock(&runtime->mutex);
			runtime->done = 1;
			pthread_cond_signal(&runtime->cond);
			pthread_mutex_unlock(&runtime->mutex);
		}
	}
	return ret;
}

static void convert_one_group(struct capture_runtime_state *runtime,
			      const uint8_t *gp,
			      int nch,
			      int unitsize,
			      uint8_t *out)
{
	const struct channel_selection_state *channel_state =
	    runtime->channel_state;

	memset(out, 0, (size_t)(64 * unitsize));

	for (int ch = 0; ch < nch; ch++) {
		const uint8_t *ch_bytes = gp + ch * 8;
		int bit = cli_source_channel_selection_output_bit_for_source_slot(channel_state,
							       ch);

		if (bit < 0)
			continue;

		for (int b = 0; b < 64; b++) {
			int byte_idx = b / 8;
			int bit_idx = b % 8;

			if (ch_bytes[byte_idx] & (1 << bit_idx))
				out[b * unitsize + bit / 8]
				    |= (uint8_t)(1 << (bit % 8));
		}
	}
}

static int emit_parallel_groups(struct capture_runtime_state *runtime,
				struct cli_waveform_archive *archive,
				const uint8_t *src,
				size_t group_count,
				int nch,
				int unitsize)
{
	size_t grp_in = (size_t)nch * 8U;
	size_t batch_bytes;
	uint8_t *batch;
	int ret;

	if (group_count == 0)
		return SR_OK;

	batch_bytes = group_count * (size_t)64 * (size_t)unitsize;
	batch = g_malloc(batch_bytes);
	if (!batch)
		return SR_ERR_MALLOC;

	for (size_t group = 0; group < group_count; group++) {
		convert_one_group(runtime, src + (group * grp_in), nch, unitsize,
				  batch + (group * (size_t)64 * (size_t)unitsize));
	}

	ret = emit_logic_payload(runtime, archive, batch,
				 (uint64_t)batch_bytes,
				 (uint16_t)unitsize);
	g_free(batch);
	return ret;
}

static void cross_to_parallel(struct capture_runtime_state *runtime,
			      struct cli_waveform_archive *archive,
			      const uint8_t *src,
			      size_t src_len,
			      int nch,
			      int unitsize)
{
	size_t grp_in = (size_t)nch * 8;
	const uint8_t *p = src;
	size_t remain = src_len;
	size_t full_groups;
	size_t emit_groups;

	if (runtime->cross_leftover_len > 0) {
		size_t need = grp_in - runtime->cross_leftover_len;

		if (remain >= need) {
			memcpy(runtime->cross_leftover +
			       runtime->cross_leftover_len,
			       p, need);
			if (emit_parallel_groups(runtime, archive,
						 runtime->cross_leftover,
						 1,
						 nch, unitsize) != SR_OK) {
				pthread_mutex_lock(&runtime->mutex);
				runtime->done = 1;
				runtime->error = 1;
				pthread_cond_signal(&runtime->cond);
				pthread_mutex_unlock(&runtime->mutex);
				return;
			}
			p += need;
			remain -= need;
			runtime->cross_leftover_len = 0;
		} else {
			memcpy(runtime->cross_leftover +
			       runtime->cross_leftover_len,
			       p, remain);
			runtime->cross_leftover_len += remain;
			return;
		}
	}

	full_groups = remain / grp_in;
	while (full_groups > 0) {
		emit_groups = full_groups > PARALLEL_EMIT_BATCH_GROUPS ?
		    PARALLEL_EMIT_BATCH_GROUPS : full_groups;
		if (emit_parallel_groups(runtime, archive, p,
					 emit_groups, nch, unitsize) != SR_OK) {
			pthread_mutex_lock(&runtime->mutex);
			runtime->done = 1;
			runtime->error = 1;
			pthread_cond_signal(&runtime->cond);
			pthread_mutex_unlock(&runtime->mutex);
			return;
		}
		p += emit_groups * grp_in;
		remain -= emit_groups * grp_in;
		full_groups -= emit_groups;
	}

	if (remain > 0) {
		memcpy(runtime->cross_leftover, p, remain);
		runtime->cross_leftover_len = remain;
	}
}

static void capture_datafeed_callback(const struct sr_dev_inst *sdi,
				      struct capture_runtime_state *runtime,
				      struct cli_waveform_archive *archive,
				      const struct sr_datafeed_packet *packet)
{
	const struct channel_selection_state *channel_state =
	    runtime->channel_state;

	(void)sdi;
	if (packet->type == SR_DF_LOGIC && archive) {
		const struct sr_datafeed_logic *logic =
		    (const struct sr_datafeed_logic *)packet->payload;

		if (logic && logic->data && logic->length > 0) {
			if (logic->format == LA_CROSS_DATA &&
			    runtime->hw_nch > 0) {
				cross_to_parallel(runtime, archive,
				    (const uint8_t *)logic->data,
				    (size_t)logic->length,
				    runtime->hw_nch,
				    runtime->unitsize);
			} else if (emit_logic_payload(runtime, archive,
						      (const uint8_t *)logic->data,
						      logic->length,
						      logic->unitsize) != SR_OK) {
				pthread_mutex_lock(&runtime->mutex);
				runtime->done = 1;
				runtime->error = 1;
				pthread_cond_signal(&runtime->cond);
				pthread_mutex_unlock(&runtime->mutex);
			}
		}
	}
	if (packet->type == SR_DF_DSO) {
		const struct sr_datafeed_dso *dso =
		    (const struct sr_datafeed_dso *)packet->payload;
		unsigned nch = (unsigned)
		    (cli_source_channel_selection_enabled_count(channel_state) > 0 ?
		     cli_source_channel_selection_enabled_count(channel_state) : 1);

		if (dso && dso->data && dso->num_samples > 0 && !runtime->done) {
			uint64_t nsamp = (uint64_t)dso->num_samples;
			uint64_t have = runtime->sample_bytes / nch;
			uint64_t want = runtime->limit_samples ?
			    runtime->limit_samples : nsamp;

			if (have < want) {
				uint64_t remaining = want - have;
				uint64_t take = (nsamp < remaining) ? nsamp : remaining;
				size_t len = (size_t)take * nch;

				runtime->sample_bytes += len;
			}
			have = runtime->sample_bytes / nch;
			if (have >= want) {
				pthread_mutex_lock(&runtime->mutex);
				runtime->done = 1;
				pthread_cond_signal(&runtime->cond);
				pthread_mutex_unlock(&runtime->mutex);
			}
		}
	}
	if (packet->type == SR_DF_ANALOG) {
		const struct sr_datafeed_analog *analog =
		    (const struct sr_datafeed_analog *)packet->payload;
		unsigned nch = (unsigned)
		    (cli_source_channel_selection_enabled_count(channel_state) > 0 ?
		     cli_source_channel_selection_enabled_count(channel_state) : 1);

		if (analog && analog->data && analog->num_samples > 0 &&
		    !runtime->done) {
			uint64_t nsamp = (uint64_t)analog->num_samples;
			uint64_t have = runtime->sample_bytes / nch;
			uint64_t want = runtime->limit_samples ?
			    runtime->limit_samples : nsamp;

			if (have < want) {
				uint64_t remaining = want - have;
				uint64_t take = (nsamp < remaining) ? nsamp : remaining;
				size_t len = (size_t)take * nch;

				runtime->sample_bytes += len;
			}
			have = runtime->sample_bytes / nch;
			if (have >= want) {
				pthread_mutex_lock(&runtime->mutex);
				runtime->done = 1;
				pthread_cond_signal(&runtime->cond);
				pthread_mutex_unlock(&runtime->mutex);
			}
		}
	}
}

void cli_waveform_session_event_callback(int event)
{
	if (capture_bridge.stream_mode == CLI_STREAM_DECODE) {
		cli_decode_runtime_on_event(capture_bridge.decode_runtime, event);
		return;
	}

	if (!capture_bridge.runtime)
		return;

	capture_event_callback(capture_bridge.runtime, event);
}

void cli_waveform_session_datafeed_callback(
	const struct sr_dev_inst *sdi,
	const struct sr_datafeed_packet *packet)
{
	if (capture_bridge.stream_mode == CLI_STREAM_DECODE) {
		cli_decode_runtime_on_datafeed(capture_bridge.decode_runtime,
					 sdi, packet);
		return;
	}

	if (!capture_bridge.runtime || !capture_bridge.archive)
		return;

	capture_datafeed_callback(sdi, capture_bridge.runtime,
				  capture_bridge.archive, packet);
}

static int wait_for_waveform_session(struct capture_runtime_state *runtime,
				     enum cli_waveform_source_kind source_kind,
				     enum cli_stream_mode stream_mode,
				     uint64_t time_msec,
				     int *stop_collect_called)
{
	struct timespec guard_deadline;
	struct timespec wall_deadline;
	struct timespec stop_deadline;
	int use_wall_deadline =
	    source_kind == CLI_WAVEFORM_SOURCE_LIVE_CAPTURE &&
	    time_msec > 0;
	int wall_stop_requested = 0;

	clock_gettime(CLOCK_REALTIME, &guard_deadline);
	timespec_add_msec(&guard_deadline, 120000);
	if (use_wall_deadline) {
		clock_gettime(CLOCK_REALTIME, &wall_deadline);
		timespec_add_msec(&wall_deadline, time_msec);
	}

	while (!runtime->done) {
		struct timespec wait_deadline;
		int wait_rc;

		wait_deadline.tv_sec = guard_deadline.tv_sec;
		wait_deadline.tv_nsec = guard_deadline.tv_nsec;
		if (use_wall_deadline && !wall_stop_requested &&
		    timespec_cmp(&wall_deadline, &wait_deadline) < 0) {
			wait_deadline.tv_sec = wall_deadline.tv_sec;
			wait_deadline.tv_nsec = wall_deadline.tv_nsec;
		}
		if (use_wall_deadline && wall_stop_requested &&
		    timespec_cmp(&stop_deadline, &wait_deadline) < 0) {
			wait_deadline.tv_sec = stop_deadline.tv_sec;
			wait_deadline.tv_nsec = stop_deadline.tv_nsec;
		}

		wait_rc = pthread_cond_timedwait(&runtime->cond,
					       &runtime->mutex,
					       &wait_deadline);
		if (wait_rc != ETIMEDOUT)
			continue;

		if (use_wall_deadline && !wall_stop_requested) {
			pthread_mutex_unlock(&runtime->mutex);
			ds_stop_collect();
			pthread_mutex_lock(&runtime->mutex);
			if (stop_collect_called)
				*stop_collect_called = 1;
			wall_stop_requested = 1;
			clock_gettime(CLOCK_REALTIME, &stop_deadline);
			timespec_add_msec(&stop_deadline, 10000);
			continue;
		}

		runtime->error = 1;
		if (stream_mode == CLI_STREAM_DECODE)
			fprintf(stderr,
				"live protocol decode timed out waiting for stream end\n");
		break;
	}

	return runtime->error ? -1 : 0;
}

int cli_waveform_session_run(const struct cli_waveform_session_request *request,
			     struct cli_waveform_session_result *result,
			     const char **error_text_out)
{
	struct capture_runtime_state runtime_storage;
	struct capture_runtime_state *runtime = &runtime_storage;
	struct cli_waveform_archive_request archive_request;
	struct cli_waveform_archive *archive = NULL;
	const char *error_text = NULL;
	int stop_collect_called = 0;
	int stream_bound = 0;
	int collect_started = 0;
	int mutex_locked = 0;
	int rc = -1;

	if (result)
		memset(result, 0, sizeof(*result));
	if (error_text_out)
		*error_text_out = NULL;

	if (!request || !request->channel_state || request->unitsize <= 0) {
		error_text = "invalid waveform session request";
		goto done;
	}
	if (request->stream_mode == CLI_STREAM_DECODE &&
	    !request->decode_runtime) {
		error_text = "missing decode runtime";
		goto done;
	}
	if (request->output_kind == CLI_WAVEFORM_OUTPUT_SRZIP &&
	    (!request->output_sdi || !request->output_file ||
	     !request->output_file[0])) {
		error_text = "missing srzip output target";
		goto done;
	}
	if (request->output_kind == CLI_WAVEFORM_OUTPUT_DSL &&
	    (!request->output_file || !request->output_file[0])) {
		error_text = "missing dsl output target";
		goto done;
	}

	capture_state_init(runtime);
	runtime->channel_state = request->channel_state;
	runtime->limit_samples = request->limit_samples;
	runtime->unitsize = request->unitsize;
	runtime->hw_nch = request->hw_nch;
	memset(&archive_request, 0, sizeof(archive_request));
	archive_request.output_kind = request->output_kind;
	archive_request.channel_state = request->channel_state;
	archive_request.output_sdi = request->output_sdi;
	archive_request.output_file = request->output_file;
	archive_request.samplerate = request->samplerate;
	archive_request.unitsize = request->unitsize;
	archive = cli_waveform_archive_create(&archive_request, &error_text);
	if (!archive)
		goto done;

	bind_capture_runtime(runtime, archive,
			     request->stream_mode == CLI_STREAM_DECODE ?
			     request->decode_runtime : NULL);
	cli_waveform_session_set_stream_mode(request->stream_mode);
	stream_bound = 1;

	pthread_mutex_lock(&runtime->mutex);
	mutex_locked = 1;
	runtime->done = 0;
	if (ds_start_collect() != SR_OK) {
		error_text = request->source_kind == CLI_WAVEFORM_SOURCE_OFFLINE_REPLAY ?
		    "failed to start offline session playback" :
		    "ds_start_collect failed";
		goto done;
	}
	collect_started = 1;

	wait_for_waveform_session(runtime, request->source_kind,
				  request->stream_mode, request->time_msec,
				  &stop_collect_called);
	pthread_mutex_unlock(&runtime->mutex);
	mutex_locked = 0;

	if (!stop_collect_called)
		ds_stop_collect();
	collect_started = 0;

	cli_waveform_session_set_stream_mode(CLI_STREAM_CAPTURE);
	cli_waveform_session_unbind_decode_runtime();
	stream_bound = 0;

	if (result) {
		result->sample_bytes = runtime->sample_bytes;
		result->sample_count = (uint64_t)request->unitsize > 0 ?
		    runtime->sample_bytes / (uint64_t)request->unitsize : 0;
	}

	if (runtime->error) {
		if (!error_text) {
			error_text = request->stream_mode == CLI_STREAM_DECODE ?
			    "live protocol decode timed out waiting for stream end" :
			    "waveform session error or timeout";
		}
		goto done;
	}

	if (cli_waveform_archive_finalize(archive, &error_text) != 0)
		goto done;

	rc = 0;

done:
	if (mutex_locked)
		pthread_mutex_unlock(&runtime->mutex);
	if (collect_started && !stop_collect_called)
		ds_stop_collect();
	if (stream_bound) {
		cli_waveform_session_set_stream_mode(CLI_STREAM_CAPTURE);
		cli_waveform_session_unbind_decode_runtime();
	}
	cli_waveform_archive_destroy(archive);
	if (error_text_out)
		*error_text_out = error_text;
	return rc;
}

int cli_waveform_session_run_live_command(const struct cli_command_shape *shape)
{
	struct decode_runtime *decode_runtime = NULL;
	struct channel_selection_state channel_state_storage;
	struct channel_selection_state *channel_state = &channel_state_storage;
	struct cli_logic_source logic_source;
	struct cli_waveform_session_request session_request;
	struct cli_waveform_session_result session_result;
	struct cli_device_capture_config_request capture_config_request;
	struct cli_device_capture_config_state capture_config_state;
	const char *command_name;
	const char *output_file;
	const char *meta_file;
	const char *json_path;
	const char *format_id;
	const char *session_error = NULL;
	uint64_t samplerate = 0;
	gboolean use_decode_export;
	gboolean use_dsl;
	int rc = 1;

	if (!shape)
		return write_command_error(NULL, "capture",
					   "missing command shape");

	use_decode_export = shape->kind == CLI_COMMAND_LIVE_DECODE;
	command_name = shape->command_name ? shape->command_name :
		       (use_decode_export ? "decode" : "capture");
	output_file = shape->output_file;
	meta_file = shape->meta_file;
	json_path = shape->json_file;
	format_id = shape->output_format_id ? shape->output_format_id : "srzip";
	use_dsl = shape->use_dsl_output;

	cli_source_logic_init(&logic_source);
	memset(&session_request, 0, sizeof(session_request));
	memset(&session_result, 0, sizeof(session_result));
	memset(&capture_config_request, 0, sizeof(capture_config_request));
	memset(&capture_config_state, 0, sizeof(capture_config_state));

	cli_source_channel_selection_reset_defaults(channel_state);
	if (shape->channels &&
	    cli_source_channel_selection_parse(channel_state, shape->channels) != 0) {
		rc = write_command_error(json_path, command_name,
					 "invalid channel selection");
		goto done;
	}
	if (shape->trig_pos_arg)
		cli_source_channel_selection_set_trigger_pos_arg(
			channel_state, shape->trig_pos_arg);

	if (cli_source_logic_open_live(&logic_source, shape, channel_state,
				       &session_error) != 0) {
		rc = write_command_error(
			json_path, command_name,
			session_error ? session_error :
			"failed to prepare logic source");
		goto done;
	}
	samplerate = logic_source.samplerate;

	capture_config_request.samplerate = samplerate;
	capture_config_request.requested_sample_limit = shape->sample_limit;
	capture_config_request.time_msec = shape->time_msec;
	capture_config_request.capture_ratio =
		(uint64_t)cli_source_channel_selection_trigger_position(
			channel_state);
	if (cli_device_config_prepare_live_capture(&capture_config_request,
						   &capture_config_state,
						   &session_error) != 0) {
		rc = write_command_error(
			json_path, command_name,
			session_error ? session_error :
			"failed to prepare live capture config");
		goto done;
	}

	if (use_decode_export) {
		decode_runtime = cli_decode_runtime_create();
		if (!decode_runtime) {
			rc = write_command_error(json_path, command_name,
						 "failed to allocate decode runtime");
			goto done;
		}
		if (cli_decode_runtime_prepare_live(decode_runtime,
						    shape,
						    logic_source.channels,
						    samplerate,
						    logic_source.source_label) != 0) {
			rc = write_command_error(
				json_path, command_name,
				cli_decode_runtime_error_text(decode_runtime));
			goto done;
		}
	}

	session_request.source_kind = CLI_WAVEFORM_SOURCE_LIVE_CAPTURE;
	session_request.stream_mode = use_decode_export ?
		CLI_STREAM_DECODE : CLI_STREAM_CAPTURE;
	session_request.output_kind = use_decode_export ?
		CLI_WAVEFORM_OUTPUT_NONE :
		(use_dsl ? CLI_WAVEFORM_OUTPUT_DSL : CLI_WAVEFORM_OUTPUT_SRZIP);
	session_request.channel_state = channel_state;
	session_request.output_sdi = logic_source.device.info.di;
	session_request.decode_runtime = decode_runtime;
	session_request.output_file = output_file;
	session_request.samplerate = samplerate;
	session_request.limit_samples =
		capture_config_state.runtime_limit_samples;
	session_request.time_msec = shape->time_msec;
	session_request.hw_nch = logic_source.hw_nch;
	session_request.unitsize = logic_source.unitsize;

	if (cli_waveform_session_run(&session_request, &session_result,
				     &session_error) != 0) {
		rc = write_command_error(
			json_path, command_name,
			session_error ? session_error :
			(use_decode_export ?
			 cli_decode_runtime_error_text(decode_runtime) :
			 "capture error or timeout"));
		goto done;
	}

	if (use_decode_export) {
		if (cli_decode_runtime_finalize_live(decode_runtime) != 0) {
			rc = write_command_error(
				json_path, command_name,
				cli_decode_runtime_error_text(decode_runtime));
			goto done;
		}

		printf("Decoded %u stacks to %u files.\n",
		       shape->pd_stack_count,
		       shape->decode_output_count);
		rc = 0;
		goto done;
	}

	write_capture_success(json_path, meta_file, format_id, output_file,
			      channel_state, samplerate,
			      session_result.sample_count,
			      logic_source.unitsize);
	rc = 0;

done:
	if (decode_runtime)
		cli_decode_runtime_destroy(decode_runtime);
	cli_source_logic_close(&logic_source);
	return rc;
}

int cli_waveform_session_run_export_command(const struct cli_command_shape *shape)
{
	struct channel_selection_state channel_state_storage;
	struct channel_selection_state *channel_state = &channel_state_storage;
	struct cli_logic_source logic_source;
	struct cli_srzip_session_conversion_request srzip_request;
	struct cli_srzip_session_conversion_result srzip_result;
	struct cli_waveform_session_request session_request;
	struct cli_waveform_session_result session_result;
	const char *session_error = NULL;
	const char *output_file;
	const char *json_path;
	const char *format_id;
	gboolean use_dsl;
	uint64_t samplerate = 0;
	int unitsize = 0;
	int rc = 1;

	if (!shape)
		return write_command_error(NULL, "export",
					   "missing command shape");

	output_file = shape->output_file;
	json_path = shape->json_file;
	format_id = shape->output_format_id ? shape->output_format_id : "srzip";
	use_dsl = shape->use_dsl_output;

	cli_source_logic_init(&logic_source);
	memset(&srzip_request, 0, sizeof(srzip_request));
	memset(&srzip_result, 0, sizeof(srzip_result));
	memset(&session_request, 0, sizeof(session_request));
	memset(&session_result, 0, sizeof(session_result));
	cli_source_channel_selection_reset_defaults(channel_state);

	if (shape->channels &&
	    cli_source_channel_selection_parse(channel_state, shape->channels) != 0) {
		rc = write_command_error(json_path, "export",
					 "invalid channel selection");
		goto done;
	}
	if (!cli_device_selected_input_is_session(shape->input_file)) {
		rc = write_command_error(
			json_path, "export",
			"stage-1 offline export input must be a local .dsl or .sr file");
		goto done;
	}
	if (!g_file_test(shape->input_file, G_FILE_TEST_IS_REGULAR)) {
		rc = write_command_error(json_path, "export",
					 "offline export input file not found");
		goto done;
	}

	if (cli_waveform_srzip_session_input_is_srzip(shape->input_file)) {
		if (!use_dsl) {
			rc = write_command_error(
				json_path, "export",
				"stage-1 offline .sr export currently supports dsl output only");
			goto done;
		}

		srzip_request.input_file = shape->input_file;
		srzip_request.output_file = output_file;
		srzip_request.channel_state = channel_state;
		srzip_request.has_requested_channels =
			shape->channels && *shape->channels;
		if (cli_waveform_srzip_session_convert_to_dsl(
			&srzip_request, &srzip_result, &session_error) != 0) {
			rc = write_command_error(
				json_path, "export",
				session_error ? session_error :
				"failed to export dsl from srzip input");
			goto done;
		}

		write_export_success(json_path, shape->input_file, format_id,
				     output_file, srzip_result.samplerate,
				     srzip_result.sample_count,
				     srzip_result.unitsize);
		rc = 0;
		goto done;
	}

	if (cli_source_logic_open_input(&logic_source, shape, channel_state, FALSE,
					&session_error) != 0) {
		rc = write_command_error(
			json_path, "export",
			session_error ? session_error :
			"failed to prepare logic source");
		goto done;
	}
	samplerate = logic_source.samplerate;
	unitsize = logic_source.unitsize;

	session_request.source_kind = CLI_WAVEFORM_SOURCE_OFFLINE_REPLAY;
	session_request.stream_mode = CLI_STREAM_CAPTURE;
	session_request.output_kind = use_dsl ?
		CLI_WAVEFORM_OUTPUT_DSL : CLI_WAVEFORM_OUTPUT_SRZIP;
	session_request.channel_state = channel_state;
	session_request.output_sdi = logic_source.device.info.di;
	session_request.output_file = output_file;
	session_request.samplerate = samplerate;
	session_request.limit_samples = 0;
	session_request.time_msec = 0;
	session_request.hw_nch = logic_source.hw_nch;
	session_request.unitsize = unitsize;

	if (cli_waveform_session_run(&session_request, &session_result,
				     &session_error) != 0) {
		rc = write_command_error(
			json_path, "export",
			session_error ? session_error :
			"offline export error or timeout");
		goto done;
	}

	write_export_success(json_path, shape->input_file, format_id, output_file,
			     samplerate, session_result.sample_count, unitsize);
	rc = 0;

done:
	cli_source_logic_close(&logic_source);
	return rc;
}
