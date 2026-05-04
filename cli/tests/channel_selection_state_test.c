#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "channel_selection_state.h"
#include "libsigrok4DSL/libsigrok.h"

static int enabled_calls[32];
static gboolean enabled_values[32];
static char channel_names[32][64];
static int trigger_reset_calls;
static uint16_t trigger_enable_value;
static uint16_t trigger_stage_value;
static uint16_t trigger_pos_value;
static uint16_t trigger_probe_value;
static unsigned char trigger_probe0_value;
static unsigned char trigger_probe1_value;

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

static void reset_stubs(void)
{
	memset(enabled_calls, 0, sizeof(enabled_calls));
	memset(enabled_values, 0, sizeof(enabled_values));
	memset(channel_names, 0, sizeof(channel_names));
	trigger_reset_calls = 0;
	trigger_enable_value = 0;
	trigger_stage_value = 0;
	trigger_pos_value = 0;
	trigger_probe_value = 0;
	trigger_probe0_value = 0;
	trigger_probe1_value = 0;
}

int ds_enable_device_channel_index(int ch_index, gboolean enable)
{
	if (ch_index >= 0 && ch_index < 32) {
		enabled_calls[ch_index]++;
		enabled_values[ch_index] = enable;
	}
	return SR_OK;
}

int ds_set_device_channel_name(int ch_index, const char *name)
{
	if (ch_index >= 0 && ch_index < 32 && name) {
		snprintf(channel_names[ch_index], sizeof(channel_names[ch_index]),
			 "%s", name);
	}
	return SR_OK;
}

int ds_trigger_reset(void)
{
	trigger_reset_calls++;
	return SR_OK;
}

int ds_trigger_probe_set(uint16_t probe, unsigned char trigger0,
			 unsigned char trigger1)
{
	trigger_probe_value = probe;
	trigger_probe0_value = trigger0;
	trigger_probe1_value = trigger1;
	return SR_OK;
}

int ds_trigger_set_stage(uint16_t stages)
{
	trigger_stage_value = stages;
	return SR_OK;
}

int ds_trigger_set_pos(uint16_t position)
{
	trigger_pos_value = position;
	return SR_OK;
}

int ds_trigger_set_en(uint16_t enable)
{
	trigger_enable_value = enable;
	return SR_OK;
}

static void prepare_requested_logic_channels(struct channel_selection_state *state)
{
	cli_source_channel_selection_reset_defaults(state);
	cli_source_channel_selection_clear_request(state);
	if (cli_source_channel_selection_add_request(state, 2, "D2") != 0 ||
	    cli_source_channel_selection_add_request(state, 0, "D0") != 0) {
		fail_test("prepare_requested_logic_channels",
			  "failed to build channel request");
	}
}

static void test_prepare_live_logic(void)
{
	struct channel_selection_state state;
	const char *error_text = NULL;

	reset_stubs();
	prepare_requested_logic_channels(&state);
	expect_true("prepare_live_logic",
		    cli_source_channel_selection_prepare_live_logic(
			&state, 4, 6, &error_text) == 0,
		    error_text ? error_text : "live logic preparation should succeed");
	expect_true("prepare_live_logic",
		    cli_source_channel_selection_hw_nch(&state) == 4,
		    "hw_nch mismatch");
	expect_true("prepare_live_logic",
		    cli_source_channel_selection_logic_unitsize(&state) == 1,
		    "unitsize mismatch");
	expect_true("prepare_live_logic",
		    cli_source_channel_selection_output_bit_for_source_slot(&state, 2) == 0,
		    "channel 2 should map to output bit 0");
	expect_true("prepare_live_logic",
		    cli_source_channel_selection_output_bit_for_source_slot(&state, 0) == 1,
		    "channel 0 should map to output bit 1");
	expect_true("prepare_live_logic", enabled_calls[5] == 1,
		    "device layout should touch every visible device channel");
	expect_true("prepare_live_logic", enabled_values[0] == TRUE &&
		    enabled_values[3] == TRUE && enabled_values[4] == FALSE,
		    "device enable mask mismatch");
	expect_text("prepare_live_logic", "D0", channel_names[0]);
	expect_text("prepare_live_logic", "D2", channel_names[2]);
	expect_true("prepare_live_logic", trigger_reset_calls == 1,
		    "trigger reset should be issued");
	expect_true("prepare_live_logic", trigger_enable_value == 0,
		    "default trigger should stay disabled");
}

static void test_prepare_input_logic_defaults_from_source(void)
{
	struct channel_selection_state state;
	struct sr_channel ch2 = { .index = 2, .type = SR_CHANNEL_LOGIC };
	struct sr_channel ch0 = { .index = 0, .type = SR_CHANNEL_LOGIC };
	GSList *channels = NULL;
	const char *error_text = NULL;

	reset_stubs();
	cli_source_channel_selection_reset_defaults(&state);
	channels = g_slist_append(channels, &ch2);
	channels = g_slist_append(channels, &ch0);

	expect_true("prepare_input_logic_defaults",
		    cli_source_channel_selection_prepare_input_logic(
			&state, channels, TRUE, &error_text) == 0,
		    error_text ? error_text :
		    "input logic preparation should succeed");
	expect_true("prepare_input_logic_defaults",
		    cli_source_channel_selection_enabled_count(&state) == 2,
		    "default logic request should include both channels");
	expect_true("prepare_input_logic_defaults",
		    cli_source_channel_selection_enabled_phys(&state, 0) == 2 &&
		    cli_source_channel_selection_enabled_phys(&state, 1) == 0,
		    "default request order mismatch");
	expect_true("prepare_input_logic_defaults",
		    cli_source_channel_selection_output_bit_for_source_slot(&state, 0) == 0 &&
		    cli_source_channel_selection_output_bit_for_source_slot(&state, 1) == 1,
		    "input source order should map directly to output bits");
	expect_true("prepare_input_logic_defaults",
		    cli_source_channel_selection_hw_nch(&state) == 2,
		    "input hw_nch mismatch");

	g_slist_free(channels);
}

static void test_prepare_input_logic_missing_channel(void)
{
	struct channel_selection_state state;
	struct sr_channel ch2 = { .index = 2, .type = SR_CHANNEL_LOGIC };
	struct sr_channel ch0 = { .index = 0, .type = SR_CHANNEL_LOGIC };
	GSList *channels = NULL;
	const char *error_text = NULL;

	reset_stubs();
	cli_source_channel_selection_reset_defaults(&state);
	cli_source_channel_selection_clear_request(&state);
	if (cli_source_channel_selection_add_request(&state, 1, "D1") != 0)
		fail_test("prepare_input_logic_missing_channel",
			  "failed to set explicit channel request");
	channels = g_slist_append(channels, &ch2);
	channels = g_slist_append(channels, &ch0);

	expect_true("prepare_input_logic_missing_channel",
		    cli_source_channel_selection_prepare_input_logic(
			&state, channels, FALSE, &error_text) != 0,
		    "input logic preparation should fail");
	expect_text("prepare_input_logic_missing_channel",
		    "selected logic channel not found in input file", error_text);

	g_slist_free(channels);
}

int main(void)
{
	test_prepare_live_logic();
	test_prepare_input_logic_defaults_from_source();
	test_prepare_input_logic_missing_channel();
	printf("channel_selection_state_test: PASS\n");
	return 0;
}
