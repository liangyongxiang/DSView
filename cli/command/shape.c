#include "shape.h"

#include <stdarg.h>
#include <string.h>

#include "libsigrok4DSL/libsigrok.h"

static gboolean has_text(const char *text)
{
	return text && *text;
}

static guint strv_length_nullable(gchar **items)
{
	guint count = 0;

	while (items && items[count])
		count++;

	return count;
}

static void clear_shape_error(struct cli_command_shape *shape)
{
	if (!shape)
		return;

	g_free(shape->error_text);
	shape->error_text = NULL;
}

static int set_shape_error(struct cli_command_shape *shape,
			   const char *command_name,
			   const char *fmt, ...)
{
	va_list ap;

	if (!shape)
		return -1;

	clear_shape_error(shape);
	if (command_name)
		shape->command_name = command_name;

	va_start(ap, fmt);
	shape->error_text = g_strdup_vprintf(fmt, ap);
	va_end(ap);
	return -1;
}

static int normalize_capture_numbers(struct cli_command_shape *shape)
{
	if (has_text(shape->opts->samples)) {
		if (sr_parse_sizestring(shape->opts->samples,
					&shape->sample_limit) != SR_OK) {
			return set_shape_error(shape, shape->command_name,
					       "invalid --samples value");
		}
		shape->has_samples = TRUE;
	}

	if (has_text(shape->opts->time)) {
		shape->time_msec = sr_parse_timestring(shape->opts->time);
		if (!shape->time_msec) {
			return set_shape_error(shape, shape->command_name,
					       "invalid --time value");
		}
		shape->has_time = TRUE;
	}

	return 0;
}

static int normalize_output_format(struct cli_command_shape *shape)
{
	shape->output_format_id = has_text(shape->opts->output_format) ?
		shape->opts->output_format : "srzip";
	shape->use_dsl_output =
		g_ascii_strcasecmp(shape->output_format_id, "dsl") == 0;

	if (g_ascii_strcasecmp(shape->output_format_id, "srzip") != 0 &&
	    !shape->use_dsl_output) {
		return set_shape_error(shape, shape->command_name,
				       "unsupported output format");
	}

	return 0;
}

static int unsupported_for_scan(const struct cli_option_state *opts)
{
	return has_text(opts->input_file) || opts->configs || opts->pd_stacks ||
	       has_text(opts->channels) || has_text(opts->output_file) ||
	       has_text(opts->output_format) || has_text(opts->samples) ||
	       has_text(opts->time) || opts->decode_outputs ||
	       has_text(opts->meta_file) || has_text(opts->trig_pos_arg) ||
	       has_text(opts->decode_start) || has_text(opts->decode_end) ||
	       opts->show || opts->gets || opts->set;
}

static int unsupported_for_show(const struct cli_option_state *opts)
{
	return has_text(opts->input_file) || opts->configs || opts->pd_stacks ||
	       has_text(opts->channels) || has_text(opts->output_file) ||
	       has_text(opts->output_format) || has_text(opts->samples) ||
	       has_text(opts->time) || opts->decode_outputs ||
	       has_text(opts->meta_file) || has_text(opts->trig_pos_arg) ||
	       has_text(opts->decode_start) || has_text(opts->decode_end) ||
	       opts->scan_devs || opts->gets || opts->set;
}

static int unsupported_for_get(const struct cli_option_state *opts)
{
	return has_text(opts->input_file) || opts->pd_stacks ||
	       has_text(opts->channels) || has_text(opts->output_file) ||
	       has_text(opts->output_format) || has_text(opts->samples) ||
	       has_text(opts->time) || opts->decode_outputs ||
	       has_text(opts->meta_file) || has_text(opts->trig_pos_arg) ||
	       has_text(opts->decode_start) || has_text(opts->decode_end) ||
	       opts->scan_devs || opts->show || opts->set;
}

