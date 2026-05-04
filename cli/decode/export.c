#include "export.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libsigrok4DSL/libsigrok.h"

static gboolean hex_char_is_valid(char c)
{
	return (c >= '0' && c <= '9') ||
	       (c >= 'A' && c <= 'F') ||
	       (c >= 'a' && c <= 'f');
}

static int hex_nibble_value(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return -1;
}

static char *format_hex_ascii_token(const char *hex)
{
	size_t len;
	int hi, lo;
	unsigned value;
	char *out;

	if (!hex || !*hex)
		return g_strdup("");

	len = strlen(hex);
	if (len == 2 && hex_char_is_valid(hex[0]) && hex_char_is_valid(hex[1])) {
		hi = hex_nibble_value(hex[0]);
		lo = hex_nibble_value(hex[1]);
		value = ((unsigned)hi << 4) | (unsigned)lo;
		if (value >= 33U && value <= 126U) {
			out = g_malloc(2);
			out[0] = (char)value;
			out[1] = '\0';
			return out;
		}
	}

	out = g_malloc(len + 3);
	out[0] = '[';
	memcpy(out + 1, hex, len);
	out[len + 1] = ']';
	out[len + 2] = '\0';
	return out;
}

static char *format_annotation_ascii_text(const char *hex_text)
{
	GString *out;
	const char *rd;
	char token[DECODE_NUM_HEX_MAX_LEN];
	size_t token_len;
	char *formatted;

	if (!hex_text || !*hex_text)
		return g_strdup("");

	out = g_string_new("");
	rd = hex_text;
	token_len = 0;

	while (*rd) {
		if (hex_char_is_valid(*rd)) {
			if (token_len + 1U < sizeof(token))
				token[token_len++] = *rd;
		} else {
			if (token_len > 0) {
				token[token_len] = '\0';
				formatted = format_hex_ascii_token(token);
				g_string_append(out, formatted);
				g_free(formatted);
				token_len = 0;
			}
			g_string_append_c(out, *rd);
		}
		rd++;
	}

	if (token_len > 0) {
		token[token_len] = '\0';
		formatted = format_hex_ascii_token(token);
		g_string_append(out, formatted);
		g_free(formatted);
	}

	return g_string_free(out, FALSE);
}

static gboolean row_desc_hidden(const char *desc)
{
	char *lower;
	gboolean hidden;

	if (!desc || !*desc)
		return FALSE;

	lower = g_ascii_strdown(desc, -1);
	hidden = strstr(lower, "bit") != NULL || strstr(lower, "warning") != NULL;
	g_free(lower);
	return hidden;
}

static int compare_decode_records(gconstpointer a, gconstpointer b,
				  gpointer user_data)
{
	const struct decode_record *lhs = *(const struct decode_record *const *)a;
	const struct decode_record *rhs = *(const struct decode_record *const *)b;

	(void)user_data;

	if (lhs->start_sample < rhs->start_sample)
		return -1;
	if (lhs->start_sample > rhs->start_sample)
		return 1;
	if (lhs->seq < rhs->seq)
		return -1;
	if (lhs->seq > rhs->seq)
		return 1;
	return 0;
}

static void csv_write_cell(FILE *f, const char *text)
{
	const unsigned char *p = (const unsigned char *)(text ? text : "");
	gboolean needs_quotes = FALSE;

	for (const unsigned char *q = p; *q; q++) {
		if (*q == ',' || *q == '"' || *q == '\n' || *q == '\r') {
			needs_quotes = TRUE;
			break;
		}
	}

	if (!needs_quotes) {
		fputs((const char *)p, f);
		return;
	}

	fputc('"', f);
	for (; *p; p++) {
		if (*p == '"')
			fputc('"', f);
		fputc(*p, f);
	}
	fputc('"', f);
}

void cli_decode_export_free_record(gpointer data)
{
	struct decode_record *rec = (struct decode_record *)data;

	if (!rec)
		return;
	g_free(rec->text);
	g_free(rec);
}

