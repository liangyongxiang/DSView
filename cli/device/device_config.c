#include "device_config.h"

#include <stdio.h>
#include <string.h>

#include "parse.h"
#include "command_result.h"
#include "device_selected.h"

struct cli_config_spec {
	const char *name;
	int key;
	int datatype;
};

static const struct cli_config_spec cli_config_specs[] = {
	{ "samplerate", SR_CONF_SAMPLERATE, SR_T_UINT64 },
	{ "limit_samples", SR_CONF_LIMIT_SAMPLES, SR_T_UINT64 },
	{ "capture_ratio", SR_CONF_CAPTURE_RATIO, SR_T_UINT64 },
	{ "horiz_triggerpos", SR_CONF_HORIZ_TRIGGERPOS, SR_T_UINT8 },
	{ "timebase", SR_CONF_TIMEBASE, SR_T_UINT64 },
	{ "threshold", SR_CONF_THRESHOLD, SR_T_LIST },
	{ "vth", SR_CONF_VTH, SR_T_FLOAT },
	{ NULL, 0, 0 }
};

static const struct cli_config_spec *find_cli_config_spec(const char *name)
{
	for (int i = 0; cli_config_specs[i].name; i++) {
		if (g_ascii_strcasecmp(cli_config_specs[i].name, name) == 0)
			return &cli_config_specs[i];
	}
	return NULL;
}

int cli_device_config_read_value(int key, GVariant **value)
{
	*value = NULL;
	return ds_get_actived_device_config(NULL, NULL, key, value);
}

int cli_device_config_read_u64(int key, uint64_t *value_out)
{
	GVariant *value = NULL;

	if (!value_out)
		return -1;
	if (cli_device_config_read_value(key, &value) != SR_OK || !value)
		return -1;

	*value_out = g_variant_get_uint64(value);
	g_variant_unref(value);
	return 0;
}

int cli_device_config_set_u64(int key, uint64_t value)
{
	return ds_set_actived_device_config(NULL, NULL, key,
					    g_variant_new_uint64(value)) == SR_OK ?
	    0 : -1;
}

static char *config_value_to_text(const struct cli_config_spec *spec,
				  GVariant *value)
{
	GVariant *listv = NULL;
	struct sr_list_item *items;
	int code;

	if (!spec || !value)
		return g_strdup("");

	switch (spec->datatype) {
	case SR_T_UINT64:
		return g_strdup_printf("%llu",
				       (unsigned long long)g_variant_get_uint64(value));
	case SR_T_UINT8:
		return g_strdup_printf("%u", (unsigned)g_variant_get_byte(value));
	case SR_T_FLOAT:
		return g_strdup_printf("%.6g", g_variant_get_double(value));
	case SR_T_LIST:
		code = g_variant_get_int16(value);
		if (ds_get_actived_device_config_list(NULL, spec->key, &listv) == SR_OK &&
		    listv) {
			items = (struct sr_list_item *)(uintptr_t)
				g_variant_get_uint64(listv);
			for (int i = 0; items[i].id >= 0; i++) {
				if (items[i].id == code) {
					char *s = g_strdup(items[i].name);
					g_variant_unref(listv);
					return s;
				}
			}
			g_variant_unref(listv);
		}
		return g_strdup_printf("%d", code);
	default:
		return g_variant_print(value, FALSE);
	}
}

static int config_text_to_variant(const struct cli_config_spec *spec,
				  int dev_mode, const char *text,
				  GVariant **out)
{
	uint64_t u64 = 0;
	double f64;
	char *end = NULL;
	int code;

	*out = NULL;
	if (!spec)
		return -1;

	switch (spec->datatype) {
	case SR_T_UINT64:
		if (sr_parse_sizestring(text, &u64) != SR_OK)
			return -1;
		*out = g_variant_new_uint64(u64);
		return 0;
	case SR_T_UINT8:
		if (sr_parse_sizestring(text, &u64) != SR_OK || u64 > 255)
			return -1;
		*out = g_variant_new_byte((guint8)u64);
		return 0;
	case SR_T_FLOAT:
		f64 = g_ascii_strtod(text, &end);
		if (end == text || *end != '\0')
			return -1;
		*out = g_variant_new_double(f64);
		return 0;
	case SR_T_LIST:
		code = ds_dsl_option_value_to_code(dev_mode, spec->key, text);
		if (code < 0)
			return -1;
		*out = g_variant_new_int16((gint16)code);
		return 0;
	default:
		return -1;
	}
}

typedef int (*cli_config_arg_visitor)(
	const struct cli_config_spec *spec, const char *name,
	const char *value, void *context, const char **error_text_out);

