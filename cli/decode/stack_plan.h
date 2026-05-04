#ifndef DSVIEW_CLI_DECODE_STACK_PLAN_H
#define DSVIEW_CLI_DECODE_STACK_PLAN_H

#include <glib.h>

#include "shape.h"
#include "libsigrok4DSL/libsigrok.h"
#include "libsigrokdecode4DSL/libsigrokdecode.h"

struct decode_decoder_step_plan {
	char *decoder_id;
	GHashTable *options;
	GHashTable *channel_indices;
};

struct decode_stack_plan {
	guint index;
	char *stack_spec;
	char *output_path;
	char *output_format_name;
	struct srd_decoder *root_decoder;
	const struct srd_decoder_annotation_row *export_row;
	char *row_title;
	GPtrArray *decoder_steps;
};

void cli_decode_stack_plan_free_decoder_step(gpointer data);
void cli_decode_stack_plan_free(gpointer data);
int cli_decode_stack_plan_build(const struct cli_command_shape *shape,
				GSList *channels,
				GPtrArray **plans_out,
				char **error_text_out);

#endif
