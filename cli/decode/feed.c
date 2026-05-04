#include "runtime_internal.h"
#include "export.h"

#include <string.h>

int cli_decode_runtime_build_channel_order_map(struct decode_runtime *runtime, GSList *channels)
{
	int max_index = -1;
	struct sr_channel *ch;

	if (!runtime)
		return -1;

	runtime->signal_count = 0;
	for (GSList *l = channels; l; l = l->next) {
		ch = (struct sr_channel *)l->data;
		if (!ch)
			continue;
		if ((int)ch->index > max_index)
			max_index = ch->index;
		runtime->signal_count++;
	}
	if (runtime->signal_count == 0)
		return -1;

	runtime->channel_order_len = (size_t)max_index + 1U;
	runtime->channel_order_by_index =
	    g_new0(int, runtime->channel_order_len);
	for (size_t i = 0; i < runtime->channel_order_len; i++)
		runtime->channel_order_by_index[i] = -1;

	{
		int order = 0;

		for (GSList *l = channels; l; l = l->next, order++) {
			ch = (struct sr_channel *)l->data;
			if (!ch)
				continue;
			runtime->channel_order_by_index[ch->index] = order;
		}
	}

	runtime->cross_group_bytes = runtime->signal_count * 8U;
	runtime->cross_leftover = g_malloc0(runtime->cross_group_bytes);
	return 0;
}

void cli_decode_runtime_annotation_callback(struct srd_proto_data *pdata, void *cb_data)
{
	struct decode_stack_runtime *stack =
	    (struct decode_stack_runtime *)cb_data;
	struct decode_runtime *runtime = stack ? stack->runtime : NULL;
	struct srd_proto_data_annotation *pda;
	struct decode_record *rec;

	if (!runtime || !stack || !pdata || !pdata->pdo || !pdata->pdo->di ||
	    pdata->pdo->di->decoder != stack->root_dec)
		return;

	pda = (struct srd_proto_data_annotation *)pdata->data;
	if (!pda || !cli_decode_export_row_contains_class(stack->export_row, pda->ann_class) ||
	    (((!pda->ann_text || !pda->ann_text[0]) &&
	      !pda->str_number_hex[0])))
		return;

	rec = g_new0(struct decode_record, 1);
	rec->start_sample = pdata->start_sample;
	rec->seq = stack->next_seq++;
	rec->text = cli_decode_export_build_annotation_text(pda);
	g_ptr_array_add(stack->records, rec);
	stack->annotations_emitted++;
	runtime->annotations_emitted++;
}

static int send_decode_logic_chunk(struct decode_runtime *runtime,
				   const uint8_t *cross_data, size_t cross_len)
{
	const uint8_t **input_ptrs = NULL;
	uint8_t *input_consts = NULL;
	uint8_t *storage = NULL;
	size_t packed_bytes;
	size_t group_count;
	char *error = NULL;
	int ret = 0;

	group_count = cross_len / runtime->cross_group_bytes;
	if (group_count == 0)
		return 0;

	packed_bytes = group_count * 8U;
	for (guint stack_idx = 0; runtime->stacks &&
	     stack_idx < runtime->stacks->len; stack_idx++) {
		struct decode_stack_runtime *stack =
		    (struct decode_stack_runtime *)g_ptr_array_index(runtime->stacks,
							     stack_idx);

		input_ptrs = g_new0(const uint8_t *, stack->logic_di->dec_num_channels);
		input_consts = g_new0(uint8_t, stack->logic_di->dec_num_channels);
		storage = g_malloc0((size_t)stack->logic_di->dec_num_channels *
				   packed_bytes);

		for (int chan = 0; chan < stack->logic_di->dec_num_channels; chan++) {
			int sig_index = stack->logic_di->dec_channelmap[chan];
			int order;
			uint8_t *dst;

			if (sig_index < 0)
				continue;
			if ((size_t)sig_index >= runtime->channel_order_len) {
				ret = -1;
				cli_decode_stack_runtime_set_error(stack,
						     "decoder channel index %d is out of range",
						     sig_index);
				cli_decode_runtime_set_error(runtime, "decode stack #%u (%s): %s",
					      stack->index, stack->stack_spec,
					      stack->error_text);
				goto done;
			}
			order = runtime->channel_order_by_index[sig_index];
			if (order < 0) {
				ret = -1;
				cli_decode_stack_runtime_set_error(stack,
						     "logic channel index %d not found in input file",
						     sig_index);
				cli_decode_runtime_set_error(runtime, "decode stack #%u (%s): %s",
					      stack->index, stack->stack_spec,
					      stack->error_text);
				goto done;
			}

			dst = storage + (size_t)chan * packed_bytes;
			input_ptrs[chan] = dst;
			for (size_t group = 0; group < group_count; group++) {
				const uint8_t *src =
				    cross_data +
				    ((group * runtime->signal_count) + (size_t)order) * 8U;
				memcpy(dst + group * 8U, src, 8U);
			}
			input_consts[chan] = (uint8_t)(dst[0] & 0x01);
		}

		if (srd_session_send(stack->session,
				     runtime->samples_sent,
				     runtime->samples_sent + (group_count * 64U),
				     input_ptrs,
				     input_consts,
				     group_count * 64U,
				     &error) != SRD_OK) {
			cli_decode_stack_runtime_set_error(stack, "%s",
					     error ? error : "protocol decode failed");
			cli_decode_runtime_set_error(runtime, "decode stack #%u (%s): %s",
					      stack->index, stack->stack_spec,
					      stack->error_text);
			g_free(error);
			ret = -1;
			goto done;
		}

		g_free(storage);
		storage = NULL;
		g_free(input_consts);
		input_consts = NULL;
		g_free(input_ptrs);
		input_ptrs = NULL;
	}
	runtime->samples_sent += group_count * 64U;

done:
	g_free(storage);
	g_free(input_consts);
	g_free(input_ptrs);
	return ret;
}

