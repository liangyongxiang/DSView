#ifndef DSVIEW_CLI_DECODE_EXPORT_H
#define DSVIEW_CLI_DECODE_EXPORT_H

#include <stdint.h>

#include <glib.h>

#include "stack_runtime.h"

void cli_decode_export_free_record(gpointer data);
int cli_decode_export_infer_output_format(const char *path, char **format_out);
const struct srd_decoder_annotation_row *cli_decode_export_select_default_row(
	const struct srd_decoder *dec);
gboolean cli_decode_export_row_contains_class(const struct srd_decoder_annotation_row *row,
			    int ann_class);
char *cli_decode_export_build_decoder_row_title(
	const struct srd_decoder *root_dec,
	const struct srd_decoder_annotation_row *export_row);
char *cli_decode_export_build_row_title(const struct decode_stack_runtime *stack);
char *cli_decode_export_build_annotation_text(
	const struct srd_proto_data_annotation *pda);
int cli_decode_export_write_table_for_stack(struct decode_stack_runtime *stack,
				 uint64_t samplerate);

#endif
