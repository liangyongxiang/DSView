#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "command_result.h"
#include "device_config.h"
#include "device_selected.h"

static GVariant *config_store[3];

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

static int slot_for_key(int key)
{
	switch (key) {
	case SR_CONF_SAMPLERATE:
		return 0;
	case SR_CONF_LIMIT_SAMPLES:
		return 1;
	case SR_CONF_CAPTURE_RATIO:
		return 2;
	default:
		return -1;
	}
}

static void reset_config_store(void)
{
	for (guint i = 0; i < G_N_ELEMENTS(config_store); i++) {
		if (config_store[i]) {
			g_variant_unref(config_store[i]);
			config_store[i] = NULL;
		}
	}
}

static void store_u64_config(int key, uint64_t value)
{
	int slot = slot_for_key(key);

	if (slot < 0)
		return;
	if (config_store[slot])
		g_variant_unref(config_store[slot]);
	config_store[slot] = g_variant_ref_sink(g_variant_new_uint64(value));
}

static uint64_t read_u64_config(int key)
{
	int slot = slot_for_key(key);

	if (slot < 0 || !config_store[slot])
		return 0;
	return g_variant_get_uint64(config_store[slot]);
}

static void free_named_value(gpointer data)
{
	struct cli_named_value *entry = (struct cli_named_value *)data;

	if (!entry)
		return;
	g_free(entry->name);
	g_free(entry->value);
	g_free(entry);
}

GPtrArray *cli_support_command_result_named_values_new(void)
{
	return g_ptr_array_new_with_free_func(free_named_value);
}

void cli_support_command_result_named_values_add(GPtrArray *values,
					  const char *name,
					  const char *value)
{
	struct cli_named_value *entry;

	if (!values)
		return;
	entry = g_new0(struct cli_named_value, 1);
	entry->name = g_strdup(name ? name : "");
	entry->value = g_strdup(value ? value : "");
	g_ptr_array_add(values, entry);
}

SR_API int ds_get_actived_device_config(const struct sr_channel *ch,
					const struct sr_channel_group *cg,
					int key, GVariant **data)
{
	int slot = slot_for_key(key);

	(void)ch;
	(void)cg;
	if (data)
		*data = NULL;
	if (slot < 0 || !config_store[slot])
		return SR_ERR;
	if (data)
		*data = g_variant_ref(config_store[slot]);
	return SR_OK;
}

SR_API int ds_set_actived_device_config(const struct sr_channel *ch,
					const struct sr_channel_group *cg,
					int key, GVariant *data)
{
	int slot = slot_for_key(key);

	(void)ch;
	(void)cg;
	if (slot < 0 || !data)
		return SR_ERR;
	if (config_store[slot])
		g_variant_unref(config_store[slot]);
	config_store[slot] = g_variant_ref_sink(data);
	return SR_OK;
}

SR_API int ds_get_actived_device_config_list(const struct sr_channel_group *cg,
					     int key, GVariant **data)
{
	(void)cg;
	(void)key;
	if (data)
		*data = NULL;
	return SR_ERR;
}

SR_API int ds_dsl_option_value_to_code(int mode, int key, const char *value)
{
	(void)mode;
	(void)key;
	(void)value;
	return -1;
}

static const struct cli_named_value *find_named_value(GPtrArray *values,
						      const char *name)
{
	for (guint i = 0; values && i < values->len; i++) {
		const struct cli_named_value *entry =
		    (const struct cli_named_value *)g_ptr_array_index(values, i);

		if (entry && g_strcmp0(entry->name, name) == 0)
			return entry;
	}
	return NULL;
}

static void test_current_values_and_read_request(void)
{
	GPtrArray *current_values;
	GPtrArray *requested_values = NULL;
	char *get_args[] = { "samplerate:limit_samples", NULL };
	const struct cli_named_value *entry;
	const char *error_text = NULL;

	reset_config_store();
	store_u64_config(SR_CONF_SAMPLERATE, 10000000ULL);
	store_u64_config(SR_CONF_LIMIT_SAMPLES, 4096ULL);
	store_u64_config(SR_CONF_CAPTURE_RATIO, 35ULL);

	current_values = cli_device_config_current_values_new();
	entry = find_named_value(current_values, "samplerate");
	expect_true("current_values", entry != NULL,
		    "samplerate should be present in current values");
	expect_text("current_values", "10000000", entry->value);
	entry = find_named_value(current_values, "limit_samples");
	expect_text("current_values", "4096", entry ? entry->value : NULL);
	g_ptr_array_free(current_values, TRUE);

	expect_true("read_requested_values",
		    cli_device_config_read_requested_values(get_args,
							   &requested_values,
							   &error_text) == 0,
		    error_text ? error_text :
		    "requested value read should succeed");
	entry = find_named_value(requested_values, "samplerate");
	expect_text("read_requested_values", "10000000",
		    entry ? entry->value : NULL);
	entry = find_named_value(requested_values, "limit_samples");
	expect_text("read_requested_values", "4096",
		    entry ? entry->value : NULL);
	g_ptr_array_free(requested_values, TRUE);
}

