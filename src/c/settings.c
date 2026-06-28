#include "settings.h"

#include "config.h"
#include "timeline.h"
#include "util.h"
#include "view_center.h"
#include "view_health.h"
#include "view_solar.h"
#include "debug_profile.h"

#include <string.h>

#define PERSIST_KEY_SETTINGS 1
#define PERSIST_MAGIC 'U'

typedef struct __attribute__((__packed__)) {
	uint8_t magic;
	bool use_phone_gps;
	int32_t lat_e6;
	int32_t lon_e6;
	int32_t altitude_m;
	int32_t tz_offset_min;
	int32_t next_sunrise;
	int32_t next_sunset;
	int32_t next_moonrise;
	int32_t next_moonset;
	bool show_steps;
	bool show_bpm;
	bool show_sun;
	bool show_moon;
	bool show_battery;
	bool show_labels;
	bool show_timeline;
	bool show_timeline_labels;
	int32_t sleep_hours_x10;
	bool wake_from_sleep;
	int32_t manual_wake_min;
	int32_t wake_duration_min;
	int32_t wind_down_min;
	bool show_date;
	int32_t date_format;
	bool human_era_year;
	bool show_time;
	bool date_above_time;
} SettingsPersist;

static bool s_use_phone_gps;
static int32_t s_lat_e6;
static int32_t s_lon_e6;
static int32_t s_altitude_m;
static int32_t s_tz_offset_min;
static time_t s_next_sunrise;
static time_t s_next_sunset;
static time_t s_next_moonrise;
static time_t s_next_moonset;
static bool s_show_steps = true;
static bool s_show_bpm = true;
static bool s_show_sun = true;
static bool s_show_moon = true;
static bool s_show_battery = true;
static bool s_show_labels = true;
static bool s_show_time = true;
static bool s_show_date = false;
static bool s_date_above_time = false;
static bool s_defer_center_layout;
static int32_t s_date_format;
static bool s_human_era_year = false;
static TimelineSettings s_timeline;

static AppTimer *s_settings_timer;

static void apply_pending_settings(void *data);

static bool update_bool(bool *field, bool value) {
	if (*field == value) {
		return false;
	}
	*field = value;
	return true;
}

static bool update_i32(int32_t *field, int32_t value) {
	if (*field == value) {
		return false;
	}
	*field = value;
	return true;
}

static void schedule_settings_apply(void) {
	if (s_settings_timer) {
		app_timer_cancel(s_settings_timer);
	}
	s_settings_timer = app_timer_register(0, apply_pending_settings, NULL);
}

static void load_timeline_defaults(void) {
	s_timeline.show = false;
	s_timeline.show_labels = true;
	s_timeline.sleep_hours_x10 = TIMELINE_DEFAULT_SLEEP_HOURS_X10;
	s_timeline.wake_from_sleep = TIMELINE_DEFAULT_WAKE_FROM_SLEEP;
	s_timeline.manual_wake_min = TIMELINE_DEFAULT_MANUAL_WAKE_MIN;
	s_timeline.wake_duration_min = TIMELINE_DEFAULT_WAKE_DURATION_MIN;
	s_timeline.wind_down_min = TIMELINE_DEFAULT_WIND_DOWN_MIN;
}

void settings_load_defaults(void) {
	s_show_steps = true;
	s_show_bpm = true;
	s_show_sun = true;
	s_show_moon = true;
	s_show_battery = true;
	s_show_labels = true;
	s_show_time = true;
	s_show_date = false;
	s_date_above_time = false;
	s_date_format = DateFormatWeekdayOnly;
	s_human_era_year = false;
	load_timeline_defaults();
}

bool settings_show_steps(void) { return s_show_steps; }
bool settings_show_bpm(void) { return s_show_bpm; }
bool settings_show_sun(void) { return s_show_sun; }
bool settings_show_moon(void) { return s_show_moon; }
bool settings_show_battery(void) { return s_show_battery; }
bool settings_show_labels(void) { return s_show_labels; }
bool settings_show_time(void) { return s_show_time; }
bool settings_show_date(void) { return s_show_date; }
bool settings_date_above_time(void) { return s_date_above_time; }
int32_t settings_date_format(void) { return s_date_format; }
bool settings_human_era_year(void) { return s_human_era_year; }
bool settings_use_phone_gps(void) { return s_use_phone_gps; }
int32_t settings_lat_e6(void) { return s_lat_e6; }
int32_t settings_lon_e6(void) { return s_lon_e6; }
int32_t settings_altitude_m(void) { return s_altitude_m; }
int32_t settings_tz_offset_min(void) { return s_tz_offset_min; }
time_t settings_next_sunrise(void) { return s_next_sunrise; }
time_t settings_next_sunset(void) { return s_next_sunset; }
time_t settings_next_moonrise(void) { return s_next_moonrise; }
time_t settings_next_moonset(void) { return s_next_moonset; }
const TimelineSettings *settings_timeline(void) { return &s_timeline; }
TimelineSettings *settings_timeline_mut(void) { return &s_timeline; }
bool settings_defer_center_layout(void) { return s_defer_center_layout; }

