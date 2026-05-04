#ifndef DSVIEW_CLI_WAVEFORM_ARCHIVE_OUTPUT_H
#define DSVIEW_CLI_WAVEFORM_ARCHIVE_OUTPUT_H

#include <stdint.h>

#include "waveform_session.h"

struct cli_waveform_archive;

struct cli_waveform_archive_request {
	enum cli_waveform_output_kind output_kind;
	const struct channel_selection_state *channel_state;
	const struct sr_dev_inst *output_sdi;
	const char *output_file;
	uint64_t samplerate;
	int unitsize;
};

struct cli_waveform_archive *cli_waveform_archive_create(
	const struct cli_waveform_archive_request *request,
	const char **error_text_out);
int cli_waveform_archive_write_logic(struct cli_waveform_archive *archive,
				     const uint8_t *data,
				     uint64_t length,
				     uint16_t unitsize);
int cli_waveform_archive_finalize(struct cli_waveform_archive *archive,
				  const char **error_text_out);
void cli_waveform_archive_destroy(struct cli_waveform_archive *archive);

void cli_waveform_archive_write_metadata(
	int dev_mode,
	const struct channel_selection_state *channel_state,
	const char *meta_path,
	uint64_t samplerate,
	uint64_t n_samples,
	int unitsize);

#endif