int cli_decode_export_infer_output_format(const char *path, char **format_out)
{
	const char *dot;

	if (format_out)
		*format_out = NULL;

	dot = path ? strrchr(path, '.') : NULL;
	if (!dot || !dot[1]) {
		fprintf(stderr, "cannot infer decode format from output path\n");
		return -1;
	}
	if (g_ascii_strcasecmp(dot, ".csv") == 0) {
		*format_out = g_strdup("csv");
		return 0;
	}
	if (g_ascii_strcasecmp(dot, ".txt") == 0) {
		*format_out = g_strdup("txt");
		return 0;
	}

	fprintf(stderr, "decode output must use .csv or .txt\n");
	return -1;
}

const struct srd_decoder_annotation_row *cli_decode_export_select_default_row(
	const struct srd_decoder *dec)
{
	const struct srd_decoder_annotation_row *first = NULL;

	for (const GSList *l = dec ? dec->annotation_rows : NULL; l; l = l->next) {
		const struct srd_decoder_annotation_row *row =
		    (const struct srd_decoder_annotation_row *)l->data;

		if (!first)
			first = row;
		if (row && !row_desc_hidden(row->desc))
			return row;
	}

	return first;
}

gboolean cli_decode_export_row_contains_class(const struct srd_decoder_annotation_row *row,
			    int ann_class)
{
	for (const GSList *l = row ? row->ann_classes : NULL; l; l = l->next) {
		if (GPOINTER_TO_INT(l->data) == ann_class)
			return TRUE;
	}
	return FALSE;
}

char *cli_decode_export_build_decoder_row_title(
	const struct srd_decoder *root_dec,
	const struct srd_decoder_annotation_row *export_row)
{
	const char *decoder_name;
	const char *row_desc;

	decoder_name = (root_dec && root_dec->name) ? root_dec->name : "Decoder";
	row_desc = (export_row && export_row->desc) ? export_row->desc : "Decoded";
	return g_strdup_printf("%s: %s", decoder_name, row_desc);
}

char *cli_decode_export_build_row_title(const struct decode_stack_runtime *stack)
{
	return cli_decode_export_build_decoder_row_title(stack ? stack->root_dec : NULL,
					       stack ? stack->export_row : NULL);
}

char *cli_decode_export_build_annotation_text(
	const struct srd_proto_data_annotation *pda)
{
	const char *usable = NULL;
	char *rendered;
	char *mark;

	if (!pda)
		return g_strdup("");

	if (pda->ann_text) {
		for (char **text = pda->ann_text; *text; text++) {
			if ((*text)[0] == '\n')
				continue;
			usable = *text;
			break;
		}
	}

	if (pda->str_number_hex[0]) {
		rendered = format_annotation_ascii_text(pda->str_number_hex);
		if (!usable || !*usable)
			return rendered;

		mark = strstr(usable, "{$}");
		if (mark) {
			GString *out = g_string_new_len(usable, (gssize)(mark - usable));
			g_string_append(out, rendered);
			g_string_append(out, mark + 3);
			g_free(rendered);
			return g_string_free(out, FALSE);
		}

		g_free(rendered);
	}

	return g_strdup(usable ? usable : "");
}

int cli_decode_export_write_table_for_stack(struct decode_stack_runtime *stack,
				 uint64_t samplerate)
{
	FILE *f;
	double ns_per_sample;
	const char *path;

	if (!stack || !stack->output_path || !stack->output_format_name)
		return -1;

	path = stack->output_path;
	f = fopen(path, "w");
	if (!f) {
		fprintf(stderr, "failed to open decode output: %s\n", path);
		return -1;
	}

	g_ptr_array_sort_with_data(stack->records, compare_decode_records, NULL);
	fputs("Id,Time[ns],", f);
	csv_write_cell(f, stack->row_title ? stack->row_title : "Decoded");
	fputc('\n', f);

	ns_per_sample = (double)SR_SEC(1) / (double)samplerate;
	for (guint i = 0; stack->records && i < stack->records->len; i++) {
		const struct decode_record *rec =
		    (const struct decode_record *)g_ptr_array_index(stack->records, i);
		double time_ns = (double)rec->start_sample * ns_per_sample;

		fprintf(f, "%u,%.2f,", i + 1U, time_ns);
		csv_write_cell(f, rec->text ? rec->text : "");
		fputc('\n', f);
	}

	fclose(f);
	stack->rows_written = stack->records ? stack->records->len : 0;
	return 0;
}
