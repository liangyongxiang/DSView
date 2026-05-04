/*
 * get_run.c - device option get command adapter for dsview-cli.
 */

#include <stdio.h>

#include "command_internal.h"
#include "command_result.h"
#include "device_config.h"
#include "device_selected.h"

static guint strv_length(char **items)
{
	guint count = 0;

	while (items && items[count])
		count++;
	return count;
}

int get_run(const struct cli_option_state *opts)
{
	struct cli_selected_device device;
	GPtrArray *values;
	guint get_count = strv_length(opts->gets);
	const char *config_error = NULL;
	int rc = 1;

	if (get_count == 0) {
		fprintf(stderr, "no --get options specified\n");
		cli_support_command_result_write_error_json(opts->json_file, "get",
					 "no --get options specified");
		return 1;
	}

	cli_device_selected_init(&device);
	if (cli_device_selected_open_live(&device, opts->drv, NULL) != 0) {
		cli_support_command_result_write_error_json(opts->json_file, "get",
					 "failed to open selected device");
		return 1;
	}
	if (cli_device_config_apply_args(&device, opts->configs, NULL,
					 &config_error) != 0) {
		cli_support_command_result_write_error_json(opts->json_file, "get",
					 config_error ? config_error :
					 "failed to apply --config values");
		goto done;
	}
	if (cli_device_config_read_requested_values(opts->gets, &values,
						    &config_error) != 0) {
		cli_support_command_result_write_error_json(opts->json_file, "get",
					 config_error ? config_error :
					 "failed to get device option");
		goto done;
	}

	cli_support_command_result_write_option_values_json(opts->json_file, "get",
					device.dev_index, device.info.driver_name,
					values);
	g_ptr_array_free(values, TRUE);
	rc = 0;

done:
	cli_device_selected_close(&device);
	return rc;
}
