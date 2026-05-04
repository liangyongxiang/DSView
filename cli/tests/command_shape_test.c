#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shape.h"
#include "option_state.h"

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

static gchar **dup_strv(const char *first, const char *second)
{
	gchar **items = g_new0(gchar *, second ? 3 : 2);

	items[0] = g_strdup(first);
	if (second)
		items[1] = g_strdup(second);
	return items;
}

static void test_selector_conflict(void)
{
	struct cli_option_state opts;
	struct cli_command_shape shape;

	cli_command_option_state_init(&opts);
	cli_command_shape_init(&shape);
	opts.scan_devs = TRUE;
	opts.show = TRUE;

	expect_true("selector_conflict",
		    cli_command_shape_build(&shape, &opts) != 0,
		    "shape build should fail");
	expect_text("selector_conflict",
		    "top-level command selectors cannot be combined",
		    cli_command_shape_error_text(&shape));

	cli_command_shape_clear(&shape);
	cli_command_option_state_clear(&opts);
}

static void test_capture_normalization(void)
{
	struct cli_option_state opts;
	struct cli_command_shape shape;

	cli_command_option_state_init(&opts);
	cli_command_shape_init(&shape);
	opts.drv = g_strdup("virtual-demo");
	opts.output_file = g_strdup("capture.sr");
	opts.samples = g_strdup("1000");

	expect_true("capture_normalization",
		    cli_command_shape_build(&shape, &opts) == 0,
		    "shape build should succeed");
	expect_true("capture_normalization",
		    shape.kind == CLI_COMMAND_LIVE_CAPTURE,
		    "expected live capture");
	expect_text("capture_normalization", "capture", shape.command_name);
	expect_text("capture_normalization", "srzip", shape.output_format_id);
	expect_true("capture_normalization", !shape.use_dsl_output,
		    "capture should default to srzip output");
	expect_true("capture_normalization", shape.has_samples,
		    "capture should normalize --samples");
	expect_true("capture_normalization", shape.sample_limit == 1000ULL,
		    "capture sample limit mismatch");

	cli_command_shape_clear(&shape);
	cli_command_option_state_clear(&opts);
}

static void test_offline_export_dsl(void)
{
	struct cli_option_state opts;
	struct cli_command_shape shape;

	cli_command_option_state_init(&opts);
	cli_command_shape_init(&shape);
	opts.input_file = g_strdup("fixture.sr");
	opts.output_file = g_strdup("fixture.dsl");
	opts.output_format = g_strdup("dsl");

	expect_true("offline_export_dsl",
		    cli_command_shape_build(&shape, &opts) == 0,
		    "shape build should succeed");
	expect_true("offline_export_dsl",
		    shape.kind == CLI_COMMAND_OFFLINE_EXPORT,
		    "expected offline export");
	expect_text("offline_export_dsl", "export", shape.command_name);
	expect_true("offline_export_dsl", shape.use_dsl_output,
		    "dsl output flag should be set");
	expect_text("offline_export_dsl", "dsl", shape.output_format_id);

	cli_command_shape_clear(&shape);
	cli_command_option_state_clear(&opts);
}

static void test_live_decode_output_mismatch(void)
{
	struct cli_option_state opts;
	struct cli_command_shape shape;

	cli_command_option_state_init(&opts);
	cli_command_shape_init(&shape);
	opts.drv = g_strdup("virtual-demo");
	opts.pd_stacks = dup_strv("uart", NULL);
	opts.decode_outputs = dup_strv("uart-a.csv", "uart-b.csv");

	expect_true("live_decode_output_mismatch",
		    cli_command_shape_build(&shape, &opts) != 0,
		    "shape build should fail");
	expect_text("live_decode_output_mismatch",
		    "live protocol decode requires one --decode-output for each -P",
		    cli_command_shape_error_text(&shape));

	cli_command_shape_clear(&shape);
	cli_command_option_state_clear(&opts);
}

static void test_live_decode_rejects_waveform_output(void)
{
	struct cli_option_state opts;
	struct cli_command_shape shape;

	cli_command_option_state_init(&opts);
	cli_command_shape_init(&shape);
	opts.drv = g_strdup("virtual-demo");
	opts.pd_stacks = dup_strv("uart", NULL);
	opts.decode_outputs = dup_strv("uart.csv", NULL);
	opts.output_file = g_strdup("capture.dsl");

	expect_true("live_decode_rejects_waveform_output",
		    cli_command_shape_build(&shape, &opts) != 0,
		    "shape build should fail");
	expect_text("live_decode_rejects_waveform_output",
		    "live protocol decode mode cannot be mixed with capture output options",
		    cli_command_shape_error_text(&shape));

	cli_command_shape_clear(&shape);
	cli_command_option_state_clear(&opts);
}

int main(void)
{
	test_selector_conflict();
	test_capture_normalization();
	test_offline_export_dsl();
	test_live_decode_output_mismatch();
	test_live_decode_rejects_waveform_output();
	printf("command_shape_test: PASS\n");
	return 0;
}
