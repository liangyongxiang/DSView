/*
 * export_run.c - offline session conversion and waveform export.
 */

#include "command_internal.h"
#include "waveform_session.h"

int export_run(const struct cli_command_shape *shape)
{
	return cli_waveform_session_run_export_command(shape);
}


