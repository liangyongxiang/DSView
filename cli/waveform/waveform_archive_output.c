#include "channel_selection_state.h"
#include "json.h"
#include "dsl_layout.h"
#include "session_metadata.h"
#include "waveform_archive_output_internal.h"

#include <errno.h>
#include <minizip/zip.h>
#include <string.h>

#include <glib/gstdio.h>

#include "libsigrok4DSL/libsigrok-internal.h"

int cli_waveform_archive_create_temp_logic_capture_file(uint64_t samplerate,
							uint32_t header_channels,
							FILE **raw_file_out,
							char **tmp_dir_out,
							char **raw_path_out)
{
	gchar *tmp_dir = NULL;
	gchar *raw_path = NULL;
	FILE *raw_file = NULL;
	uint8_t header[12];

	if (raw_file_out)
		*raw_file_out = NULL;
	if (tmp_dir_out)
		*tmp_dir_out = NULL;
	if (raw_path_out)
		*raw_path_out = NULL;

	tmp_dir = g_dir_make_tmp("dsview-cli-dsl-XXXXXX", NULL);
	if (!tmp_dir)
		return -1;

	raw_path = g_build_filename(tmp_dir, "capture.bin", NULL);
	raw_file = fopen(raw_path, "wb");
	if (!raw_file)
		goto fail;

	for (int i = 0; i < 8; i++)
		header[i] = (uint8_t)((samplerate >> (i * 8)) & 0xff);
	for (int i = 0; i < 4; i++)
		header[8 + i] = (uint8_t)((header_channels >> (i * 8)) & 0xff);

	if (fwrite(header, 1, sizeof(header), raw_file) != sizeof(header))
		goto fail;

	if (raw_file_out)
		*raw_file_out = raw_file;
	if (tmp_dir_out)
		*tmp_dir_out = tmp_dir;
	else
		g_free(tmp_dir);
	if (raw_path_out)
		*raw_path_out = raw_path;
	else
		g_free(raw_path);
	return 0;

fail:
	if (raw_file)
		fclose(raw_file);
	if (raw_path) {
		g_remove(raw_path);
		g_free(raw_path);
	}
	if (tmp_dir) {
		g_rmdir(tmp_dir);
		g_free(tmp_dir);
	}
	return -1;
}

void cli_waveform_archive_cleanup_temp_logic_capture_file(FILE **raw_file,
							  char **tmp_dir_path,
							  char **raw_path)
{
	if (raw_file && *raw_file) {
		fclose(*raw_file);
		*raw_file = NULL;
	}

	if (raw_path && *raw_path) {
		g_remove(*raw_path);
		g_free(*raw_path);
		*raw_path = NULL;
	}

	if (tmp_dir_path && *tmp_dir_path) {
		g_rmdir(*tmp_dir_path);
		g_free(*tmp_dir_path);
		*tmp_dir_path = NULL;
	}
}

static void write_metadata_internal(int dev_mode,
				    const char *meta_path,
				    const struct channel_selection_state *channel_state,
				    uint64_t samplerate,
				    uint64_t n_samples,
				    int unitsize,
				    gboolean normalize_phys)
{
	struct cli_support_json_value *metadata =
		cli_waveform_session_metadata_build_capture_metadata(
			dev_mode, channel_state, samplerate, n_samples, unitsize,
			normalize_phys);

	if (!metadata)
		return;

	cli_support_json_write_value_file(meta_path, metadata);
	cli_support_json_value_free(metadata);
}

void cli_waveform_archive_write_metadata(
	int dev_mode,
	const struct channel_selection_state *channel_state,
	const char *meta_path,
	uint64_t samplerate,
	uint64_t n_samples,
	int unitsize)
{
	write_metadata_internal(dev_mode, meta_path, channel_state, samplerate,
				n_samples, unitsize, FALSE);
}

void cli_waveform_archive_free_output_channel_shadow(
	struct cli_waveform_archive *archive)
{
	for (GSList *l = archive->output_channels_shadow; l; l = l->next) {
		struct sr_channel *ch = (struct sr_channel *)l->data;

		if (ch) {
			g_free(ch->name);
			g_free(ch->trigger);
			g_free(ch);
		}
	}
	g_slist_free(archive->output_channels_shadow);
	archive->output_channels_shadow = NULL;
	memset(&archive->output_sdi_shadow, 0, sizeof(archive->output_sdi_shadow));
}