bool settings_solar_display_enabled(void) {
	return s_show_sun || s_show_moon;
}

void settings_set_solar_times(time_t sunrise, time_t sunset, time_t moonrise, time_t moonset) {
	s_next_sunrise = sunrise;
	s_next_sunset = sunset;
	s_next_moonrise = moonrise;
	s_next_moonset = moonset;
}

void settings_save(void) {
	SettingsPersist settings;

	settings.magic = PERSIST_MAGIC;
	settings.use_phone_gps = s_use_phone_gps;
	settings.lat_e6 = s_lat_e6;
	settings.lon_e6 = s_lon_e6;
	settings.altitude_m = s_altitude_m;
	settings.tz_offset_min = s_tz_offset_min;
	settings.next_sunrise = (int32_t) s_next_sunrise;
	settings.next_sunset = (int32_t) s_next_sunset;
	settings.next_moonrise = (int32_t) s_next_moonrise;
	settings.next_moonset = (int32_t) s_next_moonset;
	settings.show_steps = s_show_steps;
	settings.show_bpm = s_show_bpm;
	settings.show_sun = s_show_sun;
	settings.show_moon = s_show_moon;
	settings.show_battery = s_show_battery;
	settings.show_labels = s_show_labels;
	settings.show_timeline = s_timeline.show;
	settings.show_timeline_labels = s_timeline.show_labels;
	settings.sleep_hours_x10 = s_timeline.sleep_hours_x10;
	settings.wake_from_sleep = s_timeline.wake_from_sleep;
	settings.manual_wake_min = s_timeline.manual_wake_min;
	settings.wake_duration_min = s_timeline.wake_duration_min;
	settings.wind_down_min = s_timeline.wind_down_min;
	settings.show_date = s_show_date;
	settings.date_format = s_date_format;
	settings.human_era_year = s_human_era_year;
	settings.show_time = s_show_time;
	settings.date_above_time = s_date_above_time;
	persist_write_data(PERSIST_KEY_SETTINGS, &settings, sizeof(settings));
}

void settings_load(void) {
	SettingsPersist settings;

	settings_load_defaults();

	if (persist_get_size(PERSIST_KEY_SETTINGS) != (int) sizeof(settings)) {
		return;
	}
	if (persist_read_data(PERSIST_KEY_SETTINGS, &settings, sizeof(settings)) != sizeof(settings)) {
		return;
	}
	if (settings.magic != PERSIST_MAGIC) {
		return;
	}

	s_use_phone_gps = settings.use_phone_gps;
	s_lat_e6 = settings.lat_e6;
	s_lon_e6 = settings.lon_e6;
	s_altitude_m = settings.altitude_m;
	s_tz_offset_min = settings.tz_offset_min;
	s_next_sunrise = settings.next_sunrise;
	s_next_sunset = settings.next_sunset;
	s_next_moonrise = settings.next_moonrise;
	s_next_moonset = settings.next_moonset;
	s_show_steps = settings.show_steps;
	s_show_bpm = settings.show_bpm;
	s_show_sun = settings.show_sun;
	s_show_moon = settings.show_moon;
	s_show_battery = settings.show_battery;
	s_show_labels = settings.show_labels;
	s_timeline.show = settings.show_timeline;
	s_timeline.show_labels = settings.show_timeline_labels;
	s_timeline.sleep_hours_x10 = settings.sleep_hours_x10 > 0 ? settings.sleep_hours_x10 : 80;
	s_timeline.wake_from_sleep = settings.wake_from_sleep;
	s_timeline.manual_wake_min = settings.manual_wake_min;
	s_timeline.wake_duration_min = settings.wake_duration_min > 0 ? settings.wake_duration_min : 45;
	s_timeline.wind_down_min = settings.wind_down_min > 0 ? settings.wind_down_min : 60;
	s_show_date = settings.show_date;
	s_date_format = settings.date_format;
	if (s_date_format < 0 || s_date_format > DATE_FORMAT_LAST) {
		s_date_format = DateFormatWeekdayOnly;
	}
	s_human_era_year = settings.human_era_year;
	s_show_time = settings.show_time;
	s_date_above_time = settings.date_above_time;
}

