#include <stdlib.h>
#include <string.h>

#include "libsigrok4DSL/libsigrok.h"

SR_API int sr_parse_sizestring(const char *sizestring, uint64_t *size)
{
	char *end = NULL;
	uint64_t multiplier = 1;
	uint64_t value;

	if (!sizestring || !size)
		return SR_ERR;

	value = strtoull(sizestring, &end, 10);
	if (end == sizestring)
		return SR_ERR;

	while (*end == ' ')
		end++;
	if (*end == 'k' || *end == 'K') {
		multiplier = 1000ULL;
		end++;
	} else if (*end == 'm' || *end == 'M') {
		multiplier = 1000000ULL;
		end++;
	} else if (*end == 'g' || *end == 'G') {
		multiplier = 1000000000ULL;
		end++;
	}

	if (*end && strcmp(end, "Hz") != 0)
		return SR_ERR;

	*size = value * multiplier;
	return SR_OK;
}

SR_API uint64_t sr_parse_timestring(const char *timestring)
{
	char *end = NULL;
	uint64_t value;

	if (!timestring)
		return 0;

	value = strtoull(timestring, &end, 10);
	if (end == timestring)
		return 0;

	while (*end == ' ')
		end++;
	if (*end == '\0' || strcmp(end, "ms") == 0)
		return value;
	if (strcmp(end, "s") == 0)
		return value * 1000ULL;
	return 0;
}
