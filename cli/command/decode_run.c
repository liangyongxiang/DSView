#include "command_internal.h"
#include "runtime.h"

int decode_run(const struct cli_command_shape *shape)
{
	return cli_decode_runtime_run_offline_command(shape);
}
