#pragma once

#include "config.h"

#include <pebble.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#define PERSIST_KEY_TIMELINE_CYCLE 2
#define TIMELINE_PERSIST_MAGIC 'f'

#define TIMELINE_MIN_MERGED_SLEEP_SEC (3 * 3600)
#define TIMELINE_SLEEP_MERGE_GAP_SEC (30 * 60)
#define TIMELINE_SLEEP_LOOKBACK_SEC (24 * 3600)
#define TIMELINE_ACTIVITY_AFTER_BED_SEC (2 * 3600)
#define TIMELINE_MIN_SLEEP_SEGMENT_SEC (40 * 60)
#define TIMELINE_MIN_OFF_WRIST_SEC TIMELINE_MIN_SLEEP_SEGMENT_SEC
#define TIMELINE_WAKE_CONFIRM_MINUTES 2
#define TIMELINE_LONG_GAP_WAKE_SEC (8 * 3600)
#define TIMELINE_POST_WAKE_GAP_COOLDOWN_SEC (4 * 3600)
#define TIMELINE_MAX_SLEEP_SEGMENTS 32
#define TIMELINE_ACTIVITY_HISTORY_CHUNK 64
#define TIMELINE_LOGIC_INTERVAL_MIN 10
#define TIMELINE_ON_WRIST_LOOKBACK_MIN 10
#define TIMELINE_AWAKE_RECENT_SLEEP_SEC (10 * 60)
#define TIMELINE_AWAKE_GRACE_SLEEP_SEC (30 * 60)
#define TIMELINE_AWAKE_GRACE_MAX_STEPS 40
#define TIMELINE_MIN_STEP_REST_QUIET_SEC (6 * 3600)
#define TIMELINE_MORNING_WAKE_HOUR_START 5
#define TIMELINE_MORNING_WAKE_HOUR_END 12
#define TIMELINE_STEP_RESUME_LOOKBACK_SEC (30 * 3600)

typedef struct {
	time_t wake_utc;
	bool wake_valid;
} TimelineWakeSnapshot;

typedef struct {
	void (*on_wake_changed)(void);
} TimelineWakeHooks;

void timeline_wake_set_hooks(const TimelineWakeHooks *hooks);
void timeline_wake_bootstrap(int32_t tz_offset_min, const TimelineSettings *settings);
void timeline_wake_set_tz_offset(int32_t tz_offset_min);
void timeline_wake_set_settings(const TimelineSettings *settings);
void timeline_wake_persist_load(void);
void timeline_wake_invalidate(void);
bool timeline_wake_settings_relevant_diff(const TimelineSettings *prev, const TimelineSettings *next);
void timeline_wake_run(void);
void timeline_wake_on_sleep_update(void);
bool timeline_wake_user_is_sleeping(void);
bool timeline_wake_marker_updates_allowed(void);
void timeline_wake_snapshot(TimelineWakeSnapshot *out);
int32_t timeline_wake_awake_minutes(const TimelineSettings *settings);
void timeline_wake_format_local(time_t utc_timestamp, int32_t tz_offset_min, char *buffer,
		size_t buffer_size);
