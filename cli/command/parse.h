#ifndef DSVIEW_CLI_COMMAND_PARSE_H
#define DSVIEW_CLI_COMMAND_PARSE_H

#include <stddef.h>

#include <glib.h>

GHashTable *cli_command_parse_generic_arg(const char *arg, gboolean sep_first,
			      const char *key_first);
void cli_command_parse_driver_spec(const char *driver_spec,
		       char *driver_name, size_t driver_name_sz,
		       char *conn, size_t conn_sz);

#endif