struct cli_apply_config_context {
	int dev_mode;
	GPtrArray *applied_values;
};

struct cli_read_config_context {
	GPtrArray *values;
};

static int visit_config_args(char **argsv, gboolean require_value,
			     const char *invalid_arg_error,
			     cli_config_arg_visitor visitor, void *context,
			     const char **error_text_out)
{
	for (guint i = 0; argsv && argsv[i]; i++) {
		const char *opt_text = argsv[i];
		GHashTable *args =
		    cli_command_parse_generic_arg(opt_text, FALSE, "channel_group");
		GHashTableIter iter;
		gpointer key, value;

		if (!args) {
			if (error_text_out)
				*error_text_out = invalid_arg_error;
			return -1;
		}
		if (g_hash_table_lookup(args, "sigrok_key")) {
			fprintf(stderr, "--channel-group is not supported yet\n");
			g_hash_table_destroy(args);
			if (error_text_out)
				*error_text_out = "--channel-group is not supported yet";
			return -1;
		}

		g_hash_table_iter_init(&iter, args);
		while (g_hash_table_iter_next(&iter, &key, &value)) {
			const char *name = (const char *)key;
			const char *text = (const char *)value;
			const struct cli_config_spec *spec = find_cli_config_spec(name);

			if (!spec) {
				fprintf(stderr, "unknown device option: %s\n", name);
				g_hash_table_destroy(args);
				if (error_text_out)
					*error_text_out = "unknown device option";
				return -1;
			}
			if (require_value && (!text || !*text)) {
				fprintf(stderr, "option needs a value: %s\n", name);
				g_hash_table_destroy(args);
				if (error_text_out)
					*error_text_out = "option needs a value";
				return -1;
			}
			if (visitor(spec, name, text, context, error_text_out) != 0) {
				g_hash_table_destroy(args);
				return -1;
			}
		}

		g_hash_table_destroy(args);
	}

	return 0;
}

static int apply_config_arg(const struct cli_config_spec *spec,
			    const char *name, const char *value,
			    void *context, const char **error_text_out)
{
	struct cli_apply_config_context *apply_context =
	    (struct cli_apply_config_context *)context;
	GVariant *gvar = NULL;

	if (config_text_to_variant(spec, apply_context ? apply_context->dev_mode : LOGIC,
				   value, &gvar) != 0) {
		fprintf(stderr, "invalid value for %s: %s\n",
			name, value ? value : "");
		if (error_text_out)
			*error_text_out = "invalid device option value";
		return -1;
	}
	if (ds_set_actived_device_config(NULL, NULL, spec->key, gvar) != SR_OK) {
		fprintf(stderr, "failed to set %s\n", name);
		if (error_text_out)
			*error_text_out = "failed to apply --config values";
		return -1;
	}
	if (apply_context && apply_context->applied_values) {
		cli_support_command_result_named_values_add(
		    apply_context->applied_values, name, value);
	}
	return 0;
}

static int read_requested_config_arg(const struct cli_config_spec *spec,
				     const char *name, const char *value,
				     void *context, const char **error_text_out)
{
	struct cli_read_config_context *read_context =
	    (struct cli_read_config_context *)context;
	GVariant *current_value = NULL;
	char *text;

	(void)value;
	(void)error_text_out;

	if (cli_device_config_read_value(spec->key, &current_value) != SR_OK ||
	    !current_value) {
		fprintf(stderr, "failed to get %s\n", name);
		return 0;
	}

	text = config_value_to_text(spec, current_value);
	printf("%s: %s\n", name, text);
	cli_support_command_result_named_values_add(read_context->values, name,
						 text);
	g_free(text);
	g_variant_unref(current_value);
	return 0;
}

static uint64_t compute_guard_limit_samples(uint64_t samplerate,
					    uint64_t time_msec)
{
	uint64_t expected_samples;
	uint64_t seconds;
	uint64_t rem_msec;
	uint64_t rem_samples;

	if (!samplerate || !time_msec)
		return 1;

	seconds = time_msec / 1000ULL;
	rem_msec = time_msec % 1000ULL;

	if (seconds > 0 && samplerate > UINT64_MAX / seconds)
		expected_samples = UINT64_MAX;
	else
		expected_samples = samplerate * seconds;

	rem_samples = (samplerate / 1000ULL) * rem_msec;
	rem_samples += ((samplerate % 1000ULL) * rem_msec) / 1000ULL;

	if (expected_samples > UINT64_MAX - rem_samples)
		expected_samples = UINT64_MAX;
	else
		expected_samples += rem_samples;

	if (!expected_samples)
		expected_samples = 1;
	if (expected_samples > 4294967295ULL / 8ULL)
		return 4294967295ULL;
	return expected_samples * 8ULL;
}

