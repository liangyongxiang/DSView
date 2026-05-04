#include <stdio.h>

#include "command_internal.h"
#include "parse.h"
#include "command_result.h"
#include "device_config.h"
#include "device_selected.h"

int scan_run(const struct cli_option_state *opts)
{
	struct ds_device_base_info *list = NULL;
	int count = 0;
	char wanted_driver[64];
	char conn[64];
	GPtrArray *entries;
	gboolean first = TRUE;

	cli_command_parse_driver_spec(opts->drv, wanted_driver,
				      sizeof(wanted_driver),
				      conn, sizeof(conn));

	if (cli_device_selected_ensure_sigrok() != SR_OK) {
		cli_support_command_result_write_error_json(opts->json_file, "scan",
					 "ds_lib_init failed");
		return 1;
	}

	ds_reload_device_list();
	g_usleep(800000);
	ds_get_device_list(&list, &count);

	entries = cli_support_command_result_scan_entries_new();
	for (int i = 0; i < count; i++) {
		struct ds_device_full_info info;
		char *driver = NULL;

		if (cli_device_selected_query_info_silent(list, count, i, &info) == 0)
			driver = info.driver_name;

		if (wanted_driver[0]) {
			if (!driver || g_ascii_strcasecmp(driver, wanted_driver) != 0)
				continue;
		}

		first = FALSE;
		cli_support_command_result_scan_entries_add(entries, i,
						(uint64_t)list[i].handle,
						list[i].name,
						driver,
						driver);
		if (driver) {
			printf("%d: %s [%s]\n", i, list[i].name, driver);
		} else {
			printf("%d: %s [driver unresolved]\n", i, list[i].name);
		}
	}

	if (first)
		printf("No devices found.\n");

	cli_support_command_result_write_scan_json(opts->json_file, entries);
	g_ptr_array_free(entries, TRUE);
	g_free(list);
	ds_lib_exit();
	return 0;
}
