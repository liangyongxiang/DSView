#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib/gstdio.h>

#include "export.h"

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

static gchar *read_file_text(const char *path)
{
	gchar *text = NULL;
	gsize len = 0;

	if (!g_file_get_contents(path, &text, &len, NULL))
		return NULL;
	(void)len;
	return text;
}

static struct decode_record *make_record(uint64_t start_sample,
					 uint64_t seq,
					 const char *text)
{
	struct decode_record *record = g_new0(struct decode_record, 1);

	record->start_sample = start_sample;
	record->seq = seq;
	record->text = g_strdup(text);
	return record;
}

static void free_records(GPtrArray *records)
{
	if (records)
		g_ptr_array_free(records, TRUE);
}

static void test_select_default_export_row(void)
{
	struct srd_decoder_annotation_row hidden_row = {
		.id = "bits",
		.desc = "Bit values",
	};
	struct srd_decoder_annotation_row visible_row = {
		.id = "data",
		.desc = "Decoded bytes",
	};
	struct srd_decoder dec = {
		.name = "UART",
	};
	const struct srd_decoder_annotation_row *selected;

	hidden_row.ann_classes = g_slist_append(NULL, GINT_TO_POINTER(1));
	visible_row.ann_classes = g_slist_append(NULL, GINT_TO_POINTER(2));
	dec.annotation_rows = g_slist_append(NULL, &hidden_row);
	dec.annotation_rows = g_slist_append(dec.annotation_rows, &visible_row);

	selected = cli_decode_export_select_default_row(&dec);
	expect_true("cli_decode_export_select_default_row", selected == &visible_row,
		    "visible row should win over hidden row");
	expect_true("cli_decode_export_select_default_row",
		    cli_decode_export_row_contains_class(selected, 2),
		    "selected row should contain annotation class 2");

	g_slist_free(hidden_row.ann_classes);
	g_slist_free(visible_row.ann_classes);
	g_slist_free(dec.annotation_rows);
}

static void test_build_annotation_display_text(void)
{
	char *text;
	char *ann_lines[] = { "Value {$}", NULL };
	struct srd_proto_data_annotation templated = {
		.str_number_hex = "41",
		.ann_text = ann_lines,
	};
	struct srd_proto_data_annotation hex_only = {
		.str_number_hex = "0A",
		.ann_text = NULL,
	};

	text = cli_decode_export_build_annotation_text(&templated);
	expect_text("cli_decode_export_build_annotation_text", "Value A", text);
	g_free(text);

	text = cli_decode_export_build_annotation_text(&hex_only);
	expect_text("cli_decode_export_build_annotation_text", "[0A]", text);
	g_free(text);
}

static void test_write_decode_table_for_stack(void)
{
	struct srd_decoder_annotation_row row = {
		.id = "data",
		.desc = "Data",
	};
	struct srd_decoder decoder = {
		.name = "UART",
	};
	struct decode_stack_runtime stack;
	GPtrArray *records;
	gchar *tmp_dir;
	gchar *output_path;
	gchar *csv_text;

	memset(&stack, 0, sizeof(stack));
	records = g_ptr_array_new_with_free_func(cli_decode_export_free_record);
	g_ptr_array_add(records, make_record(5, 1, "late"));
	g_ptr_array_add(records, make_record(2, 0, "with,comma"));
	g_ptr_array_add(records, make_record(2, 1, "quoted \"text\""));

	tmp_dir = g_dir_make_tmp("dsview-cli-decode-export-test-XXXXXX", NULL);
	if (!tmp_dir)
		fail_test("cli_decode_export_write_table_for_stack",
			  "failed to allocate temp directory");
	output_path = g_build_filename(tmp_dir, "decode.csv", NULL);

	stack.output_path = output_path;
	stack.output_format_name = "csv";
	stack.root_dec = &decoder;
	stack.export_row = &row;
	stack.row_title = cli_decode_export_build_row_title(&stack);
	stack.records = records;

	expect_true("cli_decode_export_write_table_for_stack",
		    cli_decode_export_write_table_for_stack(&stack, 1000000ULL) == 0,
		    "table write should succeed");
	expect_true("cli_decode_export_write_table_for_stack", stack.rows_written == 3,
		    "row count mismatch");

	csv_text = read_file_text(output_path);
	expect_true("cli_decode_export_write_table_for_stack", csv_text != NULL,
		    "csv output should exist");
	expect_true("cli_decode_export_write_table_for_stack",
		    g_str_has_prefix(csv_text, "Id,Time[ns],UART: Data"),
		    "csv header mismatch");
	expect_contains("cli_decode_export_write_table_for_stack", csv_text,
			"1,2000.00,\"with,comma\"");
	expect_contains("cli_decode_export_write_table_for_stack", csv_text,
			"2,2000.00,\"quoted \"\"text\"\"\"");
	expect_contains("cli_decode_export_write_table_for_stack", csv_text,
			"3,5000.00,late");

	g_free(csv_text);
	g_free(stack.row_title);
	free_records(records);
	g_remove(output_path);
	g_free(output_path);
	g_rmdir(tmp_dir);
	g_free(tmp_dir);
}

int main(void)
{
	char *format = NULL;

	expect_true("cli_decode_export_infer_output_format",
		    cli_decode_export_infer_output_format("demo.csv", &format) == 0,
		    "csv format inference should succeed");
	expect_text("cli_decode_export_infer_output_format", "csv", format);
	g_free(format);
	format = NULL;
	expect_true("cli_decode_export_infer_output_format",
		    cli_decode_export_infer_output_format("demo.bin", &format) != 0,
		    "invalid format inference should fail");

	test_select_default_export_row();
	test_build_annotation_display_text();
	test_write_decode_table_for_stack();
	printf("decode_export_test: PASS\n");
	return 0;
}
