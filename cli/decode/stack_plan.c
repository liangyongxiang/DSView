#include "stack_plan.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "parse.h"
#include "export.h"

static guint strv_length_nullable(gchar **items)
{
	guint count = 0;

	while (items && items[count])
		count++;

	return count;
}

static void set_plan_error_text(char **error_text_out, const char *fmt, ...)
{
	va_list ap;

	if (!error_text_out)
		return;

	g_free(*error_text_out);
	*error_text_out = NULL;

	va_start(ap, fmt);
	*error_text_out = g_strdup_vprintf(fmt, ap);
	va_end(ap);
}

void cli_decode_stack_plan_free_decoder_step(gpointer data)
{
	struct decode_decoder_step_plan *step =
	    (struct decode_decoder_step_plan *)data;

	if (!step)
		return;
	g_free(step->decoder_id);
	if (step->options)
		g_hash_table_destroy(step->options);
	if (step->channel_indices)
		g_hash_table_destroy(step->channel_indices);
	g_free(step);
}

void cli_decode_stack_plan_free(gpointer data)
{
	struct decode_stack_plan *plan = (struct decode_stack_plan *)data;

	if (!plan)
		return;
	g_free(plan->stack_spec);
	g_free(plan->output_path);
	g_free(plan->output_format_name);
	g_free(plan->row_title);
	if (plan->decoder_steps)
		g_ptr_array_free(plan->decoder_steps, TRUE);
	g_free(plan);
}

static const char *strip_bracketed_protocol_id(const char *text)
{
	const char *start;
	const char *end;

	if (!text || !*text)
		return text;

	start = strchr(text, '[');
	end = start ? strchr(start + 1, ']') : NULL;
	if (start && end && end > start + 1)
		return start + 1;
	return text;
}

static gboolean protocol_id_equals(const char *lhs, const char *rhs)
{
	const char *lhs_id = strip_bracketed_protocol_id(lhs);
	const char *rhs_id = strip_bracketed_protocol_id(rhs);
	size_t lhs_len;
	size_t rhs_len;

	if (!lhs_id || !rhs_id)
		return FALSE;

	lhs_len = strcspn(lhs_id, "]");
	rhs_len = strcspn(rhs_id, "]");
	if (lhs_len != rhs_len)
		return FALSE;

	return g_ascii_strncasecmp(lhs_id, rhs_id, lhs_len) == 0;
}

static const char *decoder_cli_name(const struct srd_decoder *dec)
{
	const char *colon;

	if (!dec || !dec->id)
		return NULL;

	colon = strchr(dec->id, ':');
	return colon ? colon + 1 : dec->id;
}

static const char *decoder_first_input_id(const struct srd_decoder *dec)
{
	if (!dec || !dec->inputs || !dec->inputs->data)
		return NULL;
	return strip_bracketed_protocol_id((const char *)dec->inputs->data);
}

static const char *decoder_first_output_id(const struct srd_decoder *dec)
{
	if (!dec || !dec->outputs || !dec->outputs->data)
		return NULL;
	return strip_bracketed_protocol_id((const char *)dec->outputs->data);
}

static gboolean decoder_name_matches(const struct srd_decoder *dec,
				     const char *name)
{
	if (!dec || !name || !*name)
		return FALSE;

	if (dec->id && g_ascii_strcasecmp(dec->id, name) == 0)
		return TRUE;

	return g_ascii_strcasecmp(decoder_cli_name(dec), name) == 0;
}

static int resolve_decoder_for_stack(const char *requested_name,
				     const char *required_input,
				     gboolean prefer_stackable,
				     struct srd_decoder **dec_out)
{
	const GSList *l;
	struct srd_decoder *best = NULL;
	int best_score = G_MININT;

	if (dec_out)
		*dec_out = NULL;

