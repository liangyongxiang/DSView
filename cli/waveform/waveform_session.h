#ifndef DSVIEW_CLI_WAVEFORM_SESSION_H
#define DSVIEW_CLI_WAVEFORM_SESSION_H

#include <stdint.h>
#include <stdio.h>
#include <glib.h>

#include "libsigrok4DSL/libsigrok.h"

struct channel_selection_state;
struct cli_command_shape;
struct decode_runtime;

enum cli_stream_mode {
	CLI_STREAM_CAPTURE = 0,
	CLI_STREAM_DECODE = 1,
};

enum cli_waveform_source_kind {
	CLI_WAVEFORM_SOURCE_LIVE_CAPTURE = 0,
	CLI_WAVEFORM_SOURCE_OFFLINE_REPLAY = 1,
};

enum cli_waveform_output_kind {
	CLI_WAVEFORM_OUTPUT_NONE = 0,
	CLI_WAVEFORM_OUTPUT_SRZIP = 1,
	CLI_WAVEFORM_OUTPUT_DSL = 2,
};

struct cli_waveform_session_request {
	enum cli_waveform_source_kind source_kind;
	enum cli_stream_mode stream_mode;
	enum cli_waveform_output_kind output_kind;
	const struct channel_selection_state *channel_state;
	const struct sr_dev_inst *output_sdi;
	struct decode_runtime *decode_runtime;
	const char *output_file;
	uint64_t samplerate;
	uint64_t limit_samples;
	uint64_t time_msec;
	int hw_nch;
	int unitsize;
};

struct cli_waveform_session_result {
	uint64_t sample_bytes;
	uint64_t sample_count;
};

int cli_waveform_session_run(const struct cli_waveform_session_request *request,
			     struct cli_waveform_session_result *result,
			     const char **error_text_out);
int cli_waveform_session_run_live_command(
	const struct cli_command_shape *shape);
int cli_waveform_session_run_export_command(
	const struct cli_command_shape *shape);

void cli_waveform_session_bind_decode_runtime(
	struct decode_runtime *decode_runtime);
void cli_waveform_session_unbind_decode_runtime(void);
void cli_waveform_session_set_stream_mode(enum cli_stream_mode mode);
void cli_waveform_session_notify_decode_done(gboolean failed);

void cli_waveform_session_event_callback(int event);
void cli_waveform_session_datafeed_callback(
	const struct sr_dev_inst *sdi,
	const struct sr_datafeed_packet *packet);

#endif
