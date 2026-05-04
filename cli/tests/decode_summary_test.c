#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib/gstdio.h>

#include "summary.h"
#include "stack_runtime.h"

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

static gchar *read_file_text(const char *path)
{
	gchar *text = NULL;
	gsize len = 0;

	if (!g_file_get_contents(path, &text, &len, NULL))
		return NULL;
	(void)len;
	return text;
}

static void cleanup_stack_runtime(struct decode_stack_runtime *stack)
{
	g_free(stack->stack_spec);
	g_free(stack->output_path);
	g_free(stack->output_format_name);
	g_free(stack->row_title);
	g_free(stack->error_text);
}

static void test_write_decode_summary_live(void)
{
	struct srd_decoder dec0 = { .id = "0:uart" };
	struct srd_decoder dec1 = { .id = "1:midi" };
	struct decode_stack_runtime stack0;
	struct decode_stack_runtime stack1;
	GPtrArray *stacks;
	gchar *tmp_dir;
	gchar *json_path;
	gchar *json_text;

	memset(&stack0, 0, sizeof(stack0));
	memset(&stack1, 0, sizeof(stack1));
	stack0.index = 1;
	stack0.stack_spec = g_strdup("uart:rx=CH0");
	stack0.output_path = g_strdup("uart.csv");
	stack0.output_format_name = g_strdup("csv");
	stack0.root_dec = &dec0;
	stack0.rows_written = 4;
	stack0.annotations_emitted = 6;
	stack0.row_title = g_strdup("UART: Data");
	stack0.success = TRUE;

	stack1.index = 2;
	stack1.stack_spec = g_strdup("uart:rx=CH0,midi");
	stack1.output_path = g_strdup("midi.txt");
	stack1.output_format_name = g_strdup("txt");
	stack1.root_dec = &dec1;
	stack1.rows_written = 2;
	stack1.annotations_emitted = 3;
	stack1.row_title = g_strdup("MIDI: Messages");
	stack1.success = FALSE;
	stack1.error_text = g_strdup("decoder timeout");

	stacks = g_ptr_array_new();
	g_ptr_array_add(stacks, &stack0);
	g_ptr_array_add(stacks, &stack1);

	tmp_dir = g_dir_make_tmp("dsview-cli-decode-summary-test-XXXXXX", NULL);
	if (!tmp_dir)
		fail_test("write_decode_summary_live",
			  "failed to allocate temp directory");
	json_path = g_build_filename(tmp_dir, "decode.json", NULL);

	expect_true("write_decode_summary_live",
		    cli_decode_summary_write_json(json_path, TRUE, "demo-device",
					      stacks, 6, 9) == 0,
		    "summary write should succeed");
	json_text = read_file_text(json_path);
	expect_true("write_decode_summary_live", json_text != NULL,
		    "summary output should exist");
	expect_contains("write_decode_summary_live", json_text,
			"\"command\":\"decode\"");
	expect_contains("write_decode_summary_live", json_text,
			"\"success\":true");
	expect_contains("write_decode_summary_live", json_text,
			"\"input_source\":\"live\"");
	expect_contains("write_decode_summary_live", json_text,
			"\"input_label\":\"demo-device\"");
	expect_contains("write_decode_summary_live", json_text,
			"\"stack_count\":2");
	expect_contains("write_decode_summary_live", json_text,
			"\"root_decoder\":\"0:uart\"");
	expect_contains("write_decode_summary_live", json_text,
			"\"row_title\":\"UART: Data\"");
	expect_contains("write_decode_summary_live", json_text,
			"\"error\":\"decoder timeout\"");

	g_free(json_text);
	g_remove(json_path);
	g_free(json_path);
	g_rmdir(tmp_dir);
	g_free(tmp_dir);
	g_ptr_array_free(stacks, TRUE);
	cleanup_stack_runtime(&stack0);
	cleanup_stack_runtime(&stack1);
}

static void test_write_decode_summary_offline(void)
{
	struct decode_stack_runtime stack;
	GPtrArray *stacks;
	gchar *tmp_dir;
	gchar *json_path;
	gchar *json_text;

	memset(&stack, 0, sizeof(stack));
	stack.index = 1;
	stack.stack_spec = g_strdup("uart:rx=CH1");
	stack.output_path = g_strdup("out.csv");
	stack.output_format_name = g_strdup("csv");
	stack.rows_written = 1;
	stack.annotations_emitted = 1;
	stack.success = TRUE;

	stacks = g_ptr_array_new();
	g_ptr_array_add(stacks, &stack);

	tmp_dir = g_dir_make_tmp("dsview-cli-decode-summary-offline-XXXXXX", NULL);
	if (!tmp_dir)
		fail_test("write_decode_summary_offline",
			  "failed to allocate temp directory");
	json_path = g_build_filename(tmp_dir, "offline.json", NULL);

	expect_true("write_decode_summary_offline",
		    cli_decode_summary_write_json(json_path, FALSE, "capture.dsl",
					      stacks, 1, 1) == 0,
		    "offline summary write should succeed");
	json_text = read_file_text(json_path);
	expect_true("write_decode_summary_offline", json_text != NULL,
		    "offline summary output should exist");
	expect_contains("write_decode_summary_offline", json_text,
			"\"input_file\":\"capture.dsl\"");

	g_free(json_text);
	g_remove(json_path);
	g_free(json_path);
	g_rmdir(tmp_dir);
	g_free(tmp_dir);
	g_ptr_array_free(stacks, TRUE);
	cleanup_stack_runtime(&stack);
}

int main(void)
{
	test_write_decode_summary_live();
	test_write_decode_summary_offline();
	printf("decode_summary_test: PASS\n");
	return 0;
}
