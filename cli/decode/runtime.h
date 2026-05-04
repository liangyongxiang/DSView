#ifndef DSVIEW_CLI_DECODE_RUNTIME_H
#define DSVIEW_CLI_DECODE_RUNTIME_H

#include <stdint.h>

#include <glib.h>

#include "libsigrok4DSL/libsigrok.h"

struct cli_command_shape;
struct decode_runtime;

struct decode_runtime *cli_decode_runtime_create(void);
void cli_decode_runtime_destroy(struct decode_runtime *runtime);
int cli_decode_runtime_run_offline_command(
	const struct cli_command_shape *shape);
int cli_decode_runtime_prepare_live(struct decode_runtime *runtime,
				    const struct cli_command_shape *shape,
				    GSList *channels, uint64_t samplerate,
				    const char *source_label);
int cli_decode_runtime_run_offline(struct decode_runtime *runtime,
				   const struct cli_command_shape *shape,
				   GSList *channels, uint64_t samplerate,
				   const char *source_label);
int cli_decode_runtime_finalize_live(struct decode_runtime *runtime);
const char *cli_decode_runtime_error_text(const struct decode_runtime *runtime);
void cli_decode_runtime_on_event(struct decode_runtime *runtime, int event);
void cli_decode_runtime_on_datafeed(struct decode_runtime *runtime,
			      const struct sr_dev_inst *sdi,
			      const struct sr_datafeed_packet *packet);

#endif