static bool apply_timeline_settings_from_iterator(DictionaryIterator *iterator) {
	Tuple *tuple;
	TimelineSettings next = s_timeline;
	bool changed = false;

	tuple = dict_find(iterator, MESSAGE_KEY_ShowTimeline);
	if (tuple) {
		const bool value = util_tuple_read_int32(tuple) != 0;
		if (next.show != value) {
			next.show = value;
			changed = true;
		}
	}

	tuple = dict_find(iterator, MESSAGE_KEY_ShowTimelineLabels);
	if (tuple) {
		const bool value = util_tuple_read_int32(tuple) != 0;
		if (next.show_labels != value) {
			next.show_labels = value;
			changed = true;
		}
	}

	tuple = dict_find(iterator, MESSAGE_KEY_SleepHoursX10);
	if (tuple) {
		const int32_t value = util_tuple_read_int32(tuple);
		if (value > 0 && next.sleep_hours_x10 != value) {
			next.sleep_hours_x10 = value;
			changed = true;
		}
	}

	tuple = dict_find(iterator, MESSAGE_KEY_WakeFromSleep);
	if (tuple) {
		const bool value = util_tuple_read_int32(tuple) != 0;
		if (next.wake_from_sleep != value) {
			next.wake_from_sleep = value;
			changed = true;
		}
	}

	tuple = dict_find(iterator, MESSAGE_KEY_ManualWakeMin);
	if (tuple) {
		const int32_t value = util_tuple_read_int32(tuple);
		if (value >= 0 && value < 24 * 60 && next.manual_wake_min != value) {
			next.manual_wake_min = value;
			changed = true;
		}
	}

	tuple = dict_find(iterator, MESSAGE_KEY_WakeDurationMin);
	if (tuple) {
		const int32_t value = util_tuple_read_int32(tuple);
		if (value >= 0 && next.wake_duration_min != value) {
			next.wake_duration_min = value;
			changed = true;
		}
	}

	tuple = dict_find(iterator, MESSAGE_KEY_WindDownMin);
	if (tuple) {
		const int32_t value = util_tuple_read_int32(tuple);
		if (value >= 0 && next.wind_down_min != value) {
			next.wind_down_min = value;
			changed = true;
		}
	}

	if (changed) {
		s_timeline = next;
		timeline_settings_apply(&next);
	}
	return changed;
}

static bool apply_display_settings_from_iterator(DictionaryIterator *iterator) {
	Tuple *tuple;
	bool changed = false;

	tuple = dict_find(iterator, MESSAGE_KEY_ShowSteps);
	if (tuple) {
		changed |= update_bool(&s_show_steps, util_tuple_read_int32(tuple) != 0);
	}

	tuple = dict_find(iterator, MESSAGE_KEY_ShowBpm);
	if (tuple) {
		changed |= update_bool(&s_show_bpm, util_tuple_read_int32(tuple) != 0);
	}

	tuple = dict_find(iterator, MESSAGE_KEY_ShowSun);
	if (tuple) {
		changed |= update_bool(&s_show_sun, util_tuple_read_int32(tuple) != 0);
	}

	tuple = dict_find(iterator, MESSAGE_KEY_ShowMoon);
	if (tuple) {
		changed |= update_bool(&s_show_moon, util_tuple_read_int32(tuple) != 0);
	}

	tuple = dict_find(iterator, MESSAGE_KEY_ShowBattery);
	if (tuple) {
		changed |= update_bool(&s_show_battery, util_tuple_read_int32(tuple) != 0);
	}

	tuple = dict_find(iterator, MESSAGE_KEY_ShowLabels);
	if (tuple) {
		changed |= update_bool(&s_show_labels, util_tuple_read_int32(tuple) != 0);
	}

	tuple = dict_find(iterator, MESSAGE_KEY_ShowTime);
	if (tuple) {
		changed |= update_bool(&s_show_time, util_tuple_read_int32(tuple) != 0);
	}

	tuple = dict_find(iterator, MESSAGE_KEY_ShowDate);
	if (tuple) {
		changed |= update_bool(&s_show_date, util_tuple_read_int32(tuple) != 0);
	}

	tuple = dict_find(iterator, MESSAGE_KEY_DateAboveTime);
	if (tuple) {
		changed |= update_bool(&s_date_above_time, util_tuple_read_int32(tuple) != 0);
	}

	tuple = dict_find(iterator, MESSAGE_KEY_DateFormat);
	if (tuple) {
		const int32_t value = util_tuple_read_int32(tuple);
		if (value >= 0 && value <= DATE_FORMAT_LAST) {
			changed |= update_i32(&s_date_format, value);
		}
	}

	tuple = dict_find(iterator, MESSAGE_KEY_HumanEraYear);
	if (tuple) {
		changed |= update_bool(&s_human_era_year, util_tuple_read_int32(tuple) != 0);
	}

	return changed;
}