int cli_device_config_read_named_text(const char *name, char **text_out)
{
	const struct cli_config_spec *spec;
	GVariant *value = NULL;

	if (!text_out)
		return -1;
	*text_out = NULL;

	spec = find_cli_config_spec(name);
	if (!spec)
		return -1;
	if (cli_device_config_read_value(spec->key, &value) != SR_OK || !value)
		return -2;

	*text_out = config_value_to_text(spec, value);
	g_variant_unref(value);
	return 0;
}

GPtrArray *cli_device_config_current_values_new(void)
{
	GPtrArray *values = cli_support_command_result_named_values_new();

	for (int i = 0; cli_config_specs[i].name; i++) {
		GVariant *value = NULL;
		char *text;

		if (cli_device_config_read_value(cli_config_specs[i].key, &value) !=
		    SR_OK || !value)
			continue;

		text = config_value_to_text(&cli_config_specs[i], value);
		cli_support_command_result_named_values_add(values,
						cli_config_specs[i].name,
						text);
		g_free(text);
		g_variant_unref(value);
	}

	return values;
}

int cli_device_config_apply_args(const struct cli_selected_device *device,
				 char **config_args,
				 GPtrArray **applied_values_out,
				 const char **error_text_out)
{
	struct cli_apply_config_context apply_context;
	GPtrArray *applied_values = NULL;
	int rc;

	if (error_text_out)
		*error_text_out = NULL;
	if (applied_values_out)
		*applied_values_out = NULL;
	if (!device) {
		if (error_text_out)
			*error_text_out = "missing device configuration target";
		return -1;
	}

	if (applied_values_out)
		applied_values = cli_support_command_result_named_values_new();
	apply_context.dev_mode = device->mode;
	apply_context.applied_values = applied_values;

	rc = visit_config_args(config_args, TRUE, "invalid --config argument",
			       apply_config_arg, &apply_context,
			       error_text_out);
	if (rc != 0) {
		if (applied_values)
			g_ptr_array_free(applied_values, TRUE);
		return -1;
	}

	if (applied_values_out)
		*applied_values_out = applied_values;
	return 0;
}

int cli_device_config_read_requested_values(char **get_args,
					    GPtrArray **values_out,
					    const char **error_text_out)
{
	struct cli_read_config_context read_context;
	GPtrArray *values;

	if (error_text_out)
		*error_text_out = NULL;
	if (!values_out) {
		if (error_text_out)
			*error_text_out = "missing device configuration result";
		return -1;
	}
	*values_out = NULL;

	values = cli_support_command_result_named_values_new();
	read_context.values = values;
	if (visit_config_args(get_args, FALSE, "invalid --get argument",
			      read_requested_config_arg, &read_context,
			      error_text_out) != 0) {
		g_ptr_array_free(values, TRUE);
		return -1;
	}

	*values_out = values;
	return 0;
}

int cli_device_config_prepare_live_capture(
	const struct cli_device_capture_config_request *request,
	struct cli_device_capture_config_state *state_out,
	const char **error_text_out)
{
	uint64_t configured_limit_samples = 0;

	if (error_text_out)
		*error_text_out = NULL;
	if (!request || !state_out) {
		if (error_text_out)
			*error_text_out = "missing live capture config request";
		return -1;
	}

	state_out->hardware_limit_samples = 0;
	state_out->runtime_limit_samples = 0;

	if (request->requested_sample_limit > 0) {
		state_out->hardware_limit_samples = request->requested_sample_limit;
		state_out->runtime_limit_samples = request->requested_sample_limit;
	} else if (request->time_msec > 0) {
		state_out->hardware_limit_samples =
		    compute_guard_limit_samples(request->samplerate,
						request->time_msec);
	} else if (cli_device_config_read_u64(SR_CONF_LIMIT_SAMPLES,
					     &configured_limit_samples) == 0) {
		state_out->hardware_limit_samples = configured_limit_samples;
		state_out->runtime_limit_samples = configured_limit_samples;
	}

	if (state_out->hardware_limit_samples == 0)
		state_out->hardware_limit_samples = 1000000ULL;
	if (request->requested_sample_limit == 0 && request->time_msec == 0)
		state_out->runtime_limit_samples =
		    state_out->hardware_limit_samples;

	if (cli_device_config_set_u64(SR_CONF_LIMIT_SAMPLES,
				      state_out->hardware_limit_samples) != 0) {
		if (error_text_out)
			*error_text_out = "failed to set limit_samples";
		return -1;
	}
	(void)cli_device_config_set_u64(SR_CONF_CAPTURE_RATIO,
					request->capture_ratio);

	return 0;
}
