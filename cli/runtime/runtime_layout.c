/*
 * runtime_layout.c - resolve executable-relative CLI runtime paths.
 */

#include "runtime_layout.h"

#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <glib/gstdio.h>
#ifndef _WIN32
#include <unistd.h>
#include <sys/types.h>
#endif

static int get_executable_dir(char *exe_dir, size_t exe_sz)
{
#ifdef _WIN32
	DWORD len = GetModuleFileNameA(NULL, exe_dir, (DWORD)exe_sz);
	char *slash, *backslash;

	if (len == 0 || len >= exe_sz)
		return -1;
#else
	ssize_t len = readlink("/proc/self/exe", exe_dir, exe_sz - 1);
	char *slash;

	if (len <= 0 || len >= (ssize_t)exe_sz)
		return -1;
	exe_dir[len] = '\0';
#endif

	slash = strrchr(exe_dir, '/');
#ifdef _WIN32
	backslash = strrchr(exe_dir, '\\');
	if (!slash || (backslash && backslash > slash))
		slash = backslash;
#endif
	if (!slash)
		return -1;

	*slash = '\0';
	return 0;
}

static void set_default_firmware_resource_dir(char *fw_dir, size_t fw_sz)
{
#ifdef _WIN32
	const char *program_files = g_getenv("ProgramFiles");

	if (program_files && *program_files) {
		snprintf(fw_dir, fw_sz, "%s/DSView/res", program_files);
		return;
	}
#endif

	snprintf(fw_dir, fw_sz, "/usr/share/DSView/res");
}

static void resolve_firmware_resource_dir(struct cli_runtime_layout *layout)
{
	if (layout->has_executable_dir) {
		snprintf(layout->firmware_resource_dir,
			 sizeof(layout->firmware_resource_dir),
			 "%s/res", layout->executable_dir);
		if (!g_file_test(layout->firmware_resource_dir, G_FILE_TEST_IS_DIR))
			snprintf(layout->firmware_resource_dir,
				 sizeof(layout->firmware_resource_dir),
				 "%s/../DSView/res", layout->executable_dir);
		if (!g_file_test(layout->firmware_resource_dir, G_FILE_TEST_IS_DIR))
			snprintf(layout->firmware_resource_dir,
				 sizeof(layout->firmware_resource_dir),
				 "%s/../../DSView/res", layout->executable_dir);
	} else {
		set_default_firmware_resource_dir(layout->firmware_resource_dir,
						  sizeof(layout->firmware_resource_dir));
	}

	if (!g_file_test(layout->firmware_resource_dir, G_FILE_TEST_IS_DIR))
		set_default_firmware_resource_dir(layout->firmware_resource_dir,
						  sizeof(layout->firmware_resource_dir));
}

static void resolve_user_data_dir(struct cli_runtime_layout *layout)
{
	const char *config_dir = g_get_user_config_dir();

	if (!config_dir || !*config_dir)
		config_dir = ".";
	snprintf(layout->user_data_dir, sizeof(layout->user_data_dir),
		 "%s/dsview-cli", config_dir);
	g_mkdir_with_parents(layout->user_data_dir, 0755);
}

static void resolve_decoder_script_dir(struct cli_runtime_layout *layout)
{
	layout->decoder_script_dir[0] = '\0';

	if (!layout->has_executable_dir)
		return;

	snprintf(layout->decoder_script_dir,
		 sizeof(layout->decoder_script_dir),
		 "%s/decoders", layout->executable_dir);
	if (!g_file_test(layout->decoder_script_dir, G_FILE_TEST_IS_DIR))
		snprintf(layout->decoder_script_dir,
			 sizeof(layout->decoder_script_dir),
			 "%s/../libsigrokdecode4DSL/decoders",
			 layout->executable_dir);
	if (!g_file_test(layout->decoder_script_dir, G_FILE_TEST_IS_DIR))
		snprintf(layout->decoder_script_dir,
			 sizeof(layout->decoder_script_dir),
			 "%s/../share/libsigrokdecode4DSL/decoders",
			 layout->executable_dir);
	if (!g_file_test(layout->decoder_script_dir, G_FILE_TEST_IS_DIR))
		snprintf(layout->decoder_script_dir,
			 sizeof(layout->decoder_script_dir),
			 "%s/../../share/libsigrokdecode4DSL/decoders",
			 layout->executable_dir);
	if (!g_file_test(layout->decoder_script_dir, G_FILE_TEST_IS_DIR))
		layout->decoder_script_dir[0] = '\0';
}

void cli_runtime_layout_resolve(struct cli_runtime_layout *layout)
{
	if (!layout)
		return;

	memset(layout, 0, sizeof(*layout));
	layout->has_executable_dir =
	    get_executable_dir(layout->executable_dir,
			       sizeof(layout->executable_dir)) == 0;
	resolve_firmware_resource_dir(layout);
	resolve_user_data_dir(layout);
	resolve_decoder_script_dir(layout);
#ifdef _WIN32
	if (layout->has_executable_dir) {
		snprintf(layout->python_home_dir,
			 sizeof(layout->python_home_dir),
			 "%s/Python", layout->executable_dir);
		if (!g_file_test(layout->python_home_dir, G_FILE_TEST_IS_DIR))
			layout->python_home_dir[0] = '\0';
	}
#endif
}
