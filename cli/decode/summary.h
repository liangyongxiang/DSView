#ifndef DSVIEW_CLI_DECODE_SUMMARY_H
#define DSVIEW_CLI_DECODE_SUMMARY_H

#include <stdint.h>

#include <glib.h>

int cli_decode_summary_write_json(const char *json_path,
			      gboolean source_is_live,
			      const char *source_label,
			      GPtrArray *stacks,
			      guint64 total_rows,
			      guint64 total_annotations,
			      gboolean has_decode_window,
			      uint64_t decode_start_sample,
			      uint64_t decode_end_sample,
			      uint64_t samplerate);

#endif
