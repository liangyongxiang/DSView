#include "waveform_archive_output_internal.h"

static int none_write_logic(struct cli_waveform_archive *archive,
			    const uint8_t *data,
			    uint64_t length,
			    uint16_t unitsize)
{
	(void)archive;
	(void)data;
	(void)length;
	(void)unitsize;
	return SR_OK;
}

static int none_finalize(struct cli_waveform_archive *archive,
			 const char **error_text_out)
{
	(void)archive;
	if (error_text_out)
		*error_text_out = NULL;
	return 0;
}

int cli_waveform_archive_none_init(struct cli_waveform_archive *archive,
				   const char **error_text_out)
{
	static const struct cli_waveform_archive_ops ops = {
		.write_logic = none_write_logic,
		.finalize = none_finalize,
	};

	if (error_text_out)
		*error_text_out = NULL;
	archive->ops = &ops;
	return 0;
}
