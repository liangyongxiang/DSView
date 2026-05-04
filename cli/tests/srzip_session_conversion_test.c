#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glib/gstdio.h>
#include <minizip/unzip.h>
#include <minizip/zip.h>

#include "channel_selection_state.h"
#include "srzip_session_conversion.h"

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

static int write_zip_text_entry(zipFile archive, const zip_fileinfo *zi,
				const char *entry_name, const char *text)
{
	if (zipOpenNewFileInZip(archive, entry_name, (zip_fileinfo *)zi,
				NULL, 0, NULL, 0, NULL, Z_DEFLATED,
				Z_BEST_SPEED) != ZIP_OK)
		return -1;
	if (zipWriteInFileInZip(archive, text, (unsigned int)strlen(text)) != ZIP_OK ||
	    zipCloseFileInZip(archive) != ZIP_OK)
		return -1;
	return 0;
}

static int write_zip_bytes_entry(zipFile archive, const zip_fileinfo *zi,
				 const char *entry_name,
				 const guint8 *data, gsize length)
{
	if (zipOpenNewFileInZip(archive, entry_name, (zip_fileinfo *)zi,
				NULL, 0, NULL, 0, NULL, Z_DEFLATED,
				Z_BEST_SPEED) != ZIP_OK)
		return -1;
	if (zipWriteInFileInZip(archive, data, (unsigned int)length) != ZIP_OK ||
	    zipCloseFileInZip(archive) != ZIP_OK)
		return -1;
	return 0;
}

static void prepare_channel_request(struct channel_selection_state *state)
{
	cli_source_channel_selection_reset_defaults(state);
	cli_source_channel_selection_clear_request(state);
	if (cli_source_channel_selection_add_request(state, 2, "D2") != 0 ||
	    cli_source_channel_selection_add_request(state, 0, "D0") != 0) {
		fail_test("prepare_channel_request",
			  "failed to build requested srzip channels");
	}
}

static void create_minimal_srzip_input(const char *archive_path)
{
	static const char metadata_text[] =
	    "[device 1]\n"
	    "capturefile=logic-1\n"
	    "samplerate=1000000 Hz\n"
	    "unitsize=1\n"
	    "probe1=D0\n"
	    "probe2=D1\n"
	    "probe3=D2\n";
	static const guint8 logic_bytes[] = { 0x01, 0x06, 0x05 };
	zip_fileinfo zi;
	time_t rawtime;
	struct tm *ti;
	zipFile archive;

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

	g_remove(archive_path);
	archive = zipOpen64(archive_path, FALSE);
	if (!archive)
		fail_test("create_minimal_srzip_input", "failed to create srzip input");
	if (write_zip_text_entry(archive, &zi, "metadata", metadata_text) != 0 ||
	    write_zip_bytes_entry(archive, &zi, "logic-1",
				  logic_bytes, sizeof(logic_bytes)) != 0) {
		zipClose(archive, NULL);
		fail_test("create_minimal_srzip_input",
			  "failed to populate srzip input");
	}
	zipClose(archive, NULL);
}

static void test_srzip_to_dsl_conversion(void)
{
	struct channel_selection_state state;
	struct cli_srzip_session_conversion_request request;
	struct cli_srzip_session_conversion_result result;
	const char *error_text = NULL;
	gchar *tmp_dir;
	gchar *input_path;
	gchar *output_path;
	gchar *header_text;
	guint8 channel2_block[4] = { 0 };
	guint8 channel0_block[4] = { 0 };

	prepare_channel_request(&state);
	tmp_dir = g_dir_make_tmp("dsview-cli-srzip-conversion-test-XXXXXX", NULL);
	if (!tmp_dir)
		fail_test("srzip_to_dsl_conversion",
			  "failed to allocate temp directory");
	input_path = g_build_filename(tmp_dir, "input.sr", NULL);
	output_path = g_build_filename(tmp_dir, "output.dsl", NULL);
	create_minimal_srzip_input(input_path);

	memset(&request, 0, sizeof(request));
	memset(&result, 0, sizeof(result));
	request.input_file = input_path;
	request.output_file = output_path;
	request.channel_state = &state;
	request.has_requested_channels = TRUE;

	expect_true("srzip_to_dsl_conversion",
		    cli_waveform_srzip_session_convert_to_dsl(&request, &result,
						     &error_text) == 0,
		    error_text ? error_text : "conversion should succeed");
	expect_true("srzip_to_dsl_conversion", result.samplerate == 1000000ULL,
		    "samplerate mismatch");
	expect_true("srzip_to_dsl_conversion", result.sample_count == 3ULL,
		    "sample count mismatch");
	expect_true("srzip_to_dsl_conversion", result.unitsize == 1,
		    "unitsize mismatch");

	header_text = read_zip_entry_text(output_path, "header");
	expect_true("srzip_to_dsl_conversion", header_text != NULL,
		    "output header entry should exist");
	expect_contains("srzip_to_dsl_conversion", header_text, "probe2 = D2");
	expect_contains("srzip_to_dsl_conversion", header_text, "probe0 = D0");
	expect_true("srzip_to_dsl_conversion",
		    read_zip_entry_bytes(output_path, "L-2/0", channel2_block,
					 sizeof(channel2_block)) == 1,
		    "channel 2 block should be one byte");
	expect_true("srzip_to_dsl_conversion",
		    read_zip_entry_bytes(output_path, "L-0/0", channel0_block,
					 sizeof(channel0_block)) == 1,
		    "channel 0 block should be one byte");
	expect_true("srzip_to_dsl_conversion", channel2_block[0] == 0x06,
		    "channel 2 block mismatch");
	expect_true("srzip_to_dsl_conversion", channel0_block[0] == 0x05,
		    "channel 0 block mismatch");

	g_free(header_text);
	g_remove(output_path);
	g_remove(input_path);
	g_free(output_path);
	g_free(input_path);
	g_rmdir(tmp_dir);
	g_free(tmp_dir);
}

int main(void)
{
	test_srzip_to_dsl_conversion();
	printf("srzip_session_conversion_test: PASS\n");
	return 0;
}
