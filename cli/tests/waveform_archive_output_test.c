#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib/gstdio.h>
#include <minizip/unzip.h>

#include "channel_selection_state.h"
#include "waveform_archive_output.h"
#include "libsigrok4DSL/libsigrok-internal.h"

void cli_test_waveform_archive_stub_reset(void);
int cli_test_waveform_archive_stub_meta_packets(void);
int cli_test_waveform_archive_stub_logic_packets(void);
uint64_t cli_test_waveform_archive_stub_logic_bytes(void);
int cli_test_waveform_archive_stub_cleanup_calls(void);

static void fail_test(const char *name, const char *message)
{
	fprintf(stderr, "[FAIL] %s: %s\n", name, message);
	exit(1);
}

static void expect_true(const char *name, int condition, const char *message)
{
	if (!condition)
		fail_test(name, message);
}

static void expect_text(const char *name, const char *expected,
			const char *actual)
{
	if (g_strcmp0(expected, actual) != 0) {
		fprintf(stderr,
			"[FAIL] %s: expected \"%s\", got \"%s\"\n",
			name, expected ? expected : "(null)",
			actual ? actual : "(null)");
		exit(1);
	}
}

static void expect_contains(const char *name, const char *text,
			    const char *needle)
{
	if (!text || !needle || !strstr(text, needle)) {
		fprintf(stderr, "[FAIL] %s: expected to find \"%s\"\n",
			name, needle ? needle : "(null)");
		exit(1);
	}
}

static gchar *read_zip_entry_text(const char *archive_path,
				  const char *entry_name)
{
	unzFile archive;
	unz_file_info64 info;
	gchar *text;
	int rd;

	archive = unzOpen64(archive_path);
	if (!archive)
		return NULL;
	if (unzLocateFile(archive, entry_name, 0) != UNZ_OK) {
		unzClose(archive);
		return NULL;
	}
	if (unzGetCurrentFileInfo64(archive, &info, NULL, 0, NULL, 0, NULL, 0) !=
	    UNZ_OK) {
		unzClose(archive);
		return NULL;
	}
	if (unzOpenCurrentFile(archive) != UNZ_OK) {
		unzClose(archive);
		return NULL;
	}

	text = g_malloc0((gsize)info.uncompressed_size + 1U);
	rd = unzReadCurrentFile(archive, text, (unsigned int)info.uncompressed_size);
	unzCloseCurrentFile(archive);
	unzClose(archive);
	if (rd < 0 || (uint64_t)rd != info.uncompressed_size) {
		g_free(text);
		return NULL;
	}
	return text;
}

static gsize read_zip_entry_bytes(const char *archive_path,
				  const char *entry_name,
				  guint8 *buffer,
				  gsize buffer_size)
{
	unzFile archive;
	unz_file_info64 info;
	int rd;

	if (!buffer || buffer_size == 0)
		return 0;

	archive = unzOpen64(archive_path);
	if (!archive)
		return 0;
	if (unzLocateFile(archive, entry_name, 0) != UNZ_OK) {
		unzClose(archive);
		return 0;
	}
	if (unzGetCurrentFileInfo64(archive, &info, NULL, 0, NULL, 0, NULL, 0) !=
	    UNZ_OK) {
		unzClose(archive);
		return 0;
	}
	if ((gsize)info.uncompressed_size > buffer_size) {
		unzClose(archive);
		return 0;
	}
	if (unzOpenCurrentFile(archive) != UNZ_OK) {
		unzClose(archive);
		return 0;
	}
	rd = unzReadCurrentFile(archive, buffer, (unsigned int)info.uncompressed_size);
	unzCloseCurrentFile(archive);
	unzClose(archive);
	if (rd < 0 || (uint64_t)rd != info.uncompressed_size)
		return 0;
	return (gsize)rd;
}

static void prepare_logic_channels(struct channel_selection_state *state)
{
	cli_source_channel_selection_reset_defaults(state);
	cli_source_channel_selection_clear_request(state);
	if (cli_source_channel_selection_add_request(state, 2, "D2") != 0 ||
	    cli_source_channel_selection_add_request(state, 0, "D0") != 0) {
		fail_test("prepare_logic_channels", "failed to build channel selection");
	}
}

static void free_test_channels(GSList *channels)
{
	for (GSList *l = channels; l; l = l->next) {
		struct sr_channel *ch = (struct sr_channel *)l->data;

		if (!ch)
			continue;
		g_free(ch->name);
		g_free(ch);
	}
	g_slist_free(channels);
}

