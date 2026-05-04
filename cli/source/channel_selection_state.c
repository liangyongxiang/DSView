#include "channel_selection_state.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "libsigrok4DSL/libsigrok.h"
#include "libsigrok4DSL/libsigrok-internal.h"

void cli_source_channel_selection_reset_defaults(struct channel_selection_state *state)
{
	if (!state)
		return;

	for (int i = 0; i < MAX_CH; i++) {
		state->enabled_chs[i] = i;
		state->ch_names[i][0] = '\0';
	}
	state->n_enabled_chs = MAX_CH;
	state->trig_ch = -1;
	strcpy(state->trig_type, "none");
	state->trig_pos = 50;
	state->vth = -1.0;
	memset(state->vdiv, 0, sizeof(state->vdiv));
	memset(state->coupling, 0xff, sizeof(state->coupling));
	memset(state->probe_factor, 0, sizeof(state->probe_factor));
	cli_source_channel_selection_reset_actuals(state);
}

void cli_source_channel_selection_reset_actuals(struct channel_selection_state *state)
{
	if (!state)
		return;

	memset(state->ch_vdiv, 0, sizeof(state->ch_vdiv));
	memset(state->ch_vfactor, 0, sizeof(state->ch_vfactor));
	memset(state->ch_coupling, 0, sizeof(state->ch_coupling));
	memset(state->ch_hw_offset, 0, sizeof(state->ch_hw_offset));
	memset(state->ch_bits, 0, sizeof(state->ch_bits));
	for (int i = 0; i < MAX_CH; i++)
		state->source_slot_to_output_bit[i] = -1;
	state->hw_nch = 0;
	state->unitsize = 2;
}

void cli_source_channel_selection_clear_request(struct channel_selection_state *state)
{
	if (!state)
		return;

	memset(state->enabled_chs, 0, sizeof(state->enabled_chs));
	memset(state->ch_names, 0, sizeof(state->ch_names));
	state->n_enabled_chs = 0;
}

int cli_source_channel_selection_add_request(struct channel_selection_state *state,
				  int ch, const char *name)
{
	int slot;

	if (!state)
		return -1;
	if (ch < 0 || ch >= MAX_CH) {
		fprintf(stderr, "invalid channel index: %d\n", ch);
		return -1;
	}

	for (slot = 0; slot < state->n_enabled_chs; slot++) {
		if (state->enabled_chs[slot] == ch) {
			if (name && *name) {
				snprintf(state->ch_names[slot],
					 sizeof(state->ch_names[slot]),
					 "%s", name);
			}
			return 0;
		}
	}

	if (state->n_enabled_chs >= MAX_CH) {
		fprintf(stderr, "too many channels requested\n");
		return -1;
	}

	state->enabled_chs[state->n_enabled_chs] = ch;
	if (name && *name) {
		snprintf(state->ch_names[state->n_enabled_chs],
			 sizeof(state->ch_names[state->n_enabled_chs]),
			 "%s", name);
	}
	state->n_enabled_chs++;
	return 0;
}

