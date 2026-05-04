#include "channel_selection_state.h"
#include "json.h"
#include "dsl_layout.h"
#include "session_metadata.h"

#include "libsigrok4DSL/libsigrok.h"

char *cli_waveform_session_metadata_build_dsl_logic_header(
	const struct channel_selection_state *channel_state,
	uint64_t samplerate, uint64_t sample_count,
	uint64_t total_blocks)
{
	char *samplerate_s = sr_samplerate_string(samplerate);
	GString *header = g_string_new("[version]\n");
	int n_enabled = cli_source_channel_selection_enabled_count(channel_state);

	g_string_append_printf(header, "version = %d\n", DSL_HEADER_FORMAT_VERSION);
	g_string_append(header, "[header]\n");
	g_string_append(header, "driver = DSLogic\n");
	g_string_append_printf(header, "device mode = %d\n", LOGIC);
	g_string_append(header, "capturefile = data\n");
	g_string_append_printf(header, "total samples = %llu\n",
			       (unsigned long long)sample_count);
	g_string_append_printf(header, "total probes = %d\n", n_enabled);
	g_string_append_printf(header, "total blocks = %llu\n",
			       (unsigned long long)total_blocks);
	g_string_append_printf(header, "samplerate = %s\n",
			       samplerate_s ? samplerate_s : "0 Hz");
	g_string_append(header, "trigger time = 0\n");
	g_string_append_printf(header, "trigger pos = %llu\n",
			       (unsigned long long)
			       cli_source_channel_selection_logic_trigger_pos(channel_state,
							      sample_count));

	for (int i = 0; i < n_enabled; i++) {
		char default_name[32];
		const char *name =
		    cli_source_channel_selection_display_name(channel_state, i,
						      default_name,
						      sizeof(default_name));

		g_string_append_printf(header, "probe%d = %s\n",
				       cli_source_channel_selection_enabled_phys(channel_state, i),
				       name ? name : "");
	}

	g_free(samplerate_s);
	return g_string_free(header, FALSE);
}

static struct cli_support_json_value *build_capture_channel_value(
	int dev_mode, const struct channel_selection_state *channel_state,
	int enabled_index, gboolean normalize_phys)
{
	struct cli_support_json_value *channel = cli_support_json_new_object();
	char default_name[32];
	int phys = normalize_phys ? enabled_index :
		   cli_source_channel_selection_enabled_phys(channel_state,
							    enabled_index);
	const char *name = cli_source_channel_selection_display_name(channel_state,
							 enabled_index,
							 default_name,
							 sizeof(default_name));
	const char *channel_type = (dev_mode == DSO || dev_mode == ANALOG) ?
		"dso" : "logic";

	if (!channel)
		return NULL;

	cli_support_json_object_set_int(channel, "seq", enabled_index);
	cli_support_json_object_set_int(channel, "phys", phys);
	cli_support_json_object_set_string(channel, "name", name ? name : "");
	cli_support_json_object_set_string(channel, "type", channel_type);

	if (dev_mode == DSO || dev_mode == ANALOG) {
		const char *coupling =
			(cli_source_channel_selection_channel_coupling(
				channel_state, enabled_index) == 1) ?
				"AC" : "DC";

		cli_support_json_object_set_uint64(
			channel, "vdiv_mV",
			cli_source_channel_selection_channel_vdiv(channel_state,
								 enabled_index));
		cli_support_json_object_set_uint64(
			channel, "probe_factor",
			cli_source_channel_selection_channel_vfactor(channel_state,
								    enabled_index));
		cli_support_json_object_set_string(channel, "coupling",
							   coupling);
		cli_support_json_object_set_int(
			channel, "hw_offset",
			(int)cli_source_channel_selection_channel_hw_offset(
				channel_state, enabled_index));
		cli_support_json_object_set_int(
			channel, "bits",
			(int)cli_source_channel_selection_channel_bits(
				channel_state, enabled_index));
	}

	return channel;
}

