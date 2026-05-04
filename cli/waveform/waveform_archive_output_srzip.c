#include "waveform_archive_output_internal.h"

#include <string.h>

static int create_srzip_output(struct cli_waveform_archive *archive)
{
	const struct sr_output_module *omod;
	GHashTable *params;

	omod = sr_output_find("srzip");
	if (!omod) {
		fprintf(stderr, "srzip output module not found\n");
		return -1;
	}

	params = g_hash_table_new_full(g_str_hash, g_str_equal, NULL,
				       (GDestroyNotify)g_variant_unref);
	g_hash_table_insert(params, "filename",
			    g_variant_ref_sink(
				g_variant_new_bytestring(archive->request.output_file)));

	if (cli_waveform_archive_prepare_output_channel_shadow(archive) != 0) {
		g_hash_table_destroy(params);
		fprintf(stderr, "failed to prepare output channel shadow\n");
		return -1;
	}

	archive->output = g_malloc0(sizeof(*archive->output));
	archive->output->module = omod;
	archive->output->sdi = &archive->output_sdi_shadow;
	archive->output->start_sample_index = 0;

	if (omod->init && omod->init(archive->output, params) != SR_OK) {
		g_hash_table_destroy(params);
		g_free(archive->output);
		archive->output = NULL;
		cli_waveform_archive_free_output_channel_shadow(archive);
		fprintf(stderr, "failed to initialize srzip output\n");
		return -1;
	}

	g_hash_table_destroy(params);
	return 0;
}

static int srzip_write_logic(struct cli_waveform_archive *archive,
			     const uint8_t *data,
			     uint64_t length,
			     uint16_t unitsize)
{
	struct sr_datafeed_logic logic;
	struct sr_datafeed_packet packet;
	GString *out = NULL;
	int ret;

	memset(&logic, 0, sizeof(logic));
	memset(&packet, 0, sizeof(packet));
	logic.length = length;
	logic.format = LA_SPLIT_DATA;
	logic.unitsize = unitsize;
	logic.data = (void *)data;
	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;

	ret = sr_output_send(archive->output, &packet, &out);
	if (out)
		g_string_free(out, TRUE);
	return ret;
}

static int srzip_finalize(struct cli_waveform_archive *archive,
			  const char **error_text_out)
{
	(void)archive;
	if (error_text_out)
		*error_text_out = NULL;
	return 0;
}

int cli_waveform_archive_srzip_init(struct cli_waveform_archive *archive,
				    const char **error_text_out)
{
	static const struct cli_waveform_archive_ops ops = {
		.write_logic = srzip_write_logic,
		.finalize = srzip_finalize,
	};

	if (error_text_out)
		*error_text_out = NULL;
	archive->ops = &ops;

	if (!archive->request.output_sdi || !archive->request.output_file ||
	    !archive->request.output_file[0]) {
		if (error_text_out)
			*error_text_out = "missing srzip output target";
		return -1;
	}

	if (create_srzip_output(archive) != 0) {
		if (error_text_out)
			*error_text_out = "failed to initialize srzip output";
		return -1;
	}

	if (cli_waveform_archive_send_capture_meta(archive) != SR_OK) {
		if (error_text_out)
			*error_text_out = "failed to send waveform metadata";
		return -1;
	}

	return 0;
}
