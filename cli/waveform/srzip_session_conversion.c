#include "channel_selection_state.h"
#include "srzip_session_conversion.h"
#include "waveform_archive_output.h"

#include <minizip/unzip.h>
#include <stdlib.h>
#include <string.h>

struct srzip_probe_info {
	int bit_index;
	char name[64];
};

struct srzip_logic_chunk {
	unsigned int index;
	char *name;
};

static void free_srzip_logic_chunk(gpointer data)
{
	struct srzip_logic_chunk *chunk = (struct srzip_logic_chunk *)data;

	if (!chunk)
		return;
	g_free(chunk->name);
	g_free(chunk);
}

static gint compare_srzip_logic_chunk(gconstpointer lhs, gconstpointer rhs)
{
	const struct srzip_logic_chunk *a =
	    *(const struct srzip_logic_chunk * const *)lhs;
	const struct srzip_logic_chunk *b =
	    *(const struct srzip_logic_chunk * const *)rhs;

	if (a->index < b->index)
		return -1;
	if (a->index > b->index)
		return 1;
	return 0;
}

gboolean cli_waveform_srzip_session_input_is_srzip(const char *path)
{
	const char *dot;

	if (!path || !*path)
		return FALSE;
	dot = strrchr(path, '.');
	if (!dot)
		return FALSE;
	return g_ascii_strcasecmp(dot, ".sr") == 0;
}

static char *read_unzip_entry_text(unzFile archive, const char *entry_name,
				   unz_file_info64 *info_out)
{
	unz_file_info64 info;
	char *buf;
	int rd;

	if (unzLocateFile(archive, entry_name, 0) != UNZ_OK)
		return NULL;
	if (unzGetCurrentFileInfo64(archive, &info, NULL, 0, NULL, 0,
				    NULL, 0) != UNZ_OK)
		return NULL;
	if (unzOpenCurrentFile(archive) != UNZ_OK)
		return NULL;

	buf = g_try_malloc0((gsize)info.uncompressed_size + 1U);
	if (!buf) {
		unzCloseCurrentFile(archive);
		return NULL;
	}

	rd = unzReadCurrentFile(archive, buf, (unsigned int)info.uncompressed_size);
	unzCloseCurrentFile(archive);
	if (rd < 0 || (uint64_t)rd != info.uncompressed_size) {
		g_free(buf);
		return NULL;
	}

	if (info_out)
		*info_out = info;
	return buf;
}

static int load_srzip_probe_layout(GKeyFile *kf,
				   struct srzip_probe_info probes[MAX_CH],
				   int *probe_count_out,
				   uint64_t *samplerate_out,
				   int *input_unitsize_out)
{
	char **keys = NULL;
	char *val = NULL;
	gsize key_count = 0;
	uint64_t samplerate = 0;
	int input_unitsize = 0;
	int probe_count = 0;
	int rc = -1;

	keys = g_key_file_get_keys(kf, "device 1", &key_count, NULL);
	if (!keys)
		return -1;

	val = g_key_file_get_string(kf, "device 1", "capturefile", NULL);
	if (!val || strcmp(val, "logic-1") != 0)
		goto done;
	g_free(val);
	val = NULL;

	val = g_key_file_get_string(kf, "device 1", "samplerate", NULL);
	if (!val || sr_parse_sizestring(val, &samplerate) != SR_OK || !samplerate)
		goto done;
	g_free(val);
	val = NULL;

	if (!g_key_file_has_key(kf, "device 1", "unitsize", NULL))
		goto done;
	input_unitsize = g_key_file_get_integer(kf, "device 1", "unitsize", NULL);
	if (input_unitsize <= 0 || input_unitsize > 2)
		goto done;

	memset(probes, 0, sizeof(struct srzip_probe_info) * MAX_CH);
	for (gsize i = 0; i < key_count; i++) {
		unsigned long probe_nr;
		int slot;

		if (strncmp(keys[i], "probe", 5) != 0)
			continue;
		probe_nr = strtoul(keys[i] + 5, NULL, 10);
		if (probe_nr == 0)
			continue;
		slot = (int)probe_nr - 1;
		if (slot < 0 || slot >= MAX_CH)
			goto done;

		val = g_key_file_get_string(kf, "device 1", keys[i], NULL);
		if (!val)
			goto done;

		probes[probe_count].bit_index = slot;
		g_strlcpy(probes[probe_count].name, val,
			  sizeof(probes[probe_count].name));
		g_free(val);
		val = NULL;
		probe_count++;
		if (probe_count > MAX_CH)
			goto done;
	}

	if (probe_count <= 0)
		goto done;

	*probe_count_out = probe_count;
	*samplerate_out = samplerate;
	*input_unitsize_out = input_unitsize;
	rc = 0;

done:
	g_free(val);
	g_strfreev(keys);
	return rc;
}