static GSList *build_test_channels(void)
{
	GSList *channels = NULL;
	struct {
		int index;
		const char *name;
	} defs[] = {
		{ 0, "CH0" },
		{ 1, "CH1" },
		{ 2, "CH2" },
	};

	for (size_t i = 0; i < G_N_ELEMENTS(defs); i++) {
		struct sr_channel *ch = g_malloc0(sizeof(*ch));

		ch->index = (uint16_t)defs[i].index;
		ch->type = SR_CHANNEL_LOGIC;
		ch->enabled = TRUE;
		ch->name = g_strdup(defs[i].name);
		channels = g_slist_append(channels, ch);
	}

	return channels;
}

static void test_none_archive_output(void)
{
	struct channel_selection_state state;
	struct cli_waveform_archive_request request;
	struct cli_waveform_archive *archive;
	const char *error_text = NULL;
	guint8 logic_payload[] = { 0x01, 0x02 };

	prepare_logic_channels(&state);
	cli_test_waveform_archive_stub_reset();
	memset(&request, 0, sizeof(request));
	request.output_kind = CLI_WAVEFORM_OUTPUT_NONE;
	request.channel_state = &state;
	request.samplerate = 1000000ULL;
	request.unitsize = 1;

	archive = cli_waveform_archive_create(&request, &error_text);
	if (!archive)
		fail_test("none_archive_output",
			  error_text ? error_text : "archive creation should succeed");
	expect_true("none_archive_output",
		    cli_waveform_archive_write_logic(archive, logic_payload,
						    sizeof(logic_payload), 1) == SR_OK,
		    "logic payload write should succeed");
	expect_true("none_archive_output",
		    cli_waveform_archive_finalize(archive, &error_text) == 0,
		    error_text ? error_text : "finalize should succeed");
	expect_true("none_archive_output",
		    cli_test_waveform_archive_stub_meta_packets() == 0 &&
		    cli_test_waveform_archive_stub_logic_packets() == 0,
		    "none adapter should not touch srzip output hooks");
	cli_waveform_archive_destroy(archive);
}

static void test_srzip_archive_output(void)
{
	struct channel_selection_state state;
	struct cli_waveform_archive_request request;
	struct cli_waveform_archive *archive;
	struct sr_dev_inst sdi;
	GSList *channels;
	const char *error_text = NULL;
	guint8 logic_payload[] = { 0x01, 0x02, 0x03 };

	prepare_logic_channels(&state);
	cli_test_waveform_archive_stub_reset();
	channels = build_test_channels();
	memset(&sdi, 0, sizeof(sdi));
	sdi.channels = channels;

	memset(&request, 0, sizeof(request));
	request.output_kind = CLI_WAVEFORM_OUTPUT_SRZIP;
	request.channel_state = &state;
	request.output_sdi = &sdi;
	request.output_file = "capture.sr";
	request.samplerate = 1000000ULL;
	request.unitsize = 1;

	archive = cli_waveform_archive_create(&request, &error_text);
	if (!archive)
		fail_test("srzip_archive_output",
			  error_text ? error_text : "archive creation should succeed");
	expect_true("srzip_archive_output",
		    cli_test_waveform_archive_stub_meta_packets() == 1,
		    "srzip create should emit exactly one meta packet");
	expect_true("srzip_archive_output",
		    cli_waveform_archive_write_logic(archive, logic_payload,
						    sizeof(logic_payload), 1) == SR_OK,
		    "logic payload write should succeed");
	expect_true("srzip_archive_output",
		    cli_test_waveform_archive_stub_logic_packets() == 1,
		    "srzip adapter should emit one logic packet");
	expect_true("srzip_archive_output",
		    cli_test_waveform_archive_stub_logic_bytes() ==
			sizeof(logic_payload),
		    "srzip adapter should emit the logic payload length");
	expect_true("srzip_archive_output",
		    cli_waveform_archive_finalize(archive, &error_text) == 0,
		    error_text ? error_text : "finalize should succeed");
	cli_waveform_archive_destroy(archive);
	expect_true("srzip_archive_output",
		    cli_test_waveform_archive_stub_cleanup_calls() == 1,
		    "srzip adapter should release the output module once");
	free_test_channels(channels);
}

