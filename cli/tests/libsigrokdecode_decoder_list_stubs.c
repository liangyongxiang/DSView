#include <glib.h>

#include "libsigrokdecode4DSL/libsigrokdecode.h"

static GSList *decoder_list;

static struct srd_channel uart_root_channel = {
	.id = "rx",
	.name = "RX",
	.desc = "UART RX",
	.order = 0,
};

static struct srd_channel midi_root_channel = {
	.id = "din",
	.name = "DIN",
	.desc = "MIDI DIN",
	.order = 0,
};

static struct srd_decoder_option uart_baudrate_option = {
	.id = "baudrate",
	.idn = "baudrate",
	.desc = "Baudrate",
};

static struct srd_decoder_annotation_row uart_annotation_row = {
	.id = "data",
	.desc = "Data",
};

static struct srd_decoder_annotation_row midi_annotation_row = {
	.id = "messages",
	.desc = "Messages",
};

static struct srd_decoder uart_decoder = {
	.id = "0:uart",
	.name = "UART",
	.longname = "Universal Asynchronous Receiver/Transmitter",
	.desc = "Stub UART decoder",
	.license = "gplv2+",
};

static struct srd_decoder midi_decoder = {
	.id = "1:midi",
	.name = "MIDI",
	.longname = "Musical Instrument Digital Interface",
	.desc = "Stub MIDI decoder",
	.license = "gplv2+",
};

static void ensure_decoder_list(void)
{
	if (decoder_list)
		return;

	uart_baudrate_option.def =
	    g_variant_ref_sink(g_variant_new_int64(115200));
	uart_annotation_row.ann_classes =
	    g_slist_append(NULL, GINT_TO_POINTER(0));
	midi_annotation_row.ann_classes =
	    g_slist_append(NULL, GINT_TO_POINTER(0));

	uart_decoder.inputs = g_slist_append(NULL, (gpointer)"logic");
	uart_decoder.outputs = g_slist_append(NULL, (gpointer)"uart");
	uart_decoder.channels = g_slist_append(NULL, &uart_root_channel);
	uart_decoder.options = g_slist_append(NULL, &uart_baudrate_option);
	uart_decoder.annotation_rows =
	    g_slist_append(NULL, &uart_annotation_row);

	midi_decoder.inputs = g_slist_append(NULL, (gpointer)"uart");
	midi_decoder.channels = g_slist_append(NULL, &midi_root_channel);
	midi_decoder.annotation_rows =
	    g_slist_append(NULL, &midi_annotation_row);

	decoder_list = g_slist_append(decoder_list, &uart_decoder);
	decoder_list = g_slist_append(decoder_list, &midi_decoder);
}

SRD_API const GSList *srd_decoder_list(void)
{
	ensure_decoder_list();
	return decoder_list;
}