static int unsupported_for_set(const struct cli_option_state *opts)
{
	return has_text(opts->input_file) || opts->pd_stacks ||
	       has_text(opts->channels) || has_text(opts->output_file) ||
	       has_text(opts->output_format) || has_text(opts->samples) ||
	       has_text(opts->time) || opts->decode_outputs ||
	       has_text(opts->meta_file) || has_text(opts->trig_pos_arg) ||
	       has_text(opts->decode_start) || has_text(opts->decode_end) ||
	       opts->scan_devs || opts->show || opts->gets;
}

static int unsupported_for_live_capture(const struct cli_option_state *opts)
{
	return has_text(opts->input_file) || opts->pd_stacks ||
	       opts->decode_outputs || opts->scan_devs || opts->show ||
	       has_text(opts->decode_start) || has_text(opts->decode_end) ||
	       opts->gets || opts->set;
}

static int unsupported_for_live_decode(const struct cli_option_state *opts)
{
	return has_text(opts->input_file) || has_text(opts->output_file) ||
	       has_text(opts->output_format) || has_text(opts->meta_file) ||
	       opts->scan_devs || opts->show || opts->gets || opts->set;
}

static int unsupported_for_offline_export(const struct cli_option_state *opts)
{
	return has_text(opts->drv) || opts->configs || has_text(opts->samples) ||
	       has_text(opts->time) || opts->pd_stacks || opts->decode_outputs ||
	       has_text(opts->meta_file) || has_text(opts->trig_pos_arg) ||
	       has_text(opts->decode_start) || has_text(opts->decode_end) ||
	       opts->scan_devs || opts->show || opts->gets || opts->set;
}

static int unsupported_for_offline_decode(const struct cli_option_state *opts)
{
	return has_text(opts->drv) || opts->configs || has_text(opts->samples) ||
	       has_text(opts->time) || has_text(opts->channels) ||
	       has_text(opts->output_file) || has_text(opts->output_format) ||
	       has_text(opts->meta_file) || has_text(opts->trig_pos_arg) ||
	       opts->scan_devs || opts->show || opts->gets || opts->set;
}

void cli_command_shape_init(struct cli_command_shape *shape)
{
	if (!shape)
		return;

	memset(shape, 0, sizeof(*shape));
}

void cli_command_shape_clear(struct cli_command_shape *shape)
{
	if (!shape)
		return;

	clear_shape_error(shape);
	memset(shape, 0, sizeof(*shape));
}