static int prepare_requested_srzip_channels(
	const struct cli_srzip_session_conversion_request *request,
	struct channel_selection_state *channel_state,
	const struct srzip_probe_info probes[MAX_CH],
	int probe_count,
	int *source_bits,
	const char **error_text_out)
{
	if (!request->has_requested_channels) {
		cli_source_channel_selection_clear_request(channel_state);
	}

	if (cli_source_channel_selection_enabled_count(channel_state) <= 0) {
		for (int i = 0; i < probe_count; i++) {
			if (cli_source_channel_selection_add_request(channel_state, i,
							 probes[i].name) != 0) {
				if (error_text_out)
					*error_text_out =
					    "failed to prepare srzip channel selection";
				return -1;
			}
		}
	}

	if (cli_source_channel_selection_enabled_count(channel_state) <= 0 ||
	    cli_source_channel_selection_enabled_count(channel_state) > probe_count) {
		if (error_text_out)
			*error_text_out = "selected logic channel not found in srzip input";
		return -1;
	}

	for (int seq = 0; seq < cli_source_channel_selection_enabled_count(channel_state); seq++) {
		int source_idx = cli_source_channel_selection_enabled_phys(channel_state, seq);
		const char *requested_name =
		    cli_source_channel_selection_requested_name(channel_state, seq);

		if (source_idx < 0 || source_idx >= probe_count) {
			if (error_text_out)
				*error_text_out =
				    "selected logic channel not found in srzip input";
			return -1;
		}
		source_bits[seq] = probes[source_idx].bit_index;
		if (!requested_name || !requested_name[0]) {
			if (cli_source_channel_selection_add_request(channel_state, source_idx,
							 probes[source_idx].name) != 0) {
				if (error_text_out)
					*error_text_out =
					    "failed to prepare srzip channel selection";
				return -1;
			}
		}
	}

	return 0;
}

static int collect_srzip_logic_chunks(unzFile archive, GPtrArray **chunks_out)
{
	GPtrArray *chunks;

	if (!chunks_out)
		return -1;
	*chunks_out = NULL;

	chunks = g_ptr_array_new_with_free_func(free_srzip_logic_chunk);
	if (unzGoToFirstFile(archive) != UNZ_OK) {
		g_ptr_array_free(chunks, TRUE);
		return -1;
	}

	do {
		char entry_name[128];
		unz_file_info64 info;
		struct srzip_logic_chunk *chunk;
		unsigned long chunk_index = 0;

		if (unzGetCurrentFileInfo64(archive, &info, entry_name,
					    sizeof(entry_name), NULL, 0,
					    NULL, 0) != UNZ_OK) {
			g_ptr_array_free(chunks, TRUE);
			return -1;
		}

		if (strcmp(entry_name, "logic-1") == 0) {
			chunk_index = 1;
		} else if (strncmp(entry_name, "logic-1-", 8) == 0) {
			chunk_index = strtoul(entry_name + 8, NULL, 10);
		} else {
			continue;
		}

		if (chunk_index == 0) {
			g_ptr_array_free(chunks, TRUE);
			return -1;
		}

		chunk = g_malloc0(sizeof(*chunk));
		if (!chunk) {
			g_ptr_array_free(chunks, TRUE);
			return -1;
		}
		chunk->index = (unsigned int)chunk_index;
		chunk->name = g_strdup(entry_name);
		if (!chunk->name) {
			g_free(chunk);
			g_ptr_array_free(chunks, TRUE);
			return -1;
		}
		g_ptr_array_add(chunks, chunk);
	} while (unzGoToNextFile(archive) == UNZ_OK);

	if (chunks->len == 0) {
		g_ptr_array_free(chunks, TRUE);
		return -1;
	}

	g_ptr_array_sort(chunks, compare_srzip_logic_chunk);
	*chunks_out = chunks;
	return 0;
}

