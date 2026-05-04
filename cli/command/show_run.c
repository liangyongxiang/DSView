#include <stdio.h>

#include "command_internal.h"
#include "command_result.h"
#include "device_config.h"
#include "device_selected.h"

int show_run(const struct cli_option_state *opts)
{
	struct cli_selected_device device;
	GPtrArray *configs;
	int n_ch;
	int rc = 1;

	cli_device_selected_init(&device);
	if (cli_device_selected_open_live(&device, opts->drv, NULL) != 0) {
		cli_support_command_result_write_error_json(opts->json_file, "show",
					 "failed to open selected device");
		return 1;
	}

	n_ch = device.info.di ? (int)g_slist_length(device.info.di->channels) : 0;

	printf("Driver: %s\n", device.info.driver_name);
	printf("Device: %s\n", device.info.name);
	printf("Index: %d\n", device.dev_index);
	printf("Mode: %s\n", cli_device_selected_mode_name(device.mode));
	printf("Channels: %d\n", n_ch);
	printf("Current stage-1 config values:\n");

	configs = cli_device_config_current_values_new();
	for (guint i = 0; i < configs->len; i++) {
		const struct cli_named_value *entry = g_ptr_array_index(configs, i);

		printf("  %s = %s\n", entry->name, entry->value);
	}

	cli_support_command_result_write_show_json(opts->json_file, device.dev_index,
			       (uint64_t)device.info.handle,
			       device.info.name, device.info.driver_name,
			       device.info.driver_name,
			       cli_device_selected_mode_name(device.mode),
			       n_ch, configs);
	g_ptr_array_free(configs, TRUE);
	rc = 0;
	cli_device_selected_close(&device);
	return rc;
}
