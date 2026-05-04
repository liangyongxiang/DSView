#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stack_plan.h"

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

static gchar **dup_strv(const char *first)
{
	gchar **items = g_new0(gchar *, 2);

	items[0] = g_strdup(first);
	return items;
}

static struct sr_channel *make_logic_channel(int index, const char *name)
{
	struct sr_channel *channel = g_new0(struct sr_channel, 1);

	channel->index = (uint16_t)index;
	channel->name = g_strdup(name);
	channel->enabled = TRUE;
	return channel;
}

static GSList *build_logic_channels(void)
{
	GSList *channels = NULL;

	channels = g_slist_append(channels, make_logic_channel(0, "CH0"));
	channels = g_slist_append(channels, make_logic_channel(1, "CH1"));
	channels = g_slist_append(channels, make_logic_channel(2, "CH2"));
	return channels;
}

static void free_logic_channels(GSList *channels)
{
	for (GSList *l = channels; l; l = l->next) {
		struct sr_channel *channel = (struct sr_channel *)l->data;

		if (!channel)
			continue;
		g_free(channel->name);
		g_free(channel);
	}
	g_slist_free(channels);
}

static void clear_shape_storage(struct cli_command_shape *shape)
{
	g_strfreev(shape->pd_stacks);
	g_strfreev(shape->decode_outputs);
	memset(shape, 0, sizeof(*shape));
}

static void prepare_decode_shape(struct cli_command_shape *shape,
				 const char *stack_spec,
				 const char *output_path)
{
	memset(shape, 0, sizeof(*shape));
	shape->pd_stacks = dup_strv(stack_spec);
	shape->decode_outputs = dup_strv(output_path);
	shape->pd_stack_count = 1;
	shape->decode_output_count = 1;
}

static void test_single_uart_plan(void)
{
	struct cli_command_shape shape;
	GSList *channels = build_logic_channels();
	GPtrArray *plans = NULL;
	char *error_text = NULL;
	struct decode_stack_plan *plan;
	struct decode_decoder_step_plan *step;
	GVariant *baudrate;
	GVariant *bound_channel;

	prepare_decode_shape(&shape, "uart:rx=CH1:baudrate=115200", "uart.csv");
	if (cli_decode_stack_plan_build(&shape, channels, &plans, &error_text) != 0)
		fail_test("single_uart_plan",
			  error_text ? error_text : "plan build should succeed");
	expect_true("single_uart_plan", plans && plans->len == 1,
		    "expected one decode stack plan");

	plan = (struct decode_stack_plan *)g_ptr_array_index(plans, 0);
	expect_text("single_uart_plan", "csv", plan->output_format_name);
	expect_text("single_uart_plan", "0:uart", plan->root_decoder->id);
	expect_text("single_uart_plan", "UART: Data", plan->row_title);
	expect_true("single_uart_plan", plan->decoder_steps->len == 1,
		    "expected one decode step");

	step = (struct decode_decoder_step_plan *)
	    g_ptr_array_index(plan->decoder_steps, 0);
	expect_text("single_uart_plan", "0:uart", step->decoder_id);
	baudrate = (GVariant *)g_hash_table_lookup(step->options, "baudrate");
	expect_true("single_uart_plan", baudrate != NULL,
		    "baudrate option should be present");
	expect_true("single_uart_plan",
		    g_variant_get_int64(baudrate) == 115200,
		    "baudrate option mismatch");
	bound_channel =
	    (GVariant *)g_hash_table_lookup(step->channel_indices, "rx");
	expect_true("single_uart_plan", bound_channel != NULL,
		    "rx binding should be present");
	expect_true("single_uart_plan",
		    g_variant_get_int32(bound_channel) == 1,
		    "rx binding should resolve to CH1");

	g_ptr_array_free(plans, TRUE);
	g_free(error_text);
	clear_shape_storage(&shape);
	free_logic_channels(channels);
}

static void test_stacked_uart_midi_plan(void)
{
	struct cli_command_shape shape;
	GSList *channels = build_logic_channels();
	GPtrArray *plans = NULL;
	char *error_text = NULL;
	struct decode_stack_plan *plan;
	struct decode_decoder_step_plan *final_step;

	prepare_decode_shape(&shape, "uart:rx=CH0:baudrate=9600,midi", "stack.txt");
	if (cli_decode_stack_plan_build(&shape, channels, &plans, &error_text) != 0)
		fail_test("stacked_uart_midi_plan",
			  error_text ? error_text : "plan build should succeed");
	expect_true("stacked_uart_midi_plan", plans && plans->len == 1,
		    "expected one decode stack plan");

	plan = (struct decode_stack_plan *)g_ptr_array_index(plans, 0);
	expect_text("stacked_uart_midi_plan", "txt", plan->output_format_name);
	expect_true("stacked_uart_midi_plan", plan->decoder_steps->len == 2,
		    "expected two stacked decode steps");

	final_step = (struct decode_decoder_step_plan *)
	    g_ptr_array_index(plan->decoder_steps, 1);
	expect_text("stacked_uart_midi_plan", "1:midi", final_step->decoder_id);

	g_ptr_array_free(plans, TRUE);
	g_free(error_text);
	clear_shape_storage(&shape);
	free_logic_channels(channels);
}

static void test_bad_root_channel_binding(void)
{
	struct cli_command_shape shape;
	GSList *channels = build_logic_channels();
	GPtrArray *plans = NULL;
	char *error_text = NULL;

	prepare_decode_shape(&shape, "uart:rx=CH9:baudrate=9600", "bad.csv");
	expect_true("bad_root_channel_binding",
		    cli_decode_stack_plan_build(&shape, channels, &plans,
						&error_text) != 0,
		    "plan build should fail");
	expect_text("bad_root_channel_binding",
		    "decode stack #1 (uart:rx=CH9:baudrate=9600): root decoder channel binding does not match the input logic channels",
		    error_text);
	expect_true("bad_root_channel_binding", plans == NULL,
		    "failed planning should not publish plan array");

	g_free(error_text);
	clear_shape_storage(&shape);
	free_logic_channels(channels);
}

int main(void)
{
	test_single_uart_plan();
	test_stacked_uart_midi_plan();
	test_bad_root_channel_binding();
	printf("decode_stack_plan_test: PASS\n");
	return 0;
}