static int write_srzip_logic_samples(struct cli_waveform_archive *archive,
				     const uint8_t *src, size_t src_len,
				     int input_unitsize,
				     const int *source_bits,
				     int selected_channels,
				     uint64_t *sample_count_io)
{
	uint8_t *packed = NULL;
	size_t out_unitsize;
	size_t sample_count;
	size_t out_len;
	int rc = -1;

	if (!archive || !src || !src_len || !source_bits || selected_channels <= 0 ||
	    !sample_count_io)
		return -1;
	if ((src_len % (size_t)input_unitsize) != 0)
		return -1;

	sample_count = src_len / (size_t)input_unitsize;
	out_unitsize = (selected_channels <= 8) ? 1U : 2U;
	out_len = sample_count * out_unitsize;
	packed = g_malloc(out_len);
	if (!packed)
		return -1;

	for (size_t sample_idx = 0; sample_idx < sample_count; sample_idx++) {
		uint32_t raw_val = 0;
		uint32_t out_val = 0;
		size_t src_off = sample_idx * (size_t)input_unitsize;
		size_t dst_off = sample_idx * out_unitsize;

		for (int byte_idx = 0; byte_idx < input_unitsize; byte_idx++)
			raw_val |= (uint32_t)src[src_off + (size_t)byte_idx] <<
			    (byte_idx * 8);

		for (int seq = 0; seq < selected_channels; seq++) {
			if ((raw_val >> source_bits[seq]) & 1U)
				out_val |= 1U << seq;
		}

		packed[dst_off] = (uint8_t)(out_val & 0xffU);
		if (out_unitsize > 1)
			packed[dst_off + 1] = (uint8_t)((out_val >> 8) & 0xffU);
	}

	if (cli_waveform_archive_write_logic(archive, packed, (uint64_t)out_len,
					     (uint16_t)out_unitsize) != SR_OK)
		goto done;

	*sample_count_io += (uint64_t)sample_count;
	rc = 0;

done:
	g_free(packed);
	return rc;
}

static int replay_srzip_logic_chunks(unzFile archive,
				     GPtrArray *chunks,
				     struct cli_waveform_archive *dsl_archive,
				     int input_unitsize,
				     const int *source_bits,
				     int selected_channels,
				     uint64_t *sample_count_out,
				     const char **error_text_out)
{
	uint64_t sample_count = 0;

	for (guint i = 0; i < chunks->len; i++) {
		struct srzip_logic_chunk *chunk =
		    (struct srzip_logic_chunk *)g_ptr_array_index(chunks, i);
		uint8_t io_buf[65536];
		int rd;

		if (unzLocateFile(archive, chunk->name, 0) != UNZ_OK) {
			if (error_text_out)
				*error_text_out = "failed to replay srzip logic chunk";
			return -1;
		}
		if (unzOpenCurrentFile(archive) != UNZ_OK) {
			if (error_text_out)
				*error_text_out = "failed to replay srzip logic chunk";
			return -1;
		}

		do {
			rd = unzReadCurrentFile(archive, io_buf, sizeof(io_buf));
			if (rd < 0) {
				unzCloseCurrentFile(archive);
				if (error_text_out)
					*error_text_out = "failed to replay srzip logic chunk";
				return -1;
			}
			if (rd > 0 &&
			    write_srzip_logic_samples(dsl_archive, io_buf, (size_t)rd,
						      input_unitsize, source_bits,
						      selected_channels,
						      &sample_count) != 0) {
				unzCloseCurrentFile(archive);
				if (error_text_out)
					*error_text_out = "failed to repack srzip logic samples";
				return -1;
			}
		} while (rd > 0);

		unzCloseCurrentFile(archive);
	}

	if (!sample_count) {
		if (error_text_out)
			*error_text_out = "srzip input has no logic samples";
		return -1;
	}

	*sample_count_out = sample_count;
	return 0;
}