	for (l = srd_decoder_list(); l; l = l->next) {
		struct srd_decoder *dec = (struct srd_decoder *)l->data;
		const char *dec_input;
		int score = 0;

		if (!decoder_name_matches(dec, requested_name))
			continue;

		dec_input = decoder_first_input_id(dec);
		if (required_input) {
			if (!dec_input || !protocol_id_equals(dec_input, required_input))
				continue;
			score += 100;
		}

		if (dec->id && g_ascii_strcasecmp(dec->id, requested_name) == 0)
			score += 1000;
		else if (decoder_cli_name(dec) &&
			 g_ascii_strcasecmp(decoder_cli_name(dec), requested_name) == 0)
			score += 200;

		if (prefer_stackable && decoder_first_output_id(dec))
			score += 50;
		else if (!prefer_stackable && !decoder_first_output_id(dec))
			score += 50;

		if (dec->id) {
			if (prefer_stackable && g_str_has_prefix(dec->id, "1:"))
				score += 10;
			else if (!prefer_stackable && g_str_has_prefix(dec->id, "0:"))
				score += 5;
		}

		if (score > best_score) {
			best = dec;
			best_score = score;
		}
	}

	if (!best)
		return -1;

	if (dec_out)
		*dec_out = best;
	return 0;
}

static int opts_to_gvar(struct srd_decoder *dec, GHashTable *hash,
			GHashTable **options)
{
	struct srd_decoder_option *opt;
	GSList *optl;
	GVariant *gvar;
	gint64 val_int;
	double val_dbl;
	int ret;
	char *val_str;
	char *conv;

	ret = TRUE;
	*options = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
					 (GDestroyNotify)g_variant_unref);

	for (optl = dec->options; optl; optl = optl->next) {
		opt = (struct srd_decoder_option *)optl->data;
		val_str = g_hash_table_lookup(hash, opt->id);
		if (!val_str)
			continue;
		if (g_variant_is_of_type(opt->def, G_VARIANT_TYPE_STRING)) {
			gvar = g_variant_new_string(val_str);
		} else if (g_variant_is_of_type(opt->def, G_VARIANT_TYPE_INT64)) {
			conv = NULL;
			val_int = strtoll(val_str, &conv, 0);
			if (!conv || conv == val_str || *conv) {
				fprintf(stderr,
					"decoder '%s' option '%s' requires a number\n",
					dec->id, opt->id);
				ret = FALSE;
				break;
			}
			gvar = g_variant_new_int64(val_int);
		} else if (g_variant_is_of_type(opt->def, G_VARIANT_TYPE_DOUBLE)) {
			conv = NULL;
			val_dbl = strtod(val_str, &conv);
			if (!conv || conv == val_str || *conv) {
				fprintf(stderr,
					"decoder '%s' option '%s' requires a float\n",
					dec->id, opt->id);
				ret = FALSE;
				break;
			}
			gvar = g_variant_new_double(val_dbl);
		} else {
			fprintf(stderr, "unsupported option type for decoder '%s'\n",
				dec->id);
			ret = FALSE;
			break;
		}

		g_variant_ref_sink(gvar);
		g_hash_table_insert(*options, g_strdup(opt->id), gvar);
		g_hash_table_remove(hash, opt->id);
	}

	return ret;
}

