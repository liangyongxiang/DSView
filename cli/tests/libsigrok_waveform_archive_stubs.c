#include <glib.h>
#include <string.h>

#include "libsigrok4DSL/libsigrok.h"

static int stub_meta_packets;
static int stub_logic_packets;
static uint64_t stub_logic_bytes;
static int stub_cleanup_calls;

static int stub_srzip_init(struct sr_output *o, GHashTable *options)
{
	(void)o;
	(void)options;
	return SR_OK;
}

static int stub_srzip_receive(const struct sr_output *o,
			      const struct sr_datafeed_packet *packet,
			      GString **out)
{
	(void)o;
	if (out)
		*out = NULL;
	if (!packet)
		return SR_ERR_ARG;
	if (packet->type == SR_DF_META) {
		stub_meta_packets++;
		return SR_OK;
	}
	if (packet->type == SR_DF_LOGIC) {
		const struct sr_datafeed_logic *logic =
			(const struct sr_datafeed_logic *)packet->payload;

		stub_logic_packets++;
		if (logic)
			stub_logic_bytes += logic->length;
		return SR_OK;
	}
	return SR_OK;
}

static int stub_srzip_cleanup(struct sr_output *o)
{
	(void)o;
	stub_cleanup_calls++;
	return SR_OK;
}

static struct sr_output_module stub_srzip_output_module = {
	.id = "srzip",
	.name = "Stub SRZip",
	.desc = "Stub waveform archive output",
	.exts = NULL,
	.options = NULL,
	.init = stub_srzip_init,
	.receive = stub_srzip_receive,
	.cleanup = stub_srzip_cleanup,
};

void cli_test_waveform_archive_stub_reset(void)
{
	stub_meta_packets = 0;
	stub_logic_packets = 0;
	stub_logic_bytes = 0;
	stub_cleanup_calls = 0;
}

int cli_test_waveform_archive_stub_meta_packets(void)
{
	return stub_meta_packets;
}

int cli_test_waveform_archive_stub_logic_packets(void)
{
	return stub_logic_packets;
}

uint64_t cli_test_waveform_archive_stub_logic_bytes(void)
{
	return stub_logic_bytes;
}

int cli_test_waveform_archive_stub_cleanup_calls(void)
{
	return stub_cleanup_calls;
}

SR_API char *sr_samplerate_string(uint64_t samplerate)
{
	return g_strdup_printf("%llu Hz", (unsigned long long)samplerate);
}

SR_API const struct sr_output_module *sr_output_find(char *id)
{
	return (id && strcmp(id, "srzip") == 0) ? &stub_srzip_output_module : NULL;
}

SR_API int sr_output_send(const struct sr_output *o,
			  const struct sr_datafeed_packet *packet,
			  GString **out)
{
	if (!o || !o->module || !o->module->receive) {
		if (out)
			*out = NULL;
		return SR_OK;
	}
	return o->module->receive(o, packet, out);
}

SR_API int sr_output_free(const struct sr_output *o)
{
	if (o && o->module && o->module->cleanup)
		o->module->cleanup((struct sr_output *)o);
	g_free((gpointer)o);
	return SR_OK;
}

SR_API int ds_enable_device_channel_index(int ch_index, gboolean enable)
{
	(void)ch_index;
	(void)enable;
	return SR_OK;
}

SR_API int ds_set_device_channel_name(int ch_index, const char *name)
{
	(void)ch_index;
	(void)name;
	return SR_OK;
}

SR_API int ds_trigger_reset(void)
{
	return SR_OK;
}

SR_API int ds_trigger_probe_set(uint16_t probe, unsigned char trigger0,
				unsigned char trigger1)
{
	(void)probe;
	(void)trigger0;
	(void)trigger1;
	return SR_OK;
}

SR_API int ds_trigger_set_stage(uint16_t stages)
{
	(void)stages;
	return SR_OK;
}

SR_API int ds_trigger_set_pos(uint16_t position)
{
	(void)position;
	return SR_OK;
}

SR_API int ds_trigger_set_en(uint16_t enable)
{
	(void)enable;
	return SR_OK;
}
