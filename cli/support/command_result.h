#ifndef DSVIEW_CLI_COMMAND_RESULT_H
#define DSVIEW_CLI_COMMAND_RESULT_H

#include <glib.h>
#include <stdint.h>

struct cli_named_value {
	char *name;
	char *value;
};

struct cli_scan_result_entry {
	int index;
	uint64_t handle;
	char *name;
	char *driver;
	char *driver_spec;
};

struct cli_capture_result {
	const char *mode;
	const char *output_format;
	uint64_t samples;
	uint64_t samplerate;
	int unitsize;
	const char *file;
	const char *meta;
};

struct cli_export_result {
	const char *mode;
	const char *input_file;
	const char *output_format;
	uint64_t samples;
	uint64_t samplerate;
	int unitsize;
	const char *file;
};

GPtrArray *cli_support_command_result_named_values_new(void);
void cli_support_command_result_named_values_add(GPtrArray *values,
					  const char *name,
					  const char *value);

GPtrArray *cli_support_command_result_scan_entries_new(void);
void cli_support_command_result_scan_entries_add(GPtrArray *entries,
					  int index,
					  uint64_t handle,
					  const char *name,
					  const char *driver,
					  const char *driver_spec);

int cli_support_command_result_write_error_json(const char *path, const char *command,
			     const char *error_text);
int cli_support_command_result_write_scan_json(const char *path, GPtrArray *entries);
int cli_support_command_result_write_show_json(const char *path, int dev_index, uint64_t handle,
			   const char *name, const char *driver,
			   const char *driver_spec, const char *mode,
			   int channels, GPtrArray *configs);
int cli_support_command_result_write_option_values_json(const char *path, const char *command,
				    int dev_index, const char *driver,
				    GPtrArray *values);
int cli_support_command_result_write_capture_json(const char *path,
			      const struct cli_capture_result *result);
int cli_support_command_result_write_export_json(const char *path,
			     const struct cli_export_result *result);

#endif
