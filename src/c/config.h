#pragma once

#include <pebble.h>
#include <stdbool.h>
#include <stdint.h>

#define FONT_KEY_LABEL FONT_KEY_GOTHIC_14
#define FONT_KEY_VALUE FONT_KEY_GOTHIC_28
#define FONT_KEY_BATTERY FONT_KEY_GOTHIC_24_BOLD

#define HEALTH_TOP_MARGIN 0
#define HEALTH_SIDE_MARGIN 4
#define HEALTH_LABEL_H 18
#define HEALTH_VALUE_H 32
#define HEALTH_VALUE_LABEL_GAP -3
#define HEALTH_BLOCK_H (HEALTH_VALUE_H + HEALTH_VALUE_LABEL_GAP + HEALTH_LABEL_H)

#define SOLAR_SIDE_MARGIN 4
#define SOLAR_LABEL_H 14
#define SOLAR_TIME_H 24
#define SOLAR_BOTTOM_ROW_H 24
#define SOLAR_LABEL_TIME_GAP -4
#define SOLAR_TIME_ROW_GAP -4
#define SOLAR_BLOCK_H (SOLAR_LABEL_H + SOLAR_LABEL_TIME_GAP + SOLAR_TIME_H \
	+ SOLAR_TIME_ROW_GAP + SOLAR_BOTTOM_ROW_H)
#define BOTTOM_MARGIN 4

#define MAIN_TIME_Y_OFFSET -4
#define CENTER_GAP_ABOVE_TIME_NO_LABELS 5
#define CENTER_GAP_BELOW_TIME_NO_LABELS 0
#define DATE_GAP_BELOW_TIME -4
#define DATE_GAP_ABOVE_TIME -6
#define DATE_GAP_WITHOUT_TIME_EXTRA 6
#define DATE_GAP_WITHOUT_TIME (DATE_GAP_BELOW_TIME + DATE_GAP_WITHOUT_TIME_EXTRA)
#define DATE_ROW_H 24

#define APP_MSG_BUFFER_SIZE 256
#define SOLAR_REFRESH_INTERVAL_SEC (60 * 60)
#define SOLAR_PHONE_REQUEST_MIN_SEC (30 * 60)
// Marker redraw interval — sub-pixel moves aren't visible on ~200px line
#define TIMELINE_REDRAW_INTERVAL_MIN 10

typedef struct __attribute__((__packed__)) {
	bool show;
	bool show_labels;
	int32_t sleep_hours_x10;
	bool wake_from_sleep;
	int32_t manual_wake_min;
	int32_t wake_duration_min;
	int32_t wind_down_min;
} TimelineSettings;

#define TIMELINE_DEFAULT_SLEEP_HOURS_X10 80
#define TIMELINE_DEFAULT_WAKE_FROM_SLEEP true
#define TIMELINE_DEFAULT_MANUAL_WAKE_MIN 420
#define TIMELINE_DEFAULT_WAKE_DURATION_MIN 45
#define TIMELINE_DEFAULT_WIND_DOWN_MIN 60

// Set to 1 for dev profiling (DbgProfileCmd); use 0 for release builds.
#ifndef DEBUG_PROFILE
#define DEBUG_PROFILE 0
#endif