static GHashTable *extract_channel_map(struct srd_decoder *dec, GHashTable *hash)
{
	GHashTable *channel_map;
	struct srd_channel *pdch;
	const char *single_value = NULL;
	const char *single_alias = NULL;

	channel_map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

	for (const GSList *l = dec->channels; l; l = l->next) {
		pdch = (struct srd_channel *)l->data;
		if (g_hash_table_lookup(hash, pdch->id)) {
			g_hash_table_insert(channel_map, g_strdup(pdch->id),
					    g_strdup(g_hash_table_lookup(hash, pdch->id)));
			g_hash_table_remove(hash, pdch->id);
		}
	}

	for (const GSList *l = dec->opt_channels; l; l = l->next) {
		pdch = (struct srd_channel *)l->data;
		if (g_hash_table_lookup(hash, pdch->id)) {
			g_hash_table_insert(channel_map, g_strdup(pdch->id),
					    g_strdup(g_hash_table_lookup(hash, pdch->id)));
			g_hash_table_remove(hash, pdch->id);
		}
	}

	if (g_slist_length(dec->channels) == 1 &&
	    g_hash_table_size(channel_map) == 0) {
		pdch = (struct srd_channel *)dec->channels->data;
		if (g_hash_table_lookup(hash, "rxtx")) {
			single_alias = "rxtx";
			single_value = g_hash_table_lookup(hash, "rxtx");
		} else if (g_hash_table_lookup(hash, "rx")) {
			single_alias = "rx";
			single_value = g_hash_table_lookup(hash, "rx");
			if (g_hash_table_lookup(hash, "tx") &&
			    g_ascii_strcasecmp(single_value,
					       g_hash_table_lookup(hash, "tx")) != 0) {
				fprintf(stderr,
					"single-wire decoder '%s' cannot bind different rx/tx channels\n",
					dec->id);
				g_hash_table_destroy(channel_map);
				return NULL;
			}
		} else if (g_hash_table_lookup(hash, "tx")) {
			single_alias = "tx";
			single_value = g_hash_table_lookup(hash, "tx");
		}

		if (single_value) {
			g_hash_table_insert(channel_map, g_strdup(pdch->id),
					    g_strdup(single_value));
			g_hash_table_remove(hash, single_alias);
			if (g_strcmp0(single_alias, "rx") == 0 &&
			    g_hash_table_lookup(hash, "tx"))
				g_hash_table_remove(hash, "tx");
		}
	}

	return channel_map;
}

static int lookup_channel_index_by_name(GSList *channels, const char *name,
					int *index_out)
{
	struct sr_channel *ch;

	for (GSList *l = channels; l; l = l->next) {
		ch = (struct sr_channel *)l->data;
		if (ch && ch->name &&
		    g_ascii_strcasecmp(ch->name, name) == 0) {
			*index_out = ch->index;
			return 0;
		}
	}

	return -1;
}

static int build_root_channel_indices(struct srd_decoder *dec,
				      GSList *channels,
				      GHashTable *channel_map,
				      GHashTable **indices_out)
{
	GHashTable *channel_indices;
	GVariant *gvar;
	struct srd_channel *pdch;
	const char *target_name;
	int sig_index;
	int ret = -1;

	*indices_out = NULL;
	channel_indices = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
						(GDestroyNotify)g_variant_unref);

	for (const GSList *l = dec->channels; l; l = l->next) {
		pdch = (struct srd_channel *)l->data;
		target_name = g_hash_table_lookup(channel_map, pdch->id);
		if (!target_name) {
			fprintf(stderr, "decoder channel '%s' must be bound explicitly\n",
				pdch->id);
			goto done;
		}
		if (lookup_channel_index_by_name(channels, target_name, &sig_index) != 0) {
			fprintf(stderr, "logic channel '%s' not found in input file\n",
				target_name);
			goto done;
		}
		gvar = g_variant_new_int32(sig_index);
		g_variant_ref_sink(gvar);
		g_hash_table_insert(channel_indices, g_strdup(pdch->id), gvar);
	}

	for (const GSList *l = dec->opt_channels; l; l = l->next) {
		pdch = (struct srd_channel *)l->data;
		target_name = g_hash_table_lookup(channel_map, pdch->id);
		if (!target_name)
			continue;
		if (lookup_channel_index_by_name(channels, target_name, &sig_index) != 0) {
			fprintf(stderr, "logic channel '%s' not found in input file\n",
				target_name);
			goto done;
		}
		gvar = g_variant_new_int32(sig_index);
		g_variant_ref_sink(gvar);
		g_hash_table_insert(channel_indices, g_strdup(pdch->id), gvar);
	}

	*indices_out = channel_indices;
	channel_indices = NULL;
	ret = 0;

done:
	if (channel_indices)
		g_hash_table_destroy(channel_indices);
	return ret;
}