static void test_missing_dsl_target(void)
{
	struct channel_selection_state state;
	struct cli_waveform_archive_request request;
	struct cli_waveform_archive *archive;
	const char *error_text = NULL;

	prepare_logic_channels(&state);
	memset(&request, 0, sizeof(request));
	request.output_kind = CLI_WAVEFORM_OUTPUT_DSL;
	request.channel_state = &state;
	request.samplerate = 1000000ULL;
	request.unitsize = 1;

	archive = cli_waveform_archive_create(&request, &error_text);
	expect_true("missing_dsl_target", archive == NULL,
		    "archive creation should fail");
	expect_text("missing_dsl_target", "missing dsl output target", error_text);
}

static void test_dsl_archive_output(void)
{
	struct channel_selection_state state;
	struct cli_waveform_archive_request request;
	struct cli_waveform_archive *archive;
	const char *error_text = NULL;
	gchar *tmp_dir;
	gchar *output_path;
	gchar *header_text;
	gchar *session_text;
	gchar *decoders_text;
	guint8 logic_payload[] = { 0x01, 0x02, 0x03 };
	guint8 channel2_block[4] = { 0 };
	guint8 channel0_block[4] = { 0 };

	prepare_logic_channels(&state);
	tmp_dir = g_dir_make_tmp("dsview-cli-waveform-archive-test-XXXXXX", NULL);
	if (!tmp_dir)
		fail_test("dsl_archive_output", "failed to allocate temp directory");
	output_path = g_build_filename(tmp_dir, "capture.dsl", NULL);

	memset(&request, 0, sizeof(request));
	request.output_kind = CLI_WAVEFORM_OUTPUT_DSL;
	request.channel_state = &state;
	request.output_file = output_path;
	request.samplerate = 1000000ULL;
	request.unitsize = 1;

	archive = cli_waveform_archive_create(&request, &error_text);
	if (!archive)
		fail_test("dsl_archive_output",
			  error_text ? error_text : "archive creation should succeed");
	expect_true("dsl_archive_output",
		    cli_waveform_archive_write_logic(archive, logic_payload,
						    sizeof(logic_payload), 1) == SR_OK,
		    "logic payload write should succeed");
	expect_true("dsl_archive_output",
		    cli_waveform_archive_finalize(archive, &error_text) == 0,
		    error_text ? error_text : "finalize should succeed");
	cli_waveform_archive_destroy(archive);

	expect_true("dsl_archive_output",
		    g_file_test(output_path, G_FILE_TEST_IS_REGULAR),
		    "dsl output file should exist");

	header_text = read_zip_entry_text(output_path, "header");
	session_text = read_zip_entry_text(output_path, "session");
	decoders_text = read_zip_entry_text(output_path, "decoders");
	expect_true("dsl_archive_output", header_text != NULL,
		    "header entry should exist");
	expect_true("dsl_archive_output", session_text != NULL,
		    "session entry should exist");
	expect_text("dsl_archive_output", "[]\n", decoders_text);
	expect_contains("dsl_archive_output", header_text, "total samples = 3");
	expect_contains("dsl_archive_output", header_text, "total probes = 2");
	expect_contains("dsl_archive_output", header_text, "probe2 = D2");
	expect_contains("dsl_archive_output", header_text, "probe0 = D0");
	expect_contains("dsl_archive_output", session_text, "\"index\":2");
	expect_contains("dsl_archive_output", session_text, "\"view_index\":0");
	expect_contains("dsl_archive_output", session_text, "\"name\":\"D2\"");
	expect_contains("dsl_archive_output", session_text, "\"index\":0");
	expect_contains("dsl_archive_output", session_text, "\"view_index\":1");
	expect_contains("dsl_archive_output", session_text, "\"name\":\"D0\"");

	expect_true("dsl_archive_output",
		    read_zip_entry_bytes(output_path, "L-2/0", channel2_block,
					 sizeof(channel2_block)) == 1,
		    "channel 2 block should be one byte");
	expect_true("dsl_archive_output",
		    read_zip_entry_bytes(output_path, "L-0/0", channel0_block,
					 sizeof(channel0_block)) == 1,
		    "channel 0 block should be one byte");
	expect_true("dsl_archive_output", channel2_block[0] == 0x05,
		    "channel 2 block mismatch");
	expect_true("dsl_archive_output", channel0_block[0] == 0x06,
		    "channel 0 block mismatch");

	g_free(header_text);
	g_free(session_text);
	g_free(decoders_text);
	g_remove(output_path);
	g_free(output_path);
	g_rmdir(tmp_dir);
	g_free(tmp_dir);
}

int main(void)
{
	test_none_archive_output();
	test_srzip_archive_output();
	test_missing_dsl_target();
	test_dsl_archive_output();
	printf("waveform_archive_output_test: PASS\n");
	return 0;
}
