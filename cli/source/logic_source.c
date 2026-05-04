#include "logic_source.h"

#include <stdlib.h>
#include <string.h>

#include "channel_selection_state.h"
#include "shape.h"
#include "device_config.h"

static int read_source_samplerate(uint64_t *samplerate_out, gboolean required)
{
	if (!samplerate_out)
		return -1;

	*samplerate_out = 0;
	if (cli_device_config_read_u64(SR_CONF_SAMPLERATE, samplerate_out) != 0) {
		if (required)
			return -1;
		return 0;
	}
	return 0;
}

static int select_live_channel_mode(
	const struct channel_selection_state *channel_state)
{
	GVariant *cm_data = NULL;
	int best_nch = 0;

	if (!channel_state)
		return 0;
	if (ds_get_actived_device_config_list(NULL, SR_CONF_CHANNEL_MODE,
					      &cm_data) != SR_OK ||
	    !cm_data)
		return 0;

	{
		struct sr_list_item *modes =
		    (struct sr_list_item *)(uintptr_t)g_variant_get_uint64(cm_data);
		int max_ch_idx = 0;
		int min_hw_chs;
		int best_id = -1;

		for (int i = 0; i < cli_source_channel_selection_enabled_count(channel_state); i++) {
			int phys = cli_source_channel_selection_enabled_phys(channel_state, i);

			if (phys > max_ch_idx)
				max_ch_idx = phys;
		}
		min_hw_chs = max_ch_idx + 1;
		if (min_hw_chs < cli_source_channel_selection_enabled_count(channel_state))
			min_hw_chs = cli_source_channel_selection_enabled_count(channel_state);

		for (int i = 0; modes[i].id >= 0; i++) {
			int nch = 0;
			const char *p = modes[i].name;

			if (p) {
				const char *u = strstr(p, "se ");
				if (u)
					nch = atoi(u + 3);
				else {
					const char *t = strstr(p, "0~");

					if (t)
						nch = atoi(t + 2) + 1;
				}
			}
			if (nch >= min_hw_chs && (!best_nch || nch < best_nch)) {
				best_nch = nch;
				best_id = modes[i].id;
			}
		}

		if (best_id >= 0) {
			GVariant *value = g_variant_new_int16((int16_t)best_id);

			if (ds_set_actived_device_config(NULL, NULL,
			    SR_CONF_CHANNEL_MODE, value) != SR_OK)
				best_nch = 0;
		}
	}

	g_variant_unref(cm_data);
	return best_nch;
}

void cli_source_logic_init(struct cli_logic_source *source)
{
	if (!source)
		return;

	memset(source, 0, sizeof(*source));
	cli_device_selected_init(&source->device);
	source->source_label = "logic";
}

void cli_source_logic_close(struct cli_logic_source *source)
{
	if (!source)
		return;

	cli_device_selected_close(&source->device);
}

