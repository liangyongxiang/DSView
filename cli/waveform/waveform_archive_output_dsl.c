#include "waveform_archive_output_internal.h"

static int dsl_write_logic(struct cli_waveform_archive *archive,
			   const uint8_t *data,
			   uint64_t length,
			   uint16_t unitsize)
{
	(void)unitsize;
	return (fwrite(data, 1, (size_t)length, archive->raw_capture_file) ==
		(size_t)length) ? SR_OK : SR_ERR;
}

static int dsl_finalize(struct cli_waveform_archive *archive,
			const char **error_text_out)
{
	if (error_text_out)
		*error_text_out = NULL;

	if (archive->raw_capture_file) {
		fclose(archive->raw_capture_file);
		archive->raw_capture_file = NULL;
	}

	if (cli_waveform_archive_write_logic_dsl_file(
		archive->request.channel_state,
		archive->dsl_raw_path,
		archive->request.output_file,
		archive->request.samplerate,
		archive->sample_bytes / (uint64_t)archive->request.unitsize,
		archive->request.unitsize) != 0) {
		if (error_text_out)
			*error_text_out = "failed to export dsl output";
		return -1;
	}

	return 0;
}

int cli_waveform_archive_dsl_init(struct cli_waveform_archive *archive,
				  const char **error_text_out)
{
	static const struct cli_waveform_archive_ops ops = {
		.write_logic = dsl_write_logic,
		.finalize = dsl_finalize,
	};

	if (error_text_out)
		*error_text_out = NULL;
	archive->ops = &ops;

	if (!archive->request.output_file || !archive->request.output_file[0]) {
		if (error_text_out)
			*error_text_out = "missing dsl output target";
		return -1;
	}

	if (cli_waveform_archive_create_temp_logic_capture_file(
		archive->request.samplerate,
		(uint32_t)cli_source_channel_selection_enabled_count(
			archive->request.channel_state),
		&archive->raw_capture_file,
		&archive->dsl_tmp_dir,
		&archive->dsl_raw_path) != 0) {
		if (error_text_out)
			*error_text_out = "failed to initialize dsl waveform output";
		return -1;
	}

	return 0;
}