static void apply_pending_settings(void *data) {
	(void) data;

	s_settings_timer = NULL;
	// Batch layout: coords + timeline wake before a single visibility pass.
	debug_profile_count(DBG_SETTINGS_APPLY);
	s_defer_center_layout = true;
	timeline_set_tz_offset(s_tz_offset_min);
	s_defer_center_layout = false;
	view_health_apply_visibility();
	view_solar_apply_visibility();
	view_center_apply_visibility();
	view_center_apply_layout();
	view_center_update_date();
	settings_save();
	view_solar_on_settings_changed();
}

static void inbox_received_handler(DictionaryIterator *iterator, void *context) {
	(void) context;
	Tuple *tuple;
	bool changed = false;
	const bool profile_cmd = debug_profile_handle_message(iterator);

	s_defer_center_layout = true;

	tuple = dict_find(iterator, MESSAGE_KEY_UsePhoneGPS);
	if (tuple) {
		changed |= update_bool(&s_use_phone_gps, util_tuple_read_int32(tuple) != 0);
	}

	tuple = dict_find(iterator, MESSAGE_KEY_LatitudeE6);
	if (tuple) {
		changed |= update_i32(&s_lat_e6, util_tuple_read_int32(tuple));
	}

	tuple = dict_find(iterator, MESSAGE_KEY_LongitudeE6);
	if (tuple) {
		changed |= update_i32(&s_lon_e6, util_tuple_read_int32(tuple));
	}

	tuple = dict_find(iterator, MESSAGE_KEY_AltitudeM);
	if (tuple) {
		int32_t value = util_tuple_read_int32(tuple);
		if (value < 0) {
			value = 0;
		}
		changed |= update_i32(&s_altitude_m, value);
	}

	tuple = dict_find(iterator, MESSAGE_KEY_TimezoneOffsetM);
	if (tuple) {
		changed |= update_i32(&s_tz_offset_min, util_tuple_read_int32(tuple));
	}

	tuple = dict_find(iterator, MESSAGE_KEY_Latitude);
	if (tuple && tuple->type == TUPLE_CSTRING) {
		const int32_t parsed = util_coord_str_to_e6(tuple->value->cstring);
		if (parsed != 0) {
			changed |= update_i32(&s_lat_e6, parsed);
		}
	}

	tuple = dict_find(iterator, MESSAGE_KEY_Longitude);
	if (tuple && tuple->type == TUPLE_CSTRING) {
		const int32_t parsed = util_coord_str_to_e6(tuple->value->cstring);
		if (parsed != 0) {
			changed |= update_i32(&s_lon_e6, parsed);
		}
	}

	changed |= apply_display_settings_from_iterator(iterator);
	changed |= apply_timeline_settings_from_iterator(iterator);

	if (!changed) {
		if (!profile_cmd) {
			debug_profile_count(DBG_SETTINGS_SKIPPED);
		}
		s_defer_center_layout = false;
		return;
	}

	schedule_settings_apply();
}

static void outbox_sent_handler(DictionaryIterator *iterator, void *context) {
	(void) iterator;
	(void) context;
	view_solar_clear_request_pending();
}

static void outbox_failed_handler(DictionaryIterator *iterator, AppMessageResult reason, void *context) {
	(void) iterator;
	(void) reason;
	(void) context;
	view_solar_clear_request_pending();
}

void settings_init(void) {
	app_message_open(APP_MSG_BUFFER_SIZE, APP_MSG_BUFFER_SIZE);
	app_message_register_inbox_received(inbox_received_handler);
	app_message_register_outbox_sent(outbox_sent_handler);
	app_message_register_outbox_failed(outbox_failed_handler);
}

void settings_deinit(void) {
	if (s_settings_timer) {
		app_timer_cancel(s_settings_timer);
	}
}
