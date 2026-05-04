#ifndef DSVIEW_CLI_CHANNEL_SELECTION_STATE_H
#define DSVIEW_CLI_CHANNEL_SELECTION_STATE_H

#include <stddef.h>
#include <stdint.h>
#include <glib.h>

#ifndef MAX_CH
#define MAX_CH 16
#endif

struct channel_selection_state {
	int enabled_chs[MAX_CH];
	int n_enabled_chs;
	char ch_names[MAX_CH][64];
	int trig_ch;
	char trig_type[16];
	int trig_pos;
	double vth;
	uint64_t vdiv[2];
	int coupling[2];
	uint64_t probe_factor[2];
	uint64_t ch_vdiv[MAX_CH];
	uint64_t ch_vfactor[MAX_CH];
	uint8_t ch_coupling[MAX_CH];
	uint16_t ch_hw_offset[MAX_CH];
	uint8_t ch_bits[MAX_CH];
	int source_slot_to_output_bit[MAX_CH];
	int hw_nch;
	int unitsize;
};

void cli_source_channel_selection_reset_defaults(struct channel_selection_state *state);
void cli_source_channel_selection_reset_actuals(struct channel_selection_state *state);
void cli_source_channel_selection_clear_request(struct channel_selection_state *state);
int cli_source_channel_selection_add_request(struct channel_selection_state *state,
				  int ch, const char *name);
int cli_source_channel_selection_parse(struct channel_selection_state *state,
			    const char *spec);
void cli_source_channel_selection_set_trigger_pos_arg(struct channel_selection_state *state,
					   const char *text);
int cli_source_channel_selection_prepare_live_logic(
	struct channel_selection_state *state, int hw_nch,
	int total_device_channels, const char **error_text_out);
int cli_source_channel_selection_prepare_input_logic(
	struct channel_selection_state *state, GSList *channels,
	gboolean select_all_when_empty, const char **error_text_out);
int cli_source_channel_selection_enabled_count(const struct channel_selection_state *state);
int cli_source_channel_selection_enabled_phys(const struct channel_selection_state *state,
				   int seq);
const char *cli_source_channel_selection_requested_name(
	const struct channel_selection_state *state, int seq);
const char *cli_source_channel_selection_display_name(
	const struct channel_selection_state *state, int seq,
	char *buffer, size_t buffer_size);
int cli_source_channel_selection_output_bit_for_source_slot(
	const struct channel_selection_state *state, int source_slot);
int cli_source_channel_selection_hw_nch(const struct channel_selection_state *state);
int cli_source_channel_selection_logic_unitsize(
	const struct channel_selection_state *state);
gboolean cli_source_channel_selection_trigger_enabled(
	const struct channel_selection_state *state);
int cli_source_channel_selection_trigger_channel(
	const struct channel_selection_state *state);
const char *cli_source_channel_selection_trigger_type(
	const struct channel_selection_state *state);
int cli_source_channel_selection_trigger_position(
	const struct channel_selection_state *state);
char cli_source_channel_selection_trigger_code(
	const struct channel_selection_state *state);
uint64_t cli_source_channel_selection_logic_trigger_pos(
	const struct channel_selection_state *state, uint64_t sample_count);
uint64_t cli_source_channel_selection_channel_vdiv(
	const struct channel_selection_state *state, int seq);
uint64_t cli_source_channel_selection_channel_vfactor(
	const struct channel_selection_state *state, int seq);
uint8_t cli_source_channel_selection_channel_coupling(
	const struct channel_selection_state *state, int seq);
uint16_t cli_source_channel_selection_channel_hw_offset(
	const struct channel_selection_state *state, int seq);
uint8_t cli_source_channel_selection_channel_bits(
	const struct channel_selection_state *state, int seq);

#endif