int cli_source_channel_selection_parse(struct channel_selection_state *state,
			    const char *spec)
{
	char **tokens;
	int rc = 0;

	if (!state)
		return -1;
	if (!spec || !*spec)
		return 0;

	cli_source_channel_selection_clear_request(state);
	tokens = g_strsplit(spec, ",", 0);
	for (int i = 0; tokens[i]; i++) {
		char *token = tokens[i];
		char *eq = strchr(token, '=');
		char *dash = strchr(token, '-');

		if (eq) {
			char *end = NULL;
			long ch;

			*eq = '\0';
			ch = strtol(token, &end, 10);
			if (end == token || *end != '\0' ||
			    cli_source_channel_selection_add_request(state, (int)ch,
							 eq + 1) != 0) {
				rc = -1;
				break;
			}
			continue;
		}

		if (dash) {
			char *end = NULL;
			long first, last;

			*dash = '\0';
			first = strtol(token, &end, 10);
			if (end == token || *end != '\0') {
				rc = -1;
				break;
			}
			last = strtol(dash + 1, &end, 10);
			if (end == dash + 1 || *end != '\0' || first > last) {
				rc = -1;
				break;
			}
			for (long ch = first; ch <= last; ch++) {
				if (cli_source_channel_selection_add_request(state, (int)ch,
								 NULL) != 0) {
					rc = -1;
					break;
				}
			}
			if (rc != 0)
				break;
			continue;
		}

		{
			char *end = NULL;
			long ch = strtol(token, &end, 10);

			if (end == token || *end != '\0' ||
			    cli_source_channel_selection_add_request(state, (int)ch,
							 NULL) != 0) {
				rc = -1;
				break;
			}
		}
	}

	g_strfreev(tokens);
	if (rc != 0)
		fprintf(stderr, "invalid channel selection: %s\n", spec);
	return rc;
}

void cli_source_channel_selection_set_trigger_pos_arg(struct channel_selection_state *state,
					   const char *text)
{
	if (!state || !text || !*text)
		return;

	state->trig_pos = atoi(text);
}

static void channel_selection_reset_from_logic_channels(
	struct channel_selection_state *state, GSList *channels)
{
	if (!state)
		return;

	cli_source_channel_selection_clear_request(state);
	for (GSList *l = channels; l && cli_source_channel_selection_enabled_count(state) < MAX_CH;
	     l = l->next) {
		struct sr_channel *ch = (struct sr_channel *)l->data;

		if (!ch || ch->type != SR_CHANNEL_LOGIC)
			continue;
		cli_source_channel_selection_add_request(state, ch->index, NULL);
	}
}

static void channel_selection_clear_mapping(struct channel_selection_state *state)
{
	for (int i = 0; i < MAX_CH; i++)
		state->source_slot_to_output_bit[i] = -1;
}

static int channel_selection_finalize_live_logic(
	struct channel_selection_state *state, int hw_nch)
{
	if (!state || hw_nch <= 0 || hw_nch > MAX_CH ||
	    cli_source_channel_selection_enabled_count(state) <= 0)
		return -1;

	channel_selection_clear_mapping(state);
	state->hw_nch = hw_nch;
	state->unitsize = (cli_source_channel_selection_enabled_count(state) <= 8) ? 1 : 2;

	for (int seq = 0; seq < cli_source_channel_selection_enabled_count(state); seq++) {
		int phys = cli_source_channel_selection_enabled_phys(state, seq);

		if (phys < 0 || phys >= hw_nch)
			return -1;
		state->source_slot_to_output_bit[phys] = seq;
	}
	return 0;
}

static int channel_selection_finalize_input_logic(
	struct channel_selection_state *state, GSList *channels, int total_ch)
{
	int mapped = 0;
	int order = 0;

	if (!state || total_ch <= 0 || total_ch > MAX_CH ||
	    cli_source_channel_selection_enabled_count(state) <= 0)
		return -1;

	channel_selection_clear_mapping(state);
	state->hw_nch = total_ch;
	state->unitsize = (cli_source_channel_selection_enabled_count(state) <= 8) ? 1 : 2;

	for (GSList *l = channels; l; l = l->next, order++) {
		struct sr_channel *ch = (struct sr_channel *)l->data;

		if (!ch)
			continue;
		for (int seq = 0; seq < cli_source_channel_selection_enabled_count(state); seq++) {
			if (ch->type == SR_CHANNEL_LOGIC &&
			    ch->index == cli_source_channel_selection_enabled_phys(state, seq)) {
				state->source_slot_to_output_bit[order] = seq;
				mapped++;
				break;
			}
		}
	}

	return mapped == cli_source_channel_selection_enabled_count(state) ? 0 : -1;
}