int cli_waveform_srzip_session_convert_to_dsl(
	const struct cli_srzip_session_conversion_request *request,
	struct cli_srzip_session_conversion_result *result,
	const char **error_text_out)
{
	unzFile archive = NULL;
	GKeyFile *kf = NULL;
	char *metadata = NULL;
	GPtrArray *chunks = NULL;
	struct cli_waveform_archive_request archive_request;
	struct cli_waveform_archive *dsl_archive = NULL;
	struct srzip_probe_info probes[MAX_CH];
	int source_bits[MAX_CH];
	const char *archive_error = NULL;
	uint64_t samplerate = 0;
	uint64_t sample_count = 0;
	int input_unitsize = 0;
	int output_unitsize = 0;
	int probe_count = 0;
	int rc = -1;

	if (error_text_out)
		*error_text_out = NULL;
	if (!request || !request->input_file || !request->output_file ||
	    !request->channel_state || !result) {
		if (error_text_out)
			*error_text_out = "missing srzip session conversion request";
		return -1;
	}

	memset(result, 0, sizeof(*result));

	archive = unzOpen64(request->input_file);
	if (!archive) {
		if (error_text_out)
			*error_text_out = "failed to open srzip input file";
		goto done;
	}

	metadata = read_unzip_entry_text(archive, "metadata", NULL);
	if (!metadata) {
		if (error_text_out)
			*error_text_out = "srzip input is missing metadata";
		goto done;
	}

	kf = g_key_file_new();
	if (!g_key_file_load_from_data(kf, metadata, strlen(metadata), 0, NULL)) {
		if (error_text_out)
			*error_text_out = "srzip metadata is invalid";
		goto done;
	}
	if (load_srzip_probe_layout(kf, probes, &probe_count, &samplerate,
				    &input_unitsize) != 0) {
		if (error_text_out)
			*error_text_out = "srzip input has invalid logic metadata";
		goto done;
	}

	if (prepare_requested_srzip_channels(request, request->channel_state, probes,
					     probe_count, source_bits,
					     error_text_out) != 0)
		goto done;

	if (collect_srzip_logic_chunks(archive, &chunks) != 0) {
		if (error_text_out)
			*error_text_out = "srzip input has no logic chunks";
		goto done;
	}

	output_unitsize =
	    (cli_source_channel_selection_enabled_count(request->channel_state) <= 8) ? 1 : 2;
	memset(&archive_request, 0, sizeof(archive_request));
	archive_request.output_kind = CLI_WAVEFORM_OUTPUT_DSL;
	archive_request.channel_state = request->channel_state;
	archive_request.output_sdi = NULL;
	archive_request.output_file = request->output_file;
	archive_request.samplerate = samplerate;
	archive_request.unitsize = output_unitsize;
	dsl_archive = cli_waveform_archive_create(&archive_request, &archive_error);
	if (!dsl_archive) {
		if (error_text_out)
			*error_text_out = archive_error ?
			    archive_error : "failed to create dsl output archive";
		goto done;
	}

	if (replay_srzip_logic_chunks(archive, chunks, dsl_archive, input_unitsize,
				      source_bits,
				      cli_source_channel_selection_enabled_count(
					      request->channel_state),
				      &sample_count, error_text_out) != 0)
		goto done;

	if (cli_waveform_archive_finalize(dsl_archive, &archive_error) != 0) {
		if (error_text_out)
			*error_text_out = archive_error ?
			    archive_error : "failed to finalize dsl output archive";
		goto done;
	}

	result->samplerate = samplerate;
	result->sample_count = sample_count;
	result->unitsize = output_unitsize;
	rc = 0;

done:
	if (archive)
		unzClose(archive);
	if (kf)
		g_key_file_free(kf);
	g_free(metadata);
	if (chunks)
		g_ptr_array_free(chunks, TRUE);
	cli_waveform_archive_destroy(dsl_archive);
	return rc;
}
