#include "command_internal.h"

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void cli_command_option_state_init(struct cli_option_state *state)
{
	if (!state)
		return;

	memset(state, 0, sizeof(*state));
	state->progname = "dsview-cli";
}

void cli_command_option_state_clear(struct cli_option_state *state)
{
	if (!state)
		return;

	g_free(state->drv);
	state->drv = NULL;
	g_free(state->input_file);
	state->input_file = NULL;
	g_strfreev(state->configs);
	state->configs = NULL;
	g_strfreev(state->pd_stacks);
	state->pd_stacks = NULL;
	g_free(state->channels);
	state->channels = NULL;
	g_free(state->output_file);
	state->output_file = NULL;
	g_free(state->output_format);
	state->output_format = NULL;
	g_free(state->samples);
	state->samples = NULL;
	g_free(state->time);
	state->time = NULL;
	g_strfreev(state->gets);
	state->gets = NULL;
	g_strfreev(state->decode_outputs);
	state->decode_outputs = NULL;
	g_free(state->meta_file);
	state->meta_file = NULL;
	g_free(state->json_file);
	state->json_file = NULL;
	g_free(state->trig_pos_arg);
	state->trig_pos_arg = NULL;

	state->scan_devs = FALSE;
	state->show = FALSE;
	state->set = FALSE;
	state->help = FALSE;
}

static int append_string_option(gchar ***items, const char *value)
{
	size_t count = 0;
	gchar **next;

	while (*items && (*items)[count])
		count++;

	next = g_realloc_n(*items, count + 2, sizeof(*next));
	next[count] = g_strdup(value ? value : "");
	next[count + 1] = NULL;
	*items = next;
	return 0;
}

static int set_single_option(gchar **slot, const char *value,
			     const char *option_name)
{
	if (*slot) {
		fprintf(stderr, "option \"%s\" only allowed once\n", option_name);
		return -1;
	}

	*slot = g_strdup(value ? value : "");
	return 0;
}

