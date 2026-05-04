/*
 * command_result_json.c - command-result JSON serialization.
 */

#include "json.h"
#include "command_result.h"

static void free_named_value(gpointer data)
{
	struct cli_named_value *entry = (struct cli_named_value *)data;

	if (!entry)
		return;

	g_free(entry->name);
	g_free(entry->value);
	g_free(entry);
}

static void free_scan_result_entry(gpointer data)
{
	struct cli_scan_result_entry *entry =
	    (struct cli_scan_result_entry *)data;

	if (!entry)
		return;

	g_free(entry->name);
	g_free(entry->driver);
	g_free(entry->driver_spec);
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

	entry = g_malloc0(sizeof(*entry));
	entry->name = g_strdup(name ? name : "");
	entry->value = g_strdup(value ? value : "");
	g_ptr_array_add(values, entry);
}

GPtrArray *cli_support_command_result_scan_entries_new(void)
{
	return g_ptr_array_new_with_free_func(free_scan_result_entry);
}

void cli_support_command_result_scan_entries_add(GPtrArray *entries,
					  int index,
					  uint64_t handle,
					  const char *name,
					  const char *driver,
					  const char *driver_spec)
{
	struct cli_scan_result_entry *entry;

	if (!entries)
		return;

	entry = g_malloc0(sizeof(*entry));
	entry->index = index;
	entry->handle = handle;
	entry->name = g_strdup(name ? name : "");
	entry->driver = g_strdup(driver);
	entry->driver_spec = g_strdup(driver_spec);
	g_ptr_array_add(entries, entry);
}

static struct cli_support_json_value *build_named_values_object(GPtrArray *values)
{
	struct cli_support_json_value *object = cli_support_json_new_object();

	if (!object)
		return NULL;

	for (guint i = 0; values && i < values->len; i++) {
		const struct cli_named_value *entry =
		    (const struct cli_named_value *)g_ptr_array_index(values, i);

		cli_support_json_object_set_string(object,
						   entry ? entry->name : "",
						   entry ? entry->value : "");
	}

	return object;
}

static struct cli_support_json_value *build_scan_result_value(GPtrArray *entries)
{
	struct cli_support_json_value *result = cli_support_json_new_array();

	if (!result)
		return NULL;

	for (guint i = 0; entries && i < entries->len; i++) {
		const struct cli_scan_result_entry *entry =
		    (const struct cli_scan_result_entry *)g_ptr_array_index(entries, i);
		struct cli_support_json_value *item = cli_support_json_new_object();

		if (!item) {
			cli_support_json_value_free(result);
			return NULL;
		}

		cli_support_json_object_set_int(item, "index",
						entry ? entry->index : 0);
		cli_support_json_object_set_uint64(item, "handle",
						  entry ? entry->handle : 0U);
		cli_support_json_object_set_string(item, "name",
						  entry ? entry->name : "");
		if (entry && entry->driver)
			cli_support_json_object_set_string(item, "driver",
							   entry->driver);
		if (entry && entry->driver_spec)
			cli_support_json_object_set_string(item, "driver_spec",
							   entry->driver_spec);
		cli_support_json_array_append(result, item);
	}

	return result;
}

static struct cli_support_json_value *build_show_result_value(
	int dev_index, uint64_t handle, const char *name, const char *driver,
	const char *driver_spec, const char *mode, int channels,
	GPtrArray *configs)
{
	struct cli_support_json_value *result = cli_support_json_new_object();

	if (!result)
		return NULL;

	cli_support_json_object_set_int(result, "index", dev_index);
	cli_support_json_object_set_uint64(result, "handle", handle);
	cli_support_json_object_set_string(result, "name", name);
	cli_support_json_object_set_string(result, "driver", driver);
	cli_support_json_object_set_string(result, "driver_spec", driver_spec);
	cli_support_json_object_set_string(result, "mode", mode);
	cli_support_json_object_set_int(result, "channels", channels);
	cli_support_json_object_set(result, "configs",
				    build_named_values_object(configs));
	return result;
}

