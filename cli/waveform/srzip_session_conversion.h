#ifndef DSVIEW_CLI_SRZIP_SESSION_CONVERSION_H
#define DSVIEW_CLI_SRZIP_SESSION_CONVERSION_H

#include <stdint.h>
#include <glib.h>

struct channel_selection_state;

struct cli_srzip_session_conversion_request {
	const char *input_file;
	const char *output_file;
	struct channel_selection_state *channel_state;
	gboolean has_requested_channels;
};

struct cli_srzip_session_conversion_result {
	uint64_t samplerate;
	uint64_t sample_count;
	int unitsize;
};

gboolean cli_waveform_srzip_session_input_is_srzip(const char *path);
int cli_waveform_srzip_session_convert_to_dsl(
	const struct cli_srzip_session_conversion_request *request,
	struct cli_srzip_session_conversion_result *result,
	const char **error_text_out);

#endif
