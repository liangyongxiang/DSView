#include "device_selected.h"

#include "parse.h"
#include "runtime_layout.h"
#include "waveform_session.h"

#include <stdio.h>
#include <string.h>

static int ensure_sigrok_runtime(void)
{
	struct cli_runtime_layout layout;
	int ret;

	cli_runtime_layout_resolve(&layout);

	ds_set_firmware_resource_dir(layout.firmware_resource_dir);
	ds_set_user_data_dir(layout.user_data_dir);
	ds_set_event_callback(cli_waveform_session_event_callback);
	ds_set_datafeed_callback(cli_waveform_session_datafeed_callback);
	ds_log_level(0);

	ret = ds_lib_init();
	if (ret != SR_OK)
		fprintf(stderr, "ds_lib_init failed: %d\n", ret);
	return ret;
}

int cli_device_selected_ensure_sigrok(void)
{
	return ensure_sigrok_runtime();
}

static void cli_device_selected_reset(struct cli_selected_device *device)
{
	if (!device)
		return;

	device->dev_index = -1;
	device->mode = -1;
	device->owns_library = FALSE;
	device->virtual_handle = NULL_HANDLE;
	memset(&device->info, 0, sizeof(device->info));
}

void cli_device_selected_init(struct cli_selected_device *device)
{
	cli_device_selected_reset(device);
}

static const char *device_error_hint(int err_code)
{
	switch (err_code) {
	case SR_ERR_DEVICE_IS_EXCLUSIVE:
		return "device is in use by another application "
		    "(DSView GUI or another dsview-cli instance)";
	case SR_ERR_FIRMWARE_NOT_EXIST:
		return "firmware file not found";
	case SR_ERR_DEVICE_FIRMWARE_VERSION_LOW:
		return "device firmware version too low, update via DSView GUI";
	case SR_ERR_DEVICE_USB_IO_ERROR:
		return "USB I/O error";
	case SR_ERR_DEVICE_NO_DRIVER:
		return "no driver for this device";
	default:
		return NULL;
	}
}

static int activate_device(int dev_index, struct ds_device_base_info *list, int count)
{
	ds_device_handle requested_handle;
	int ret;

	if (dev_index < 0 || dev_index >= count) {
		fprintf(stderr, "device index %d out of range (0-%d)\n",
			dev_index, count - 1);
		return -1;
	}

	requested_handle = list[dev_index].handle;
	ret = ds_active_device_by_index(dev_index);
	if (ret != SR_OK) {
		int last_err = ds_get_last_error();
		const char *hint = device_error_hint(last_err);

		if (hint)
			fprintf(stderr, "failed to activate device %d: %s\n",
				dev_index, hint);
		else
			fprintf(stderr, "failed to activate device %d (code %d)\n",
				dev_index, last_err);
		return -1;
	}

	{
		struct ds_device_full_info activated;

		memset(&activated, 0, sizeof(activated));
		ds_get_actived_device_info(&activated);
		if (activated.handle != requested_handle) {
			fprintf(stderr,
				"device %d (\"%s\") is in use by another application "
				"(DSView GUI or another dsview-cli instance); "
				"library fell back to \"%s\"\n",
				dev_index, list[dev_index].name, activated.name);
			return -2;
		}
	}

	return 0;
}

static int wait_for_device_init(void)
{
	int init_status = 0;

	for (int i = 0; i < 100; i++) {
		ds_get_actived_device_init_status(&init_status);
		if (init_status)
			return 0;
		g_usleep(100000);
	}

	return -1;
}

static void abandon_library_for_process_exit(struct cli_selected_device *device)
{
	if (!device || !device->owns_library)
		return;

	/*
	 * dsview-cli is a one-shot process.  The full DSView library shutdown
	 * waits for global hotplug/device teardown and can block after a live
	 * DSLogic acquisition has already stopped and produced the requested
	 * output.  Let process teardown reclaim the library globals so command
	 * completion is not delayed by GUI-oriented background cleanup.
	 */
	device->owns_library = FALSE;
}

int cli_device_selected_query_info_silent(struct ds_device_base_info *list,
					  int count, int dev_index,
					  struct ds_device_full_info *info)
{
	if (dev_index < 0 || dev_index >= count)
		return -1;
	if (ds_active_device_by_index(dev_index) != SR_OK)
		return -1;
	memset(info, 0, sizeof(*info));
	ds_get_actived_device_info(info);
	if (info->handle != list[dev_index].handle)
		return -2;
	return 0;
}

static int resolve_device_index(struct ds_device_base_info *list, int count,
				const char *driver_spec)
{
	char wanted_driver[64];
	char conn[64];

	if (count <= 0)
		return -1;

	cli_command_parse_driver_spec(driver_spec, wanted_driver, sizeof(wanted_driver),
			  conn, sizeof(conn));
	if (!wanted_driver[0])
		return 0;

	for (int i = 0; i < count; i++) {
		struct ds_device_full_info info;

		if (cli_device_selected_query_info_silent(list, count, i, &info) != 0)
			continue;
		if (g_ascii_strcasecmp(info.driver_name, wanted_driver) == 0)
			return i;
	}

	return -1;
}

