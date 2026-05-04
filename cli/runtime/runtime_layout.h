#ifndef DSVIEW_CLI_RUNTIME_LAYOUT_H
#define DSVIEW_CLI_RUNTIME_LAYOUT_H

#include <glib.h>

struct cli_runtime_layout {
	char executable_dir[512];
	char firmware_resource_dir[512];
	char user_data_dir[512];
	char decoder_script_dir[512];
#ifdef _WIN32
	char python_home_dir[512];
#endif
	gboolean has_executable_dir;
};

void cli_runtime_layout_resolve(struct cli_runtime_layout *layout);

#endif
