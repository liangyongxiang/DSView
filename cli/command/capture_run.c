/*
 * capture_run.c - live capture command adapter for dsview-cli.
 */

#include "command_internal.h"
#include "waveform_session.h"

int capture_run(const struct cli_command_shape *shape)
{
	return cli_waveform_session_run_live_command(shape);
}
