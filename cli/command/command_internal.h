#ifndef DSVIEW_CLI_COMMAND_INTERNAL_H
#define DSVIEW_CLI_COMMAND_INTERNAL_H

#include "shape.h"
#include "option_state.h"

int cli_command_options_parse(struct cli_option_state *opts, int argc, char **argv);
void cli_command_options_free(struct cli_option_state *opts);
void cli_command_options_show_help(const struct cli_option_state *opts);

int scan_run(const struct cli_option_state *opts);
int show_run(const struct cli_option_state *opts);
int get_run(const struct cli_option_state *opts);
int set_run(const struct cli_option_state *opts);
int capture_run(const struct cli_command_shape *shape);
int export_run(const struct cli_command_shape *shape);
int decode_run(const struct cli_command_shape *shape);

#endif
