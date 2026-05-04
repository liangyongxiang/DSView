#ifndef DSVIEW_CLI_SESSION_METADATA_H
#define DSVIEW_CLI_SESSION_METADATA_H

#include <stdint.h>

#include <glib.h>

struct channel_selection_state;
struct cli_support_json_value;

char *cli_waveform_session_metadata_build_dsl_logic_header(
	const struct channel_selection_state *channel_state,
	uint64_t samplerate, uint64_t sample_count,
	uint64_t total_blocks);
struct cli_support_json_value *cli_waveform_session_metadata_build_capture_metadata(
	int dev_mode,
	const struct channel_selection_state *channel_state,
	uint64_t samplerate, uint64_t sample_count,
	int unitsize, gboolean normalize_phys);
struct cli_support_json_value *cli_waveform_session_metadata_build_dsl_logic_session(
	const struct channel_selection_state *channel_state);

#endif