static int channel_selection_apply_device_layout(
	const struct channel_selection_state *state, int total_ch)
{
	if (!state || total_ch <= 0)
		return SR_ERR_ARG;

	for (int ch = 0; ch < total_ch; ch++) {
		gboolean en = (ch < state->hw_nch) ? TRUE : FALSE;

		ds_enable_device_channel_index(ch, en);

		for (int seq = 0; seq < cli_source_channel_selection_enabled_count(state); seq++) {
			if (cli_source_channel_selection_enabled_phys(state, seq) == ch) {
				const char *name =
				    cli_source_channel_selection_requested_name(state, seq);

				if (name && name[0])
					ds_set_device_channel_name(ch, name);
				break;
			}
		}
	}
	return SR_OK;
}

static void channel_selection_apply_trigger(
	const struct channel_selection_state *state)
{
	ds_trigger_reset();

	if (!cli_source_channel_selection_trigger_enabled(state)) {
		ds_trigger_set_en(0);
		return;
	}

	ds_trigger_set_en(1);
	ds_trigger_set_stage(1);
	ds_trigger_set_pos((uint16_t)cli_source_channel_selection_trigger_position(state));
	ds_trigger_probe_set((uint16_t)cli_source_channel_selection_trigger_channel(state),
			     (unsigned char)cli_source_channel_selection_trigger_code(state),
			     'X');
}

int cli_source_channel_selection_prepare_live_logic(
	struct channel_selection_state *state, int hw_nch,
	int total_device_channels, const char **error_text_out)
{
	int resolved_hw_nch;

	if (error_text_out)
		*error_text_out = NULL;
	if (!state) {
		if (error_text_out)
			*error_text_out = "missing channel selection state";
		return -1;
	}

	resolved_hw_nch = hw_nch > 0 ? hw_nch :
	    cli_source_channel_selection_enabled_count(state);
	if (channel_selection_finalize_live_logic(state, resolved_hw_nch) != 0) {
		if (error_text_out)
			*error_text_out = "invalid logic channel mapping";
		return -1;
	}

	if (total_device_channels <= 0)
		total_device_channels = MAX_CH;
	if (channel_selection_apply_device_layout(state, total_device_channels) !=
	    SR_OK) {
		if (error_text_out)
			*error_text_out = "failed to apply channel layout";
		return -1;
	}

	channel_selection_apply_trigger(state);
	return 0;
}

int cli_source_channel_selection_prepare_input_logic(
	struct channel_selection_state *state, GSList *channels,
	gboolean select_all_when_empty, const char **error_text_out)
{
	int total_ch;

	if (error_text_out)
		*error_text_out = NULL;
	if (!state || !channels) {
		if (error_text_out)
			*error_text_out = "input file has no logic channels";
		return -1;
	}

	if (select_all_when_empty) {
		channel_selection_reset_from_logic_channels(state, channels);
	}
	if (cli_source_channel_selection_enabled_count(state) <= 0) {
		if (error_text_out)
			*error_text_out = "no logic channels selected";
		return -1;
	}

	total_ch = (int)g_slist_length(channels);
	if (total_ch <= 0) {
		if (error_text_out)
			*error_text_out = "input file has no logic channels";
		return -1;
	}
	if (channel_selection_finalize_input_logic(state, channels, total_ch) != 0) {
		if (error_text_out)
			*error_text_out = "selected logic channel not found in input file";
		return -1;
	}
	if (channel_selection_apply_device_layout(state, total_ch) != SR_OK) {
		if (error_text_out)
			*error_text_out = "failed to apply channel layout";
		return -1;
	}

	return 0;
}

gboolean cli_source_channel_selection_trigger_enabled(
	const struct channel_selection_state *state)
{
	return state && state->trig_ch >= 0 &&
	       strcmp(state->trig_type, "none") != 0;
}