static int consume_cross_logic(struct decode_runtime *runtime,
			       const uint8_t *src, size_t src_len)
{
	size_t needed;
	size_t complete_len;

	if (!runtime || runtime->cross_group_bytes == 0)
		return -1;

	if (runtime->cross_leftover_len > 0) {
		needed = runtime->cross_group_bytes - runtime->cross_leftover_len;
		if (src_len < needed) {
			memcpy(runtime->cross_leftover + runtime->cross_leftover_len,
			       src, src_len);
			runtime->cross_leftover_len += src_len;
			return 0;
		}

		memcpy(runtime->cross_leftover + runtime->cross_leftover_len,
		       src, needed);
		if (send_decode_logic_chunk(runtime, runtime->cross_leftover,
					    runtime->cross_group_bytes) != 0)
			return -1;
		runtime->cross_leftover_len = 0;
		src += needed;
		src_len -= needed;
	}

	complete_len = (src_len / runtime->cross_group_bytes) *
	    runtime->cross_group_bytes;
	if (complete_len > 0) {
		if (send_decode_logic_chunk(runtime, src, complete_len) != 0)
			return -1;
		src += complete_len;
		src_len -= complete_len;
	}

	if (src_len > 0) {
		memcpy(runtime->cross_leftover, src, src_len);
		runtime->cross_leftover_len = src_len;
	}

	return 0;
}

void cli_decode_runtime_on_event(struct decode_runtime *runtime, int event)
{
	if (!runtime)
		return;

	switch (event) {
	case DS_EV_COLLECT_TASK_END_BY_ERROR:
		cli_decode_runtime_mark_done(runtime, TRUE, "decode playback failed");
		break;
	case DS_EV_COLLECT_TASK_END_BY_DETACHED:
		cli_decode_runtime_mark_done(runtime, TRUE, "decode input was detached");
		break;
	default:
		break;
	}
}

void cli_decode_runtime_on_datafeed(struct decode_runtime *runtime,
			      const struct sr_dev_inst *sdi,
			      const struct sr_datafeed_packet *packet)
{
	const struct sr_datafeed_logic *logic;

	(void)sdi;

	if (!runtime || !packet)
		return;

	switch (packet->type) {
	case SR_DF_LOGIC:
		logic = (const struct sr_datafeed_logic *)packet->payload;
		if (!logic || !logic->data || logic->length == 0)
			return;
		if (logic->format != LA_CROSS_DATA) {
			cli_decode_runtime_mark_done(runtime, TRUE,
					 "unsupported logic packet format: %d",
					 logic->format);
			return;
		}
		if (consume_cross_logic(runtime, (const uint8_t *)logic->data,
					(size_t)logic->length) != 0)
			cli_decode_runtime_mark_done(runtime, TRUE, "%s",
					 runtime->error_text ? runtime->error_text :
					 "failed to decode logic packet");
		break;
	case SR_DF_END:
		if (runtime->cross_leftover_len != 0) {
			cli_decode_runtime_mark_done(runtime, TRUE,
					 "input stream ended with partial logic group");
			return;
		}
		cli_decode_runtime_mark_done(runtime, FALSE, NULL);
		break;
	default:
		break;
	}
}