static int finalize_decode_stack_plan(struct decode_stack_plan *plan,
				      char **error_text_out)
{
	struct decode_decoder_step_plan *root_step;

	if (!plan->decoder_steps || plan->decoder_steps->len == 0) {
		set_plan_error_text(error_text_out, "decode stack #%u is empty",
				    plan->index);
		return -1;
	}

	root_step = (struct decode_decoder_step_plan *)
	    g_ptr_array_index(plan->decoder_steps, 0);
	if (!plan->root_decoder || !root_step || !root_step->channel_indices) {
		set_plan_error_text(error_text_out,
				    "failed to build decode stack plan #%u",
				    plan->index);
		return -1;
	}
	if (!plan->root_decoder->annotation_rows) {
		set_plan_error_text(
		    error_text_out,
		    "decode stack #%u (%s): decoder '%s' has no annotation rows",
		    plan->index, plan->stack_spec, plan->root_decoder->id);
		return -1;
	}

	plan->export_row = cli_decode_export_select_default_row(plan->root_decoder);
	if (!plan->export_row) {
		set_plan_error_text(
		    error_text_out,
		    "decode stack #%u (%s): decoder '%s' has no exportable annotation row",
		    plan->index, plan->stack_spec, plan->root_decoder->id);
		return -1;
	}
	plan->row_title = cli_decode_export_build_decoder_row_title(plan->root_decoder,
							 plan->export_row);
	return 0;
}

static int plan_single_decode_stack(struct decode_stack_plan *plan,
				    GSList *channels,
				    char **error_text_out)
{
	char **tokens;
	const char *required_input = "logic";
	guint token_count = 0;
	int ret = -1;

	tokens = g_strsplit(plan->stack_spec, ",", 0);
	while (tokens[token_count])
		token_count++;
	if (token_count == 0) {
		set_plan_error_text(error_text_out, "decode stack #%u is empty",
				    plan->index);
		goto done;
	}

	for (guint i = 0; i < token_count; i++) {
		GHashTable *pd_args = NULL;
		GHashTable *options = NULL;
		GHashTable *channel_map = NULL;
		GHashTable *channel_indices = NULL;
		struct decode_decoder_step_plan *step = NULL;
		struct srd_decoder *dec = NULL;
		char *pd_name_copy = NULL;
		const char *pd_name;
		const char *next_input;
		GHashTableIter iter;
		gpointer key, value;

		pd_args = cli_command_parse_generic_arg(tokens[i], TRUE, NULL);
		if (!pd_args) {
			set_plan_error_text(
			    error_text_out,
			    "decode stack #%u (%s): invalid protocol decoder specification",
			    plan->index, plan->stack_spec);
			goto done;
		}

		pd_name = g_hash_table_lookup(pd_args, "sigrok_key");
		if (!pd_name || !*pd_name) {
			set_plan_error_text(
			    error_text_out,
			    "decode stack #%u (%s): decoder id is missing",
			    plan->index, plan->stack_spec);
			g_hash_table_destroy(pd_args);
			goto done;
		}
		pd_name_copy = g_strdup(pd_name);
		g_hash_table_remove(pd_args, "sigrok_key");

		if (resolve_decoder_for_stack(pd_name_copy, required_input,
					      i + 1U < token_count, &dec) != 0) {
			set_plan_error_text(
			    error_text_out,
			    "decode stack #%u (%s): decoder not found or incompatible for stack element '%s'",
			    plan->index, plan->stack_spec, pd_name_copy);
			g_free(pd_name_copy);
			g_hash_table_destroy(pd_args);
			goto done;
		}

		if (!opts_to_gvar(dec, pd_args, &options)) {
			set_plan_error_text(
			    error_text_out,
			    "decode stack #%u (%s): invalid decoder option in '%s'",
			    plan->index, plan->stack_spec, dec->id);
			g_hash_table_destroy(pd_args);
			g_hash_table_destroy(options);
			g_free(pd_name_copy);
			goto done;
		}

		if (i == 0) {
			channel_map = extract_channel_map(dec, pd_args);
			if (!channel_map) {
				set_plan_error_text(
				    error_text_out,
				    "decode stack #%u (%s): invalid root decoder channel binding",
				    plan->index, plan->stack_spec);
				g_hash_table_destroy(pd_args);
				g_hash_table_destroy(options);
				g_free(pd_name_copy);
				goto done;
			}
			if (build_root_channel_indices(dec, channels, channel_map,
						      &channel_indices) != 0) {
				set_plan_error_text(
				    error_text_out,
				    "decode stack #%u (%s): root decoder channel binding does not match the input logic channels",
				    plan->index, plan->stack_spec);
				g_hash_table_destroy(pd_args);
				g_hash_table_destroy(options);
				g_hash_table_destroy(channel_map);
				g_free(pd_name_copy);
				goto done;
			}
		}

		g_hash_table_iter_init(&iter, pd_args);
		if (g_hash_table_iter_next(&iter, &key, &value)) {
			set_plan_error_text(
			    error_text_out,
			    "decode stack #%u (%s): unknown decoder key '%s' in -P",
			    plan->index, plan->stack_spec, (const char *)key);
			g_hash_table_destroy(pd_args);
			g_hash_table_destroy(options);
			if (channel_map)
				g_hash_table_destroy(channel_map);
			if (channel_indices)
				g_hash_table_destroy(channel_indices);
			g_free(pd_name_copy);
			goto done;
		}

		step = g_new0(struct decode_decoder_step_plan, 1);
		step->decoder_id = g_strdup(dec->id);
		step->options = options;
		step->channel_indices = channel_indices;
		g_ptr_array_add(plan->decoder_steps, step);
		options = NULL;
		channel_indices = NULL;

		if (i == 0)
			plan->root_decoder = dec;

		g_hash_table_destroy(pd_args);
		if (channel_map)
			g_hash_table_destroy(channel_map);
		g_free(pd_name_copy);
		pd_name_copy = NULL;

		next_input = decoder_first_output_id(dec);
		if (i + 1U < token_count) {
			if (!next_input || !*next_input) {
				set_plan_error_text(
				    error_text_out,
				    "decode stack #%u (%s): decoder '%s' cannot feed the next stacked decoder",
				    plan->index, plan->stack_spec, dec->id);
				goto done;
			}
			required_input = next_input;
		}
	}

	if (finalize_decode_stack_plan(plan, error_text_out) != 0)
		goto done;

	ret = 0;

done:
	g_strfreev(tokens);
	return ret;
}