int cli_command_options_parse(struct cli_option_state *opts, int argc, char **argv)
{
	int opt;

	enum {
		OPT_SCAN = 256,
		OPT_SHOW,
		OPT_GET,
		OPT_SET,
		OPT_PD_STACK,
		OPT_SAMPLES,
		OPT_TIME,
		OPT_DECODE_OUTPUT,
		OPT_META,
		OPT_JSON,
		OPT_TRIG_POS
	};

	static const struct option long_options[] = {
		{ "scan", no_argument, NULL, OPT_SCAN },
		{ "show", no_argument, NULL, OPT_SHOW },
		{ "get", required_argument, NULL, OPT_GET },
		{ "set", no_argument, NULL, OPT_SET },
		{ "driver", required_argument, NULL, 'd' },
		{ "input-file", required_argument, NULL, 'i' },
		{ "config", required_argument, NULL, 'c' },
		{ "protocol-decoders", required_argument, NULL, 'P' },
		{ "samples", required_argument, NULL, OPT_SAMPLES },
		{ "time", required_argument, NULL, OPT_TIME },
		{ "channels", required_argument, NULL, 'C' },
		{ "output-file", required_argument, NULL, 'o' },
		{ "output-format", required_argument, NULL, 'O' },
		{ "decode-output", required_argument, NULL, OPT_DECODE_OUTPUT },
		{ "meta", required_argument, NULL, OPT_META },
		{ "json", required_argument, NULL, OPT_JSON },
		{ "trig-pos", required_argument, NULL, OPT_TRIG_POS },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	opts->progname = (argc > 0 && argv && argv[0]) ? argv[0] : "dsview-cli";
	optind = 1;

	while ((opt = getopt_long(argc, argv, "d:i:c:P:C:o:O:h",
				  long_options, NULL)) != -1) {
		switch (opt) {
		case OPT_SCAN:
			opts->scan_devs = TRUE;
			break;
		case OPT_SHOW:
			opts->show = TRUE;
			break;
		case OPT_GET:
			if (append_string_option(&opts->gets, optarg) != 0)
				return 1;
			break;
		case OPT_SET:
			opts->set = TRUE;
			break;
		case 'd':
			if (set_single_option(&opts->drv, optarg, "--driver/-d") != 0)
				return 1;
			break;
		case 'i':
			if (set_single_option(&opts->input_file, optarg,
					      "--input-file/-i") != 0)
				return 1;
			break;
		case 'c':
			if (append_string_option(&opts->configs, optarg) != 0)
				return 1;
			break;
		case 'P':
			if (append_string_option(&opts->pd_stacks, optarg) != 0)
				return 1;
			break;
		case OPT_SAMPLES:
			if (set_single_option(&opts->samples, optarg,
					      "--samples") != 0)
				return 1;
			break;
		case OPT_TIME:
			if (set_single_option(&opts->time, optarg, "--time") != 0)
				return 1;
			break;
		case 'C':
			if (set_single_option(&opts->channels, optarg,
					      "--channels/-C") != 0)
				return 1;
			break;
		case 'o':
			if (set_single_option(&opts->output_file, optarg,
					      "--output-file/-o") != 0)
				return 1;
			break;
		case 'O':
			if (set_single_option(&opts->output_format, optarg,
					      "--output-format/-O") != 0)
				return 1;
			break;
		case OPT_DECODE_OUTPUT:
			if (append_string_option(&opts->decode_outputs, optarg) != 0)
				return 1;
			break;
		case OPT_META:
			if (set_single_option(&opts->meta_file, optarg, "--meta") != 0)
				return 1;
			break;
		case OPT_JSON:
			if (set_single_option(&opts->json_file, optarg, "--json") != 0)
				return 1;
			break;
		case OPT_TRIG_POS:
			if (set_single_option(&opts->trig_pos_arg, optarg,
					      "--trig-pos") != 0)
				return 1;
			break;
		case 'h':
			opts->help = TRUE;
			break;
		default:
			return 1;
		}
	}

	if (optind != argc) {
		fprintf(stderr, "superfluous command line argument \"%s\"\n",
			argv[optind]);
		return 1;
	}

	return 0;
}

void cli_command_options_show_help(const struct cli_option_state *opts)
{
	fprintf(stderr,
		"Usage:\n"
		"  %s --scan [-d DRIVER[:conn=...]] [--json FILE]\n"
		"  %s --show -d DRIVER[:conn=...] [--json FILE]\n"
		"  %s --get KEY[=VALUE][:KEY=VALUE...] -d DRIVER[:conn=...] "
		    "[-c KEY=VALUE[:...]] [--json FILE]\n"
		"  %s --set -d DRIVER[:conn=...] -c KEY=VALUE[:...] [--json FILE]\n"
		"  %s -d DRIVER[:conn=...] [-c KEY=VALUE[:...]] "
		    "[--samples N | --time T] [-C CHS] -o FILE "
		    "[-O srzip|dsl] [--meta FILE] [--json FILE] [--trig-pos PCT]\n"
		"  %s -i CAPTURE.(dsl|sr) -o FILE [-O srzip|dsl] [--json FILE]\n"
		"  %s -d DRIVER[:conn=...] [-c KEY=VALUE[:...]] "
		    "[--samples N | --time T] [-C CHS] "
		    "(-P STACK --decode-output FILE)... "
		    "[--json FILE] [--trig-pos PCT]\n"
		"  %s -i CAPTURE.dsl (-P STACK --decode-output FILE)... [--json FILE]\n"
		"\n"
		"Stage-1 supported options:\n"
		"  --scan                  Scan for devices.\n"
		"  --show                  Show selected device details.\n"
		"  --get ARG               Get device option(s).\n"
		"  --set                   Set device option(s) only.\n"
		"  -d, --driver SPEC       Device driver spec (driver[:conn=...]).\n"
		"  -i, --input-file FILE   Offline input session file (.dsl/.sr only in stage-1).\n"
		"  -c, --config ARG        Device option(s) key=value[:key=value...].\n"
		"  -P, --protocol-decoders STACK\n"
		"                         Decoder stack. May be specified multiple times.\n"
		"  --samples N             Number of samples to acquire.\n"
		"  --time T                Wall-clock capture duration.\n"
		"  -C, --channels SPEC     Channels to use (for example 0=CLK,1-3).\n"
		"  -o, --output-file FILE  Output file path.\n"
		"  -O, --output-format ID  Output format (srzip or dsl).\n"
		"  --decode-output FILE    Protocol table export file; one required for each -P.\n"
		"  --meta FILE             Optional metadata sidecar path.\n"
		"  --json FILE             Optional command-result JSON sidecar path.\n"
		"  --trig-pos PCT          Pre-trigger percentage (default 50).\n"
		"  -h, --help              Show this help message.\n",
		opts->progname, opts->progname, opts->progname,
		opts->progname, opts->progname, opts->progname,
		opts->progname, opts->progname);
}

void cli_command_options_free(struct cli_option_state *opts)
{
	cli_command_option_state_clear(opts);
}
