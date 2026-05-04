#ifndef DSVIEW_CLI_DEVICE_CONFIG_H
#define DSVIEW_CLI_DEVICE_CONFIG_H

#include <stdint.h>
#include <glib.h>

struct cli_selected_device;

struct cli_device_capture_config_request {
	uint64_t samplerate;
	uint64_t requested_sample_limit;
	uint64_t time_msec;
	uint64_t capture_ratio;
};

struct cli_device_capture_config_state {
	uint64_t hardware_limit_samples;
	uint64_t runtime_limit_samples;
};

int cli_device_config_read_value(int key, GVariant **value);
int cli_device_config_read_u64(int key, uint64_t *value_out);
int cli_device_config_set_u64(int key, uint64_t value);
int cli_device_config_read_named_text(const char *name, char **text_out);
GPtrArray *cli_device_config_current_values_new(void);
int cli_device_config_apply_args(const struct cli_selected_device *device,
				 char **config_args,
				 GPtrArray **applied_values_out,
				 const char **error_text_out);
int cli_device_config_read_requested_values(char **get_args,
					    GPtrArray **values_out,
					    const char **error_text_out);
int cli_device_config_prepare_live_capture(
	const struct cli_device_capture_config_request *request,
	struct cli_device_capture_config_state *state_out,
	const char **error_text_out);

#endif