char cli_source_channel_selection_trigger_code(
	const struct channel_selection_state *state)
{
	if (!state)
		return 'X';
	if (!strcmp(state->trig_type, "rising"))
		return 'R';
	if (!strcmp(state->trig_type, "falling"))
		return 'F';
	if (!strcmp(state->trig_type, "high"))
		return '1';
	if (!strcmp(state->trig_type, "low"))
		return '0';
	return 'X';
}

int cli_source_channel_selection_enabled_count(const struct channel_selection_state *state)
{
	return state ? state->n_enabled_chs : 0;
}

int cli_source_channel_selection_enabled_phys(const struct channel_selection_state *state,
				   int seq)
{
	if (!state || seq < 0 || seq >= state->n_enabled_chs)
		return -1;
	return state->enabled_chs[seq];
}

const char *cli_source_channel_selection_requested_name(
	const struct channel_selection_state *state, int seq)
{
	if (!state || seq < 0 || seq >= state->n_enabled_chs)
		return NULL;
	return state->ch_names[seq];
}

const char *cli_source_channel_selection_display_name(
	const struct channel_selection_state *state, int seq,
	char *buffer, size_t buffer_size)
{
	const char *name;

	if (!state || seq < 0 || seq >= state->n_enabled_chs)
		return NULL;

	name = state->ch_names[seq];
	if (name[0])
		return name;

	if (buffer && buffer_size > 0) {
		snprintf(buffer, buffer_size, "CH%d", state->enabled_chs[seq]);
		return buffer;
	}
	return "";
}

int cli_source_channel_selection_output_bit_for_source_slot(
	const struct channel_selection_state *state, int source_slot)
{
	if (!state || source_slot < 0 || source_slot >= MAX_CH)
		return -1;
	return state->source_slot_to_output_bit[source_slot];
}

int cli_source_channel_selection_hw_nch(const struct channel_selection_state *state)
{
	return state ? state->hw_nch : 0;
}

int cli_source_channel_selection_logic_unitsize(
	const struct channel_selection_state *state)
{
	return state ? state->unitsize : 0;
}

int cli_source_channel_selection_trigger_channel(const struct channel_selection_state *state)
{
	return state ? state->trig_ch : -1;
}

const char *cli_source_channel_selection_trigger_type(
	const struct channel_selection_state *state)
{
	return state ? state->trig_type : "none";
}

int cli_source_channel_selection_trigger_position(
	const struct channel_selection_state *state)
{
	return state ? state->trig_pos : 0;
}

uint64_t cli_source_channel_selection_logic_trigger_pos(
	const struct channel_selection_state *state, uint64_t sample_count)
{
	if (!state)
		return 0;
	return (sample_count * (uint64_t)state->trig_pos + 50ULL) / 100ULL;
}

uint64_t cli_source_channel_selection_channel_vdiv(
	const struct channel_selection_state *state, int seq)
{
	if (!state || seq < 0 || seq >= state->n_enabled_chs)
		return 0;
	return state->ch_vdiv[seq];
}

uint64_t cli_source_channel_selection_channel_vfactor(
	const struct channel_selection_state *state, int seq)
{
	if (!state || seq < 0 || seq >= state->n_enabled_chs)
		return 0;
	return state->ch_vfactor[seq];
}

uint8_t cli_source_channel_selection_channel_coupling(
	const struct channel_selection_state *state, int seq)
{
	if (!state || seq < 0 || seq >= state->n_enabled_chs)
		return 0;
	return state->ch_coupling[seq];
}

uint16_t cli_source_channel_selection_channel_hw_offset(
	const struct channel_selection_state *state, int seq)
{
	if (!state || seq < 0 || seq >= state->n_enabled_chs)
		return 0;
	return state->ch_hw_offset[seq];
}

uint8_t cli_source_channel_selection_channel_bits(
	const struct channel_selection_state *state, int seq)
{
	if (!state || seq < 0 || seq >= state->n_enabled_chs)
		return 0;
	return state->ch_bits[seq];
}
