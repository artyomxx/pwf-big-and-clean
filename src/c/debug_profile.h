#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "config.h"

#if DEBUG_PROFILE

typedef enum {
	DBG_TICK = 0,
	DBG_TICK_SLEEP_SKIP,
	DBG_HEALTH_EVENT,
	DBG_HEALTH_SLEEP_SKIP,
	DBG_CENTER_LAYOUT,
	DBG_CENTER_LAYOUT_CACHED,
	DBG_TIMELINE_REFRESH,
	DBG_TIMELINE_LAYOUT_CB,
	DBG_TIMELINE_WAKE_SLEEP,
	DBG_TIMELINE_WAKE_ACTIVITY,
	DBG_TIMELINE_WAKE_MANUAL,
	DBG_TIMELINE_WAKE_SCAN_MIN,
	DBG_TIMELINE_DIRTY,
	DBG_SOLAR_MAYBE_REFRESH,
	DBG_SOLAR_SCHEDULED,
	DBG_SOLAR_PHONE_REQ,
	DBG_SETTINGS_APPLY,
	DBG_SETTINGS_SKIPPED,
	DBG_CONNECTION,
	DBG_COUNT,
} DebugTag;

#define DBG_PROFILE_CMD_RESET 1
#define DBG_PROFILE_CMD_DUMP 2
#define DBG_PROFILE_CMD_HEALTH_EXPORT 3

void debug_profile_init(void);
void debug_profile_deinit(void);
void debug_profile_reset(void);
void debug_profile_dump(void);
bool debug_profile_handle_message(void *iterator);
void debug_profile_count(DebugTag tag);
void debug_profile_add(DebugTag tag, uint32_t amount);

#else

#define debug_profile_init()
#define debug_profile_deinit()
#define debug_profile_reset()
#define debug_profile_dump()
#define debug_profile_handle_message(iterator) false
#define debug_profile_count(tag)
#define debug_profile_add(tag, amount)

#endif
