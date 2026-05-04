#ifndef DSVIEW_CLI_WAVEFORM_ARCHIVE_OUTPUT_INTERNAL_H
#define DSVIEW_CLI_WAVEFORM_ARCHIVE_OUTPUT_INTERNAL_H

#include <stdint.h>
#include <stdio.h>

#include <glib.h>

#include "libsigrok4DSL/libsigrok-internal.h"
#include "waveform_archive_output.h"

struct cli_waveform_archive_ops {
	int (*write_logic)(struct cli_waveform_archive *archive,
			   const uint8_t *data,
			   uint64_t length,
			   uint16_t unitsize);
	int (*finalize)(struct cli_waveform_archive *archive,
			const char **error_text_out);
};

struct cli_waveform_archive {
	struct cli_waveform_archive_request request;
	const struct cli_waveform_archive_ops *ops;
	struct sr_output *output;
	FILE *raw_capture_file;
	struct sr_dev_inst output_sdi_shadow;
	GSList *output_channels_shadow;
	char *dsl_tmp_dir;
	char *dsl_raw_path;
	uint64_t sample_bytes;
};

SR_API const struct sr_output_module *sr_output_find(char *id);
SR_API int sr_output_send(const struct sr_output *o,
			  const struct sr_datafeed_packet *packet,
			  GString **out);
SR_API int sr_output_free(const struct sr_output *o);

int cli_waveform_archive_none_init(struct cli_waveform_archive *archive,
				   const char **error_text_out);
int cli_waveform_archive_srzip_init(struct cli_waveform_archive *archive,
				    const char **error_text_out);
int cli_waveform_archive_dsl_init(struct cli_waveform_archive *archive,
				  const char **error_text_out);

int cli_waveform_archive_create_temp_logic_capture_file(
	uint64_t samplerate,
	uint32_t header_channels,
	FILE **raw_file_out,
	char **tmp_dir_out,
	char **raw_path_out);
void cli_waveform_archive_cleanup_temp_logic_capture_file(FILE **raw_file,
							  char **tmp_dir_path,
							  char **raw_path);

void cli_waveform_archive_free_output_channel_shadow(
	struct cli_waveform_archive *archive);
int cli_waveform_archive_prepare_output_channel_shadow(
	struct cli_waveform_archive *archive);
int cli_waveform_archive_send_capture_meta(struct cli_waveform_archive *archive);

int cli_waveform_archive_write_logic_dsl_file(
	const struct channel_selection_state *channel_state,
	const char *raw_path,
	const char *out_path,
	uint64_t samplerate,
	uint64_t sample_count,
	int unitsize);

#endif