void cli_device_selected_close(struct cli_selected_device *device)
{
	if (!device)
		return;

	if (device->virtual_handle != NULL_HANDLE)
		ds_remove_device(device->virtual_handle);
	abandon_library_for_process_exit(device);

	cli_device_selected_reset(device);
}

int cli_device_selected_open_live(struct cli_selected_device *device,
				  const char *driver_spec,
				  const char **error_text_out)
{
	struct ds_device_base_info *list = NULL;
	int count = 0;
	int dev_index;
	int ret;

	if (error_text_out)
		*error_text_out = NULL;
	if (!device) {
		if (error_text_out)
			*error_text_out = "missing selected device request";
		return -1;
	}

	cli_device_selected_init(device);
	if (cli_device_selected_ensure_sigrok() != SR_OK) {
		if (error_text_out)
			*error_text_out = "ds_lib_init failed";
		return -1;
	}
	device->owns_library = TRUE;

	ds_reload_device_list();
	g_usleep(800000);
	ds_get_device_list(&list, &count);

	if (count == 0) {
		fprintf(stderr, "no devices found\n");
		g_free(list);
		if (error_text_out)
			*error_text_out = "failed to open selected device";
		cli_device_selected_close(device);
		return -1;
	}

	dev_index = resolve_device_index(list, count, driver_spec);
	if (dev_index < 0) {
		fprintf(stderr, "no device matches driver spec: %s\n",
			driver_spec ? driver_spec : "");
		g_free(list);
		if (error_text_out)
			*error_text_out = "failed to open selected device";
		cli_device_selected_close(device);
		return -1;
	}

	ret = activate_device(dev_index, list, count);
	g_free(list);
	if (ret != 0) {
		if (error_text_out)
			*error_text_out = "failed to open selected device";
		cli_device_selected_close(device);
		return -1;
	}

	if (wait_for_device_init() != 0) {
		fprintf(stderr, "device init timed out\n");
		if (error_text_out)
			*error_text_out = "failed to open selected device";
		cli_device_selected_close(device);
		return -1;
	}

	ds_get_actived_device_info(&device->info);
	device->dev_index = dev_index;
	device->mode = ds_get_actived_device_mode();
	return 0;
}

gboolean cli_device_selected_input_is_dsl(const char *path)
{
	const char *dot;

	if (!path || !*path)
		return FALSE;
	dot = strrchr(path, '.');
	return dot && g_ascii_strcasecmp(dot, ".dsl") == 0;
}

gboolean cli_device_selected_input_is_session(const char *path)
{
	const char *dot;

	if (!path || !*path)
		return FALSE;
	dot = strrchr(path, '.');
	if (!dot)
		return FALSE;
	return g_ascii_strcasecmp(dot, ".dsl") == 0 ||
	    g_ascii_strcasecmp(dot, ".sr") == 0;
}

int cli_device_selected_open_input(struct cli_selected_device *device,
				   const char *path,
				   const char **error_text_out)
{
	struct ds_device_base_info *list = NULL;
	int count = 0;
	int ret;

	if (error_text_out)
		*error_text_out = NULL;
	if (!device || !path || !*path) {
		if (error_text_out)
			*error_text_out = "missing input source request";
		return -1;
	}

	cli_device_selected_init(device);
	if (cli_device_selected_ensure_sigrok() != SR_OK) {
		if (error_text_out)
			*error_text_out = "ds_lib_init failed";
		return -1;
	}
	device->owns_library = TRUE;

	ret = ds_device_from_file(path);
	if (ret != SR_OK) {
		fprintf(stderr, "failed to create virtual device from file\n");
		if (error_text_out)
			*error_text_out = "failed to load input session file";
		cli_device_selected_close(device);
		return -1;
	}

	if (ds_get_device_list(&list, &count) != SR_OK || count <= 0 || !list) {
		fprintf(stderr, "failed to query virtual device list\n");
		g_free(list);
		if (error_text_out)
			*error_text_out = "failed to load input session file";
		cli_device_selected_close(device);
		return -1;
	}

	device->virtual_handle = list[count - 1].handle;
	g_free(list);

	if (ds_active_device(device->virtual_handle) != SR_OK) {
		fprintf(stderr, "failed to activate virtual device\n");
		if (error_text_out)
			*error_text_out = "failed to load input session file";
		cli_device_selected_close(device);
		return -1;
	}

	ds_get_actived_device_info(&device->info);
	device->mode = ds_get_actived_device_mode();
	return 0;
}

const char *cli_device_selected_mode_name(int mode)
{
	if (mode == DSO)
		return "DSO";
	if (mode == ANALOG)
		return "ANALOG";
	return "LOGIC";
}