int cli_waveform_archive_prepare_output_channel_shadow(
	struct cli_waveform_archive *archive)
{
	int n_enabled = cli_source_channel_selection_enabled_count(
	    archive->request.channel_state);
	const struct sr_dev_inst *source = archive->request.output_sdi;

	if (!source)
		return -1;

	cli_waveform_archive_free_output_channel_shadow(archive);
	archive->output_sdi_shadow = *source;
	archive->output_sdi_shadow.channels = NULL;

	for (int seq = 0; seq < n_enabled; seq++) {
		struct sr_channel *orig = NULL;
		struct sr_channel *copy;
		char default_name[32];
		int phys = cli_source_channel_selection_enabled_phys(
		    archive->request.channel_state, seq);
		const char *name = cli_source_channel_selection_display_name(
		    archive->request.channel_state, seq,
		    default_name, sizeof(default_name));

		for (GSList *l = source->channels; l; l = l->next) {
			struct sr_channel *cand = (struct sr_channel *)l->data;

			if ((int)cand->index == phys) {
				orig = cand;
				break;
			}
		}
		if (!orig)
			return -1;

		copy = g_malloc0(sizeof(*copy));
		*copy = *orig;
		copy->index = (uint16_t)seq;
		copy->enabled = TRUE;
		copy->name = g_strdup(name ? name :
				      (orig->name ? orig->name : ""));
		copy->trigger = orig->trigger ? g_strdup(orig->trigger) : NULL;
		archive->output_channels_shadow =
		    g_slist_append(archive->output_channels_shadow, copy);
	}

	archive->output_sdi_shadow.channels = archive->output_channels_shadow;
	return 0;
}

int cli_waveform_archive_send_capture_meta(struct cli_waveform_archive *archive)
{
	struct sr_config src;
	struct sr_datafeed_meta meta;
	struct sr_datafeed_packet packet;
	GSList *cfg = NULL;
	GString *out = NULL;
	int ret;

	memset(&src, 0, sizeof(src));
	memset(&meta, 0, sizeof(meta));
	memset(&packet, 0, sizeof(packet));

	src.key = SR_CONF_SAMPLERATE;
	src.data = g_variant_ref_sink(
	    g_variant_new_uint64(archive->request.samplerate));
	cfg = g_slist_append(cfg, &src);
	meta.config = cfg;
	packet.type = SR_DF_META;
	packet.payload = &meta;
	ret = sr_output_send(archive->output, &packet, &out);
	if (out)
		g_string_free(out, TRUE);
	g_slist_free(cfg);
	g_variant_unref(src.data);
	return ret;
}