int cli_decode_stack_plan_build(const struct cli_command_shape *shape,
				GSList *channels,
				GPtrArray **plans_out,
				char **error_text_out)
{
	GPtrArray *plans;
	guint stack_count;

	if (plans_out)
		*plans_out = NULL;
	if (error_text_out)
		*error_text_out = NULL;

	if (!shape || !plans_out)
		return -1;

	stack_count = strv_length_nullable(shape ? shape->pd_stacks : NULL);
	if (stack_count == 0) {
		set_plan_error_text(error_text_out,
				    "no protocol decoder stacks were specified");
		return -1;
	}

	plans = g_ptr_array_new_with_free_func(cli_decode_stack_plan_free);

	for (guint i = 0; i < stack_count; i++) {
		struct decode_stack_plan *plan;

		plan = g_new0(struct decode_stack_plan, 1);
		plan->index = i + 1U;
		plan->stack_spec = g_strdup(shape->pd_stacks[i]);
		plan->output_path = g_strdup(shape->decode_outputs[i]);
		plan->decoder_steps =
		    g_ptr_array_new_with_free_func(cli_decode_stack_plan_free_decoder_step);

		if (cli_decode_export_infer_output_format(plan->output_path,
					       &plan->output_format_name) != 0) {
			set_plan_error_text(
			    error_text_out,
			    "decode stack #%u output must use .csv or .txt",
			    plan->index);
			cli_decode_stack_plan_free(plan);
			g_ptr_array_free(plans, TRUE);
			return -1;
		}
		if (plan_single_decode_stack(plan, channels, error_text_out) != 0) {
			cli_decode_stack_plan_free(plan);
			g_ptr_array_free(plans, TRUE);
			return -1;
		}

		g_ptr_array_add(plans, plan);
	}

	*plans_out = plans;
	return 0;
}