static void test_apply_args_collects_applied_values(void)
{
	struct cli_selected_device device;
	GPtrArray *applied_values = NULL;
	char *config_args[] = { "samplerate=10M:limit_samples=5K",
				"capture_ratio=40", NULL };
	const struct cli_named_value *entry;
	const char *error_text = NULL;

	reset_config_store();
	memset(&device, 0, sizeof(device));
	device.mode = LOGIC;

	expect_true("apply_args",
		    cli_device_config_apply_args(&device, config_args,
						 &applied_values,
						 &error_text) == 0,
		    error_text ? error_text : "config apply should succeed");
	expect_true("apply_args",
		    read_u64_config(SR_CONF_SAMPLERATE) == 10000000ULL,
		    "samplerate should be parsed and stored");
	expect_true("apply_args",
		    read_u64_config(SR_CONF_LIMIT_SAMPLES) == 5000ULL,
		    "limit_samples should be parsed and stored");
	expect_true("apply_args",
		    read_u64_config(SR_CONF_CAPTURE_RATIO) == 40ULL,
		    "capture_ratio should be parsed and stored");
	entry = find_named_value(applied_values, "samplerate");
	expect_text("apply_args", "10M", entry ? entry->value : NULL);
	entry = find_named_value(applied_values, "limit_samples");
	expect_text("apply_args", "5K", entry ? entry->value : NULL);
	entry = find_named_value(applied_values, "capture_ratio");
	expect_text("apply_args", "40", entry ? entry->value : NULL);
	g_ptr_array_free(applied_values, TRUE);
}

static void test_prepare_live_capture_time_guard(void)
{
	struct cli_device_capture_config_request request;
	struct cli_device_capture_config_state state;
	const char *error_text = NULL;

	reset_config_store();
	memset(&request, 0, sizeof(request));
	memset(&state, 0, sizeof(state));
	request.samplerate = 1000000ULL;
	request.time_msec = 2000ULL;
	request.capture_ratio = 25ULL;

	expect_true("prepare_live_capture_time_guard",
		    cli_device_config_prepare_live_capture(&request, &state,
							   &error_text) == 0,
		    error_text ? error_text :
		    "time-based capture config should succeed");
	expect_true("prepare_live_capture_time_guard",
		    state.hardware_limit_samples == 16000000ULL,
		    "guard limit mismatch");
	expect_true("prepare_live_capture_time_guard",
		    state.runtime_limit_samples == 0ULL,
		    "runtime limit should stay zero for time-based capture");
	expect_true("prepare_live_capture_time_guard",
		    read_u64_config(SR_CONF_LIMIT_SAMPLES) == 16000000ULL,
		    "stored hardware limit mismatch");
	expect_true("prepare_live_capture_time_guard",
		    read_u64_config(SR_CONF_CAPTURE_RATIO) == 25ULL,
		    "stored capture ratio mismatch");
}

static void test_prepare_live_capture_existing_limit(void)
{
	struct cli_device_capture_config_request request;
	struct cli_device_capture_config_state state;
	const char *error_text = NULL;

	reset_config_store();
	store_u64_config(SR_CONF_LIMIT_SAMPLES, 2048ULL);
	memset(&request, 0, sizeof(request));
	memset(&state, 0, sizeof(state));
	request.samplerate = 1000000ULL;
	request.capture_ratio = 60ULL;

	expect_true("prepare_live_capture_existing_limit",
		    cli_device_config_prepare_live_capture(&request, &state,
							   &error_text) == 0,
		    error_text ? error_text :
		    "existing limit capture config should succeed");
	expect_true("prepare_live_capture_existing_limit",
		    state.hardware_limit_samples == 2048ULL &&
		    state.runtime_limit_samples == 2048ULL,
		    "existing limit should be reused");
	expect_true("prepare_live_capture_existing_limit",
		    read_u64_config(SR_CONF_CAPTURE_RATIO) == 60ULL,
		    "capture ratio should be updated");
}

int main(void)
{
	test_current_values_and_read_request();
	test_apply_args_collects_applied_values();
	test_prepare_live_capture_time_guard();
	test_prepare_live_capture_existing_limit();
	reset_config_store();
	printf("device_config_test: PASS\n");
	return 0;
}
