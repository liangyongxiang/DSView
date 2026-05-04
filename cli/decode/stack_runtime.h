#ifndef DSVIEW_CLI_DECODE_STACK_RUNTIME_H
#define DSVIEW_CLI_DECODE_STACK_RUNTIME_H

#include <stdint.h>

#include <glib.h>

#include "libsigrokdecode4DSL/libsigrokdecode.h"

struct decode_runtime;

struct decode_record {
	uint64_t start_sample;
	uint64_t seq;
	char *text;
};

struct decode_stack_runtime {
	struct decode_runtime *runtime;
	guint index;
	char *stack_spec;
	char *output_path;
	char *output_format_name;
	struct srd_session *session;
	struct srd_decoder_inst *logic_di;
	struct srd_decoder *root_dec;
	const struct srd_decoder_annotation_row *export_row;
	GPtrArray *records;
	guint64 rows_written;
	guint64 annotations_emitted;
	guint64 next_seq;
	char *row_title;
	gboolean success;
	char *error_text;
};

void cli_decode_stack_runtime_free(gpointer data);

#endif
