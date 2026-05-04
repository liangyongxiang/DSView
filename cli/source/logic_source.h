#ifndef DSVIEW_CLI_LOGIC_SOURCE_H
#define DSVIEW_CLI_LOGIC_SOURCE_H

#include <stdint.h>
#include <glib.h>

#include "libsigrok4DSL/libsigrok.h"
#include "libsigrok4DSL/libsigrok-internal.h"
#include "device_selected.h"

struct cli_command_shape;
struct channel_selection_state;

struct cli_logic_source {
	struct cli_selected_device device;
	GSList *channels;
	uint64_t samplerate;
	int hw_nch;
	int unitsize;
	const char *source_label;
};

void cli_source_logic_init(struct cli_logic_source *source);
void cli_source_logic_close(struct cli_logic_source *source);

int cli_source_logic_open_live(struct cli_logic_source *source,
			       const struct cli_command_shape *shape,
			       struct channel_selection_state *channel_state,
			       const char **error_text_out);
int cli_source_logic_open_input(struct cli_logic_source *source,
				const struct cli_command_shape *shape,
				struct channel_selection_state *channel_state,
				gboolean dsl_only,
				const char **error_text_out);

#endif