int cli_source_logic_open_live(struct cli_logic_source *source,
			       const struct cli_command_shape *shape,
			       struct channel_selection_state *channel_state,
			       const char **error_text_out)
{
	int hw_nch;

	if (error_text_out)
		*error_text_out = NULL;
	if (!source || !shape || !channel_state) {
		if (error_text_out)
			*error_text_out = "missing logic source request";
		return -1;
	}

	cli_source_logic_init(source);
	if (cli_device_selected_open_live(&source->device, shape->driver_spec,
					  error_text_out) != 0) {
		if (error_text_out && !*error_text_out)
			*error_text_out = "failed to open selected device";
		return -1;
	}
	if (source->device.mode != LOGIC) {
		if (error_text_out) {
			*error_text_out = shape->kind == CLI_COMMAND_LIVE_DECODE ?
			    "stage-1 live protocol decode currently supports logic devices only" :
			    "stage-1 capture currently supports logic devices only";
		}
		goto fail;
	}
	if (cli_device_config_apply_args(&source->device, shape->configs, NULL,
					 error_text_out) != 0) {
		if (error_text_out && !*error_text_out)
			*error_text_out = "failed to apply --config values";
		goto fail;
	}
	if (read_source_samplerate(&source->samplerate, FALSE) != 0) {
		if (error_text_out)
			*error_text_out = "failed to read samplerate";
		goto fail;
	}
	if (!source->samplerate)
		source->samplerate = 1000000ULL;

	hw_nch = select_live_channel_mode(channel_state);
	source->channels = source->device.info.di ?
	    source->device.info.di->channels : NULL;
	if (cli_source_channel_selection_prepare_live_logic(
		channel_state, hw_nch,
		source->device.info.di ?
		(int)g_slist_length(source->device.info.di->channels) : MAX_CH,
		error_text_out) != 0) {
		if (error_text_out && !*error_text_out)
			*error_text_out = "failed to prepare live channel selection";
		goto fail;
	}

	source->hw_nch = cli_source_channel_selection_hw_nch(channel_state);
	source->unitsize = cli_source_channel_selection_logic_unitsize(channel_state);
	source->source_label = *source->device.info.name ? source->device.info.name :
	    (shape->driver_spec && *shape->driver_spec ? shape->driver_spec : "live");
	return 0;

fail:
	cli_source_logic_close(source);
	return -1;
}

int cli_source_logic_open_input(struct cli_logic_source *source,
				const struct cli_command_shape *shape,
				struct channel_selection_state *channel_state,
				gboolean dsl_only,
				const char **error_text_out)
{
	if (error_text_out)
		*error_text_out = NULL;
	if (!source || !shape || !shape->input_file) {
		if (error_text_out)
			*error_text_out = "missing input source request";
		return -1;
	}

	if (dsl_only) {
		if (!cli_device_selected_input_is_dsl(shape->input_file)) {
			if (error_text_out)
				*error_text_out =
				    "stage-1 decode input must be a local .dsl file";
			return -1;
		}
	} else if (!cli_device_selected_input_is_session(shape->input_file)) {
		if (error_text_out)
			*error_text_out =
			    "stage-1 offline export input must be a local .dsl or .sr file";
		return -1;
	}

	if (!g_file_test(shape->input_file, G_FILE_TEST_IS_REGULAR)) {
		if (error_text_out)
			*error_text_out = dsl_only ?
			    "decode input file not found" :
			    "offline export input file not found";
		return -1;
	}

	cli_source_logic_init(source);
	if (cli_device_selected_open_input(&source->device, shape->input_file,
					   error_text_out) != 0) {
		if (error_text_out && !*error_text_out)
			*error_text_out = "failed to load input session file";
		return -1;
	}
	if (source->device.mode != LOGIC) {
		if (error_text_out) {
			*error_text_out = dsl_only ?
			    "stage-1 decode supports logic .dsl files only" :
			    "stage-1 offline export currently supports logic session files only";
		}
		goto fail;
	}
	if (!source->device.info.di || !source->device.info.di->channels) {
		if (error_text_out)
			*error_text_out = "input file has no logic channels";
		goto fail;
	}
	if (read_source_samplerate(&source->samplerate, TRUE) != 0) {
		if (error_text_out)
			*error_text_out = "failed to read samplerate from input file";
		goto fail;
	}
	if (!source->samplerate) {
		if (error_text_out)
			*error_text_out = "input file has invalid samplerate";
		goto fail;
	}

	source->channels = source->device.info.di->channels;
	source->source_label = shape->input_file;
	if (!channel_state)
		return 0;

	if (cli_source_channel_selection_prepare_input_logic(
		channel_state, source->channels,
		!shape->channels || !*shape->channels, error_text_out) != 0) {
		if (error_text_out && !*error_text_out)
			*error_text_out = "failed to prepare input channel selection";
		goto fail;
	}

	source->hw_nch = cli_source_channel_selection_hw_nch(channel_state);
	source->unitsize = cli_source_channel_selection_logic_unitsize(channel_state);
	return 0;

fail:
	cli_source_logic_close(source);
	return -1;
}
