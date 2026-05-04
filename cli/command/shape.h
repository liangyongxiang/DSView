#ifndef DSVIEW_CLI_SHAPE_H
#define DSVIEW_CLI_SHAPE_H

#include <glib.h>
#include <stdint.h>

#include "option_state.h"

enum cli_command_kind {
	CLI_COMMAND_NONE = 0,
	CLI_COMMAND_SCAN,
	CLI_COMMAND_SHOW,
	CLI_COMMAND_GET,
	CLI_COMMAND_SET,
	CLI_COMMAND_LIVE_CAPTURE,
	CLI_COMMAND_OFFLINE_EXPORT,
	CLI_COMMAND_LIVE_DECODE,
	CLI_COMMAND_OFFLINE_DECODE,
};

struct cli_command_shape {
	const struct cli_option_state *opts;
	enum cli_command_kind kind;
	const char *command_name;
	const char *driver_spec;
	const char *input_file;
	const char *output_file;
	const char *output_format_id;
	const char *meta_file;
	const char *json_file;
	const char *channels;
	const char *trig_pos_arg;
	gchar **gets;
	gchar **configs;
	gchar **pd_stacks;
	gchar **decode_outputs;
	guint get_count;
	guint config_count;
	guint pd_stack_count;
	guint decode_output_count;
	gboolean use_dsl_output;
	gboolean has_samples;
	uint64_t sample_limit;
	gboolean has_time;
	uint64_t time_msec;
	char *error_text;
};

void cli_command_shape_init(struct cli_command_shape *shape);
void cli_command_shape_clear(struct cli_command_shape *shape);
int cli_command_shape_build(struct cli_command_shape *shape,
			    const struct cli_option_state *opts);
const char *cli_command_shape_error_text(const struct cli_command_shape *shape);

#endif