struct cli_support_json_value *cli_waveform_session_metadata_build_capture_metadata(
	int dev_mode,
	const struct channel_selection_state *channel_state,
	uint64_t samplerate, uint64_t sample_count,
	int unitsize, gboolean normalize_phys)
{
	struct cli_support_json_value *metadata = cli_support_json_new_object();
	struct cli_support_json_value *channel_map;
	struct cli_support_json_value *trigger;
	const char *mode_name = (dev_mode == DSO) ? "dso" :
		(dev_mode == ANALOG) ? "analog" : "logic";
	int n_enabled;

	if (!metadata)
		return NULL;

	n_enabled = cli_source_channel_selection_enabled_count(channel_state);
	cli_support_json_object_set_string(metadata, "mode", mode_name);
	cli_support_json_object_set_uint64(metadata, "samplerate", samplerate);
	cli_support_json_object_set_uint64(metadata, "samples", sample_count);
	cli_support_json_object_set_int(metadata, "unitsize", unitsize);

	channel_map = cli_support_json_new_array();
	if (!channel_map) {
		cli_support_json_value_free(metadata);
		return NULL;
	}

	for (int i = 0; i < n_enabled; i++) {
		struct cli_support_json_value *channel =
			build_capture_channel_value(dev_mode, channel_state, i,
							 normalize_phys);

		if (!channel) {
			cli_support_json_value_free(channel_map);
			cli_support_json_value_free(metadata);
			return NULL;
		}
		cli_support_json_array_append(channel_map, channel);
	}
	cli_support_json_object_set(metadata, "channel_map", channel_map);

	trigger = cli_support_json_new_object();
	if (!trigger) {
		cli_support_json_value_free(metadata);
		return NULL;
	}

	cli_support_json_object_set_bool(
		trigger, "enabled",
		cli_source_channel_selection_trigger_enabled(channel_state));
	if (cli_source_channel_selection_trigger_enabled(channel_state)) {
		cli_support_json_object_set_int(
			trigger, "channel",
			cli_source_channel_selection_trigger_channel(channel_state));
		cli_support_json_object_set_string(
			trigger, "type",
			cli_source_channel_selection_trigger_type(channel_state));
	}
	cli_support_json_object_set_int(
		trigger, "pos_pct",
		cli_source_channel_selection_trigger_position(channel_state));
	cli_support_json_object_set(metadata, "trigger", trigger);
	return metadata;
}

static struct cli_support_json_value *build_dsl_logic_channel_value(
	const struct channel_selection_state *channel_state, int enabled_index)
{
	struct cli_support_json_value *channel = cli_support_json_new_object();
	char default_name[32];
	const char *name = cli_source_channel_selection_display_name(channel_state,
							 enabled_index,
							 default_name,
							 sizeof(default_name));

	if (!channel)
		return NULL;

	cli_support_json_object_set_int(
		channel, "index",
		cli_source_channel_selection_enabled_phys(channel_state,
							  enabled_index));
	cli_support_json_object_set_int(channel, "view_index", enabled_index);
	cli_support_json_object_set_int(channel, "type", SR_CHANNEL_LOGIC);
	cli_support_json_object_set_bool(channel, "enabled", TRUE);
	cli_support_json_object_set_string(channel, "name", name ? name : "");
	cli_support_json_object_set_string(channel, "colour", "default");
	cli_support_json_object_set_int(channel, "strigger", 0);
	return channel;
}

struct cli_support_json_value *cli_waveform_session_metadata_build_dsl_logic_session(
	const struct channel_selection_state *channel_state)
{
	struct cli_support_json_value *session = cli_support_json_new_object();
	struct cli_support_json_value *channels;
	struct cli_support_json_value *decoders;
	int n_enabled = cli_source_channel_selection_enabled_count(channel_state);

	if (!session)
		return NULL;

	cli_support_json_object_set_int(session, "Version",
					DSL_SESSION_FORMAT_VERSION);
	cli_support_json_object_set_string(session, "Device", "DSLogic");
	cli_support_json_object_set_int(session, "DeviceMode", LOGIC);
	cli_support_json_object_set_string(session, "Title", "DSView Export");

	channels = cli_support_json_new_array();
	if (!channels) {
		cli_support_json_value_free(session);
		return NULL;
	}

	for (int i = 0; i < n_enabled; i++) {
		struct cli_support_json_value *channel =
			build_dsl_logic_channel_value(channel_state, i);

		if (!channel) {
			cli_support_json_value_free(channels);
			cli_support_json_value_free(session);
			return NULL;
		}
		cli_support_json_array_append(channels, channel);
	}
	cli_support_json_object_set(session, "channel", channels);

	decoders = cli_support_json_new_array();
	if (!decoders) {
		cli_support_json_value_free(session);
		return NULL;
	}
	cli_support_json_object_set(session, "decoder", decoders);
	return session;
}
