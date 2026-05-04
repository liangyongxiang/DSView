/*
 * set_run.c - device option set command adapter for dsview-cli.
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

int set_run(const struct cli_option_state *opts)
{
	struct cli_selected_device device;
	GPtrArray *values;
	const char *config_error = NULL;
	int rc = 1;

	if (strv_length(opts->configs) == 0) {
		fprintf(stderr, "no --config values specified\n");
		cli_support_command_result_write_error_json(opts->json_file, "set",
					 "no --config values specified");
		return 1;
	}

	cli_device_selected_init(&device);
	if (cli_device_selected_open_live(&device, opts->drv, NULL) != 0) {
		cli_support_command_result_write_error_json(opts->json_file, "set",
					 "failed to open selected device");
		return 1;
	}
	if (cli_device_config_apply_args(&device, opts->configs, &values,
					 &config_error) != 0) {
		cli_support_command_result_write_error_json(opts->json_file, "set",
					 config_error ? config_error :
					 "failed to apply --config values");
		goto done;
	}

	printf("Applied configuration values on %s.\n", device.info.name);

	cli_support_command_result_write_option_values_json(opts->json_file, "set",
					device.dev_index, device.info.driver_name,
					values);
	g_ptr_array_free(values, TRUE);
	rc = 0;

done:
	cli_device_selected_close(&device);
	return rc;
}