int cli_waveform_archive_write_logic_dsl_file(
	const struct channel_selection_state *channel_state,
	const char *raw_path,
	const char *out_path,
	uint64_t samplerate,
	uint64_t sample_count,
	int unitsize)
{
	FILE *f = NULL;
	zipFile zip_archive = NULL;
	zip_fileinfo zi;
	char *header_text = NULL;
	char *session_json = NULL;
	struct cli_support_json_value *session_value = NULL;
	uint8_t *sample_buf = NULL;
	uint8_t **channel_blocks = NULL;
	uint8_t header_bytes[12];
	int ret = -1;
	uint64_t total_blocks;

	if (!raw_path || !out_path || sample_count == 0 || unitsize <= 0)
		return -1;

	f = fopen(raw_path, "rb");
	if (!f)
		goto done;
	if (fread(header_bytes, 1, sizeof(header_bytes), f) != sizeof(header_bytes))
		goto done;

	total_blocks = (sample_count + DSL_LOGIC_BLOCK_SAMPLES - 1ULL) /
	    DSL_LOGIC_BLOCK_SAMPLES;
	header_text = cli_waveform_session_metadata_build_dsl_logic_header(
		channel_state, samplerate, sample_count, total_blocks);
	session_value =
	    cli_waveform_session_metadata_build_dsl_logic_session(channel_state);
	if (!session_value)
		goto done;
	session_json = cli_support_json_render(session_value);
	if (!header_text || !session_json)
		goto done;

	g_remove(out_path);
	zip_archive = zipOpen64(out_path, FALSE);
	if (!zip_archive)
		goto done;

	{
		time_t rawtime;
		struct tm *ti;

		memset(&zi, 0, sizeof(zi));
		time(&rawtime);
		ti = localtime(&rawtime);
		if (ti) {
			zi.tmz_date.tm_year = ti->tm_year;
			zi.tmz_date.tm_mon = ti->tm_mon;
			zi.tmz_date.tm_mday = ti->tm_mday;
			zi.tmz_date.tm_hour = ti->tm_hour;
			zi.tmz_date.tm_min = ti->tm_min;
			zi.tmz_date.tm_sec = ti->tm_sec;
		}
	}

	if (zipOpenNewFileInZip(zip_archive, "header", &zi, NULL, 0, NULL, 0,
				NULL, Z_DEFLATED, Z_BEST_SPEED) != ZIP_OK)
		goto done;
	if (zipWriteInFileInZip(zip_archive, header_text,
				(unsigned int)strlen(header_text)) != ZIP_OK ||
	    zipCloseFileInZip(zip_archive) != ZIP_OK)
		goto done;
	if (zipOpenNewFileInZip(zip_archive, "decoders", &zi, NULL, 0, NULL, 0,
				NULL, Z_DEFLATED, Z_BEST_SPEED) != ZIP_OK)
		goto done;
	if (zipWriteInFileInZip(zip_archive, "[]\n", 3) != ZIP_OK ||
	    zipCloseFileInZip(zip_archive) != ZIP_OK)
		goto done;
	if (zipOpenNewFileInZip(zip_archive, "session", &zi, NULL, 0, NULL, 0,
				NULL, Z_DEFLATED, Z_BEST_SPEED) != ZIP_OK)
		goto done;
	if (zipWriteInFileInZip(zip_archive, session_json,
				(unsigned int)strlen(session_json)) != ZIP_OK ||
	    zipCloseFileInZip(zip_archive) != ZIP_OK)
		goto done;

	channel_blocks = g_malloc0(sizeof(*channel_blocks) *
				   cli_source_channel_selection_enabled_count(channel_state));
	if (!channel_blocks)
		goto done;

	for (uint64_t block_index = 0; block_index < total_blocks; block_index++) {
		uint64_t sample_start = block_index * DSL_LOGIC_BLOCK_SAMPLES;
		uint64_t samples_in_block = sample_count - sample_start;
		size_t block_bytes;
		size_t sample_bytes;

		if (samples_in_block > DSL_LOGIC_BLOCK_SAMPLES)
			samples_in_block = DSL_LOGIC_BLOCK_SAMPLES;
		block_bytes = (size_t)((samples_in_block + 7ULL) / 8ULL);
		sample_bytes = (size_t)(samples_in_block * (uint64_t)unitsize);

		for (int ch = 0;
		     ch < cli_source_channel_selection_enabled_count(channel_state);
		     ch++) {
			g_free(channel_blocks[ch]);
			channel_blocks[ch] = g_malloc0(block_bytes);
			if (!channel_blocks[ch])
				goto done;
		}

		sample_buf = g_realloc(sample_buf, sample_bytes);
		if (!sample_buf)
			goto done;
		if (fread(sample_buf, 1, sample_bytes, f) != sample_bytes)
			goto done;

		for (uint64_t sample_idx = 0;
		     sample_idx < samples_in_block;
		     sample_idx++) {
			uint32_t raw_val;
			size_t sample_off = (size_t)(sample_idx * (uint64_t)unitsize);
			size_t byte_index = (size_t)(sample_idx >> 3);
			uint8_t bit_mask = (uint8_t)(1u << (sample_idx & 7));

			if (unitsize == 1) {
				raw_val = sample_buf[sample_off];
			} else {
				raw_val = sample_buf[sample_off] |
				    ((uint32_t)sample_buf[sample_off + 1] << 8);
			}

			for (int seq = 0;
			     seq < cli_source_channel_selection_enabled_count(channel_state);
			     seq++) {
				if ((raw_val >> seq) & 1U)
					channel_blocks[seq][byte_index] |= bit_mask;
			}
		}

		for (int seq = 0;
		     seq < cli_source_channel_selection_enabled_count(channel_state);
		     seq++) {
			char *chunk_name = g_strdup_printf("L-%d/%llu",
							    cli_source_channel_selection_enabled_phys(
								    channel_state,
								    seq),
							    (unsigned long long)block_index);

			if (!chunk_name)
				goto done;
			if (zipOpenNewFileInZip(zip_archive, chunk_name, &zi,
						NULL, 0, NULL, 0, NULL,
						Z_DEFLATED, Z_BEST_SPEED) != ZIP_OK) {
				g_free(chunk_name);
				goto done;
			}
			if (zipWriteInFileInZip(zip_archive, channel_blocks[seq],
						(unsigned int)block_bytes) != ZIP_OK ||
			    zipCloseFileInZip(zip_archive) != ZIP_OK) {
				g_free(chunk_name);
				goto done;
			}
			g_free(chunk_name);
		}
	}

	ret = 0;

done:
	if (zip_archive)
		zipClose(zip_archive, NULL);
	if (ret != 0)
		g_remove(out_path);
	if (channel_blocks) {
		for (int ch = 0;
		     ch < cli_source_channel_selection_enabled_count(channel_state);
		     ch++)
			g_free(channel_blocks[ch]);
		g_free(channel_blocks);
	}
	g_free(sample_buf);
	g_free(header_text);
	g_free(session_json);
	cli_support_json_value_free(session_value);
	if (f)
		fclose(f);
	return ret;
}

