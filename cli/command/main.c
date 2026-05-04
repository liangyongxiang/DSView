#include <stdio.h>

#include "command_internal.h"
#include "json.h"

int main(int argc, char **argv)
{
	struct cli_option_state opts_storage;
	const struct cli_option_state *opts = &opts_storage;
	struct cli_command_shape command_shape_storage;
	const struct cli_command_shape *shape = &command_shape_storage;
	int rc = 1;

	cli_command_option_state_init(&opts_storage);
	cli_command_shape_init(&command_shape_storage);

	if (cli_command_options_parse(&opts_storage, argc, argv) != 0) {
		cli_command_options_show_help(opts);
		goto done;
	}

	if (opts->help) {
		cli_command_options_show_help(opts);
		rc = 0;
		goto done;
	}

	if (cli_command_shape_build(&command_shape_storage, opts) != 0) {
		fprintf(stderr, "%s\n", cli_command_shape_error_text(shape));
		cli_support_json_write_envelope(opts->json_file,
					shape->command_name ? shape->command_name :
					"command",
					FALSE, NULL,
					cli_command_shape_error_text(shape));
		goto done;
	}

	switch (shape->kind) {
	case CLI_COMMAND_SCAN:
		rc = scan_run(opts);
		break;
	case CLI_COMMAND_SHOW:
		rc = show_run(opts);
		break;
	case CLI_COMMAND_GET:
		rc = get_run(opts);
		break;
	case CLI_COMMAND_SET:
		rc = set_run(opts);
		break;
	case CLI_COMMAND_OFFLINE_DECODE:
		rc = decode_run(shape);
		break;
	case CLI_COMMAND_OFFLINE_EXPORT:
		rc = export_run(shape);
		break;
	case CLI_COMMAND_LIVE_CAPTURE:
	case CLI_COMMAND_LIVE_DECODE:
		rc = capture_run(shape);
		break;
	case CLI_COMMAND_NONE:
	default:
		break;
	}

done:
	cli_command_shape_clear(&command_shape_storage);
	cli_command_options_free(&opts_storage);
	return rc;
}
