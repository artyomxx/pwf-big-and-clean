#pragma once

#include <pebble.h>
#include <time.h>

#include "config.h"

typedef enum {
	DateFormatWeekdayOnly = 0,
	DateFormatFullWeekdayDMYFullMonth = 1,
	DateFormatShortWeekdayDMYFullMonth = 2,
	DateFormatShortWeekdayDMYAbbrYear = 3,
	DateFormatDMYAbbrYear = 4,
	DateFormatDMYFullMonth = 5,
	DateFormatISOSlashWeekdaySuffix = 6,
	DateFormatISOSlash = 7,
	DateFormatISOSpaceWeekdaySuffix = 8,
	DateFormatISOSpace = 9,
	DateFormatAlwaysFriday = 10,
} DateFormat;

#define DATE_FORMAT_LAST DateFormatAlwaysFriday

void settings_load_defaults(void);
void settings_load(void);
void settings_save(void);

void settings_init(void);
void settings_deinit(void);

bool settings_show_steps(void);
bool settings_show_bpm(void);
bool settings_show_sun(void);
bool settings_show_moon(void);
bool settings_show_battery(void);
bool settings_show_labels(void);
bool settings_show_time(void);
bool settings_show_date(void);
bool settings_date_above_time(void);
int32_t settings_date_format(void);
bool settings_human_era_year(void);

bool settings_use_phone_gps(void);
int32_t settings_lat_e6(void);
int32_t settings_lon_e6(void);
int32_t settings_altitude_m(void);
int32_t settings_tz_offset_min(void);

time_t settings_next_sunrise(void);
time_t settings_next_sunset(void);
time_t settings_next_moonrise(void);
time_t settings_next_moonset(void);
void settings_set_solar_times(time_t sunrise, time_t sunset, time_t moonrise, time_t moonset);

const TimelineSettings *settings_timeline(void);
TimelineSettings *settings_timeline_mut(void);

bool settings_defer_center_layout(void);
bool settings_solar_display_enabled(void);