static int initialize_archive_adapter(struct cli_waveform_archive *archive,
				      const char **error_text_out)
{
	switch (archive->request.output_kind) {
	case CLI_WAVEFORM_OUTPUT_NONE:
		return cli_waveform_archive_none_init(archive, error_text_out);
	case CLI_WAVEFORM_OUTPUT_SRZIP:
		return cli_waveform_archive_srzip_init(archive, error_text_out);
	case CLI_WAVEFORM_OUTPUT_DSL:
		return cli_waveform_archive_dsl_init(archive, error_text_out);
	default:
		if (error_text_out)
			*error_text_out = "invalid waveform output kind";
		return -1;
	}
}

struct cli_waveform_archive *cli_waveform_archive_create(
	const struct cli_waveform_archive_request *request,
	const char **error_text_out)
{
	struct cli_waveform_archive *archive;

	if (error_text_out)
		*error_text_out = NULL;
	if (!request || !request->channel_state || request->unitsize <= 0) {
		if (error_text_out)
			*error_text_out = "invalid waveform archive request";
		return NULL;
	}

	archive = g_malloc0(sizeof(*archive));
	archive->request = *request;

	if (initialize_archive_adapter(archive, error_text_out) == 0)
		return archive;

	cli_waveform_archive_destroy(archive);
	return NULL;
}

int cli_waveform_archive_write_logic(struct cli_waveform_archive *archive,
				     const uint8_t *data,
				     uint64_t length,
				     uint16_t unitsize)
{
	int ret;

	if (!archive || !data || length == 0)
		return SR_ERR_ARG;
	if ((int)unitsize != archive->request.unitsize)
		return SR_ERR_ARG;
	if (!archive->ops || !archive->ops->write_logic)
		ret = SR_ERR_ARG;
	else
		ret = archive->ops->write_logic(archive, data, length, unitsize);

	if (ret == SR_OK)
		archive->sample_bytes += length;
	return ret;
}

int cli_waveform_archive_finalize(struct cli_waveform_archive *archive,
				  const char **error_text_out)
{
	if (error_text_out)
		*error_text_out = NULL;
	if (!archive)
		return -1;
	if (!archive->ops || !archive->ops->finalize)
		return 0;
	return archive->ops->finalize(archive, error_text_out);
}

void cli_waveform_archive_destroy(struct cli_waveform_archive *archive)
{
	if (!archive)
		return;

	if (archive->output) {
		sr_output_free(archive->output);
		archive->output = NULL;
	}
	cli_waveform_archive_free_output_channel_shadow(archive);

	cli_waveform_archive_cleanup_temp_logic_capture_file(
		&archive->raw_capture_file,
		&archive->dsl_tmp_dir,
		&archive->dsl_raw_path);
	g_free(archive);
}
