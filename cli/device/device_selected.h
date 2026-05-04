#ifndef DSVIEW_CLI_DEVICE_SELECTED_H
#define DSVIEW_CLI_DEVICE_SELECTED_H

#include <glib.h>

#include "libsigrok4DSL/libsigrok.h"
#include "libsigrok4DSL/libsigrok-internal.h"

struct cli_selected_device {
	int dev_index;
	int mode;
	gboolean owns_library;
	ds_device_handle virtual_handle;
	struct ds_device_full_info info;
};

int cli_device_selected_ensure_sigrok(void);
const char *cli_device_selected_mode_name(int mode);

void cli_device_selected_init(struct cli_selected_device *device);
void cli_device_selected_close(struct cli_selected_device *device);

int cli_device_selected_query_info_silent(struct ds_device_base_info *list,
					  int count, int dev_index,
					  struct ds_device_full_info *info);
int cli_device_selected_open_live(struct cli_selected_device *device,
				  const char *driver_spec,
				  const char **error_text_out);
int cli_device_selected_open_input(struct cli_selected_device *device,
				   const char *path,
				   const char **error_text_out);

gboolean cli_device_selected_input_is_dsl(const char *path);
gboolean cli_device_selected_input_is_session(const char *path);

#endif
