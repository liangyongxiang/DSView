#ifndef DSVIEW_CLI_DECODE_RUNTIME_INTERNAL_H
#define DSVIEW_CLI_DECODE_RUNTIME_INTERNAL_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#include <glib.h>

#include "shape.h"
#include "stack_runtime.h"
#include "libsigrok4DSL/libsigrok.h"
#include "libsigrokdecode4DSL/libsigrokdecode.h"

struct decode_stack_plan;

struct decode_runtime {
	const struct cli_command_shape *shape;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	volatile int done;
	volatile int error;
	char *error_text;
	guint64 rows_written;
	guint64 annotations_emitted;
	uint64_t samplerate;
	uint64_t samples_sent;
	uint64_t decode_start_sample;
	uint64_t decode_end_sample;
	gboolean has_decode_window;
	GPtrArray *stack_plans;
	GPtrArray *stacks;
	int *channel_order_by_index;
	size_t channel_order_len;
	guint signal_count;
	guint cross_group_bytes;
	uint8_t *cross_leftover;
	size_t cross_leftover_len;
	gboolean runtime_ready;
	gboolean source_is_live;
	char *source_label;
};

void cli_decode_runtime_init(struct decode_runtime *runtime);
void cli_decode_runtime_clear_error(struct decode_runtime *runtime);
void cli_decode_runtime_set_error(struct decode_runtime *runtime, const char *fmt, ...);
void cli_decode_stack_runtime_set_error(struct decode_stack_runtime *stack,
			  const char *fmt, ...);
void cli_decode_runtime_mark_done(struct decode_runtime *runtime, gboolean failed,
		      const char *fmt, ...);
void cli_decode_runtime_reset_state_data(struct decode_runtime *runtime);
void cli_decode_runtime_reset_objects(struct decode_runtime *runtime);
int cli_decode_runtime_begin_session(struct decode_runtime *runtime, GSList *channels,
			 uint64_t samplerate, gboolean source_is_live,
			 const char *source_label);
int cli_decode_runtime_finish_session(struct decode_runtime *runtime);
int cli_decode_runtime_prepare_stacks(struct decode_runtime *runtime);
int cli_decode_runtime_build_channel_order_map(struct decode_runtime *runtime, GSList *channels);
void cli_decode_runtime_annotation_callback(struct srd_proto_data *pdata, void *cb_data);

#endif
