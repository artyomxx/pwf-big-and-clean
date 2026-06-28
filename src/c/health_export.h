#pragma once

#include <stdint.h>

#include "config.h"

#if DEBUG_PROFILE

void health_export_dump(int32_t tz_offset_min);

#else

static inline void health_export_dump(int32_t tz_offset_min) {
	(void) tz_offset_min;
}

#endif
