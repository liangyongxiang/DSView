#ifndef DSVIEW_CLI_OPTION_STATE_H
#define DSVIEW_CLI_OPTION_STATE_H

#include <glib.h>

struct cli_option_state {
	gboolean scan_devs;
	gboolean show;
	gboolean set;
	gboolean help;
	gchar *drv;
	gchar *input_file;
	gchar **configs;
	gchar **pd_stacks;
	gchar *channels;
	gchar *output_file;
	gchar *output_format;
	gchar *samples;
	gchar *time;
	gchar **gets;
	gchar **decode_outputs;
	gchar *meta_file;
	gchar *json_file;
	gchar *trig_pos_arg;
	const char *progname;
};

void cli_command_option_state_init(struct cli_option_state *state);
void cli_command_option_state_clear(struct cli_option_state *state);

#endif