static struct cli_support_json_value *build_option_values_result_value(
	int dev_index, const char *driver, GPtrArray *values)
{
	struct cli_support_json_value *result = cli_support_json_new_object();

	if (!result)
		return NULL;

	cli_support_json_object_set_int(result, "index", dev_index);
	cli_support_json_object_set_string(result, "driver", driver);
	cli_support_json_object_set(result, "values",
				    build_named_values_object(values));
	return result;
}

static struct cli_support_json_value *build_capture_result_value(
	const struct cli_capture_result *result)
{
	struct cli_support_json_value *json = cli_support_json_new_object();

	if (!json)
		return NULL;

	cli_support_json_object_set_string(json, "mode",
					   result ? result->mode : "");
	cli_support_json_object_set_string(json, "output_format",
					   result ? result->output_format : "");
	cli_support_json_object_set_uint64(json, "samples",
					   result ? result->samples : 0U);
	cli_support_json_object_set_uint64(json, "samplerate",
					   result ? result->samplerate : 0U);
	cli_support_json_object_set_int(json, "unitsize",
					result ? result->unitsize : 0);
	cli_support_json_object_set_string(json, "file",
					   result ? result->file : "");
	if (result && result->meta && *result->meta)
		cli_support_json_object_set_string(json, "meta", result->meta);
	return json;
}

static struct cli_support_json_value *build_export_result_value(
	const struct cli_export_result *result)
{
	struct cli_support_json_value *json = cli_support_json_new_object();

	if (!json)
		return NULL;

	cli_support_json_object_set_string(json, "mode",
					   result ? result->mode : "");
	cli_support_json_object_set_string(json, "input_file",
					   result ? result->input_file : "");
	cli_support_json_object_set_string(json, "output_format",
					   result ? result->output_format : "");
	cli_support_json_object_set_uint64(json, "samples",
					   result ? result->samples : 0U);
	cli_support_json_object_set_uint64(json, "samplerate",
					   result ? result->samplerate : 0U);
	cli_support_json_object_set_int(json, "unitsize",
					result ? result->unitsize : 0);
	cli_support_json_object_set_string(json, "file",
					   result ? result->file : "");
	return json;
}

int cli_support_command_result_write_error_json(const char *path, const char *command,
			     const char *error_text)
{
	return cli_support_json_write_envelope(path, command, FALSE, NULL,
					       error_text);
}

int cli_support_command_result_write_scan_json(const char *path, GPtrArray *entries)
{
	struct cli_support_json_value *result;
	int rc;

	result = build_scan_result_value(entries);
	if (!result)
		return -1;

	rc = cli_support_json_write_envelope(path, "scan", TRUE, result, NULL);
	cli_support_json_value_free(result);
	return rc;
}

int cli_support_command_result_write_show_json(const char *path, int dev_index, uint64_t handle,
			   const char *name, const char *driver,
			   const char *driver_spec, const char *mode,
			   int channels, GPtrArray *configs)
{
	struct cli_support_json_value *result;
	int rc;

	result = build_show_result_value(dev_index, handle, name, driver,
					 driver_spec, mode, channels, configs);
	if (!result)
		return -1;

	rc = cli_support_json_write_envelope(path, "show", TRUE, result, NULL);
	cli_support_json_value_free(result);
	return rc;
}

int cli_support_command_result_write_option_values_json(const char *path, const char *command,
				    int dev_index, const char *driver,
				    GPtrArray *values)
{
	struct cli_support_json_value *result;
	int rc;

	result = build_option_values_result_value(dev_index, driver, values);
	if (!result)
		return -1;

	rc = cli_support_json_write_envelope(path, command, TRUE, result, NULL);
	cli_support_json_value_free(result);
	return rc;
}

int cli_support_command_result_write_capture_json(const char *path,
			      const struct cli_capture_result *result)
{
	struct cli_support_json_value *json;
	int rc;

	json = build_capture_result_value(result);
	if (!json)
		return -1;

	rc = cli_support_json_write_envelope(path, "capture", TRUE, json, NULL);
	cli_support_json_value_free(json);
	return rc;
}

int cli_support_command_result_write_export_json(const char *path,
			     const struct cli_export_result *result)
{
	struct cli_support_json_value *json;
	int rc;

	json = build_export_result_value(result);
	if (!json)
		return -1;

	rc = cli_support_json_write_envelope(path, "export", TRUE, json, NULL);
	cli_support_json_value_free(json);
	return rc;
}