int cli_command_shape_build(struct cli_command_shape *shape,
			    const struct cli_option_state *opts)
{
	guint selector_count;
	gboolean has_input;
	gboolean has_decode_request;

	if (!shape || !opts)
		return -1;

	cli_command_shape_clear(shape);
	shape->opts = opts;
	shape->driver_spec = opts->drv;
	shape->input_file = opts->input_file;
	shape->output_file = opts->output_file;
	shape->meta_file = opts->meta_file;
	shape->json_file = opts->json_file;
	shape->channels = opts->channels;
	shape->trig_pos_arg = opts->trig_pos_arg;
	shape->decode_start = opts->decode_start;
	shape->decode_end = opts->decode_end;
	shape->gets = opts->gets;
	shape->configs = opts->configs;
	shape->pd_stacks = opts->pd_stacks;
	shape->decode_outputs = opts->decode_outputs;
	shape->get_count = strv_length_nullable(opts->gets);
	shape->config_count = strv_length_nullable(opts->configs);
	shape->pd_stack_count = strv_length_nullable(opts->pd_stacks);
	shape->decode_output_count = strv_length_nullable(opts->decode_outputs);

	selector_count = (opts->scan_devs ? 1U : 0U) +
			 (opts->show ? 1U : 0U) +
			 (shape->get_count > 0 ? 1U : 0U) +
			 (opts->set ? 1U : 0U);
	if (selector_count > 1U) {
		return set_shape_error(shape, "command",
				       "top-level command selectors cannot be combined");
	}

	has_input = has_text(opts->input_file);
	has_decode_request = shape->pd_stack_count > 0 ||
			     shape->decode_output_count > 0;

	if (opts->scan_devs) {
		shape->kind = CLI_COMMAND_SCAN;
		shape->command_name = "scan";
		if (unsupported_for_scan(opts)) {
			return set_shape_error(shape, "scan",
					       "scan mode cannot be mixed with non-scan options");
		}
		return 0;
	}

	if (opts->show) {
		shape->kind = CLI_COMMAND_SHOW;
		shape->command_name = "show";
		if (unsupported_for_show(opts)) {
			return set_shape_error(shape, "show",
					       "show mode cannot be mixed with non-show options");
		}
		return 0;
	}

	if (shape->get_count > 0) {
		shape->kind = CLI_COMMAND_GET;
		shape->command_name = "get";
		if (unsupported_for_get(opts)) {
			return set_shape_error(shape, "get",
					       "get mode cannot be mixed with non-get options");
		}
		return 0;
	}

	if (opts->set) {
		shape->kind = CLI_COMMAND_SET;
		shape->command_name = "set";
		if (unsupported_for_set(opts)) {
			return set_shape_error(shape, "set",
					       "set mode cannot be mixed with non-set options");
		}
		if (shape->config_count == 0) {
			return set_shape_error(shape, "set",
					       "no --config values specified");
		}
		return 0;
	}

	if (has_input && has_decode_request) {
		shape->kind = CLI_COMMAND_OFFLINE_DECODE;
		shape->command_name = "decode";
		if (unsupported_for_offline_decode(opts)) {
			return set_shape_error(shape, "decode",
					       "offline protocol decode mode cannot be mixed with live capture options");
		}
		if (shape->pd_stack_count == 0) {
			return set_shape_error(shape, "decode",
					       "offline protocol decode requires -P/--protocol-decoders");
		}
		if (shape->decode_output_count == 0) {
			return set_shape_error(shape, "decode",
					       "offline protocol decode requires --decode-output");
		}
		if (shape->pd_stack_count != shape->decode_output_count) {
			return set_shape_error(shape, "decode",
					       "offline protocol decode requires one --decode-output for each -P");
		}
		return 0;
	}

	if (has_input) {
		shape->kind = CLI_COMMAND_OFFLINE_EXPORT;
		shape->command_name = "export";
		if (unsupported_for_offline_export(opts)) {
			return set_shape_error(shape, "export",
					       "offline export mode cannot be mixed with live capture or protocol decode options");
		}
		if (!has_text(opts->output_file)) {
			return set_shape_error(shape, "export",
					       "offline export requires -o/--output-file");
		}
		return normalize_output_format(shape);
	}

	if (has_decode_request) {
		shape->kind = CLI_COMMAND_LIVE_DECODE;
		shape->command_name = "decode";
		if (unsupported_for_live_decode(opts)) {
			return set_shape_error(shape, "decode",
					       "live protocol decode mode cannot be mixed with capture output options");
		}
		if (shape->pd_stack_count == 0) {
			return set_shape_error(shape, "decode",
					       "live protocol decode requires -P/--protocol-decoders");
		}
		if (shape->decode_output_count == 0) {
			return set_shape_error(shape, "decode",
					       "live protocol decode requires --decode-output");
		}
		if (shape->pd_stack_count != shape->decode_output_count) {
			return set_shape_error(shape, "decode",
					       "live protocol decode requires one --decode-output for each -P");
		}
		return normalize_capture_numbers(shape);
	}

	shape->kind = CLI_COMMAND_LIVE_CAPTURE;
	shape->command_name = "capture";
	if (unsupported_for_live_capture(opts)) {
		return set_shape_error(shape, "capture",
				       "capture mode cannot be mixed with offline or control-only options");
	}
	if (!has_text(opts->output_file)) {
		return set_shape_error(shape, "capture",
				       "capture requires -o/--output-file");
	}
	if (normalize_output_format(shape) != 0)
		return -1;

	return normalize_capture_numbers(shape);
}

const char *cli_command_shape_error_text(const struct cli_command_shape *shape)
{
	return (shape && shape->error_text) ? shape->error_text :
		"invalid command shape";
}
