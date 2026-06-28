#include "view_solar.h"

#include "config.h"
#include "settings.h"
#include "solar_calc.h"
#include "timeline.h"
#include "util.h"
#include "debug_profile.h"

#include <stdio.h>
#include <time.h>

static Layer *s_block;
static TextLayer *s_sun_label;
static TextLayer *s_sun_rise;
static TextLayer *s_sun_set;
static TextLayer *s_moon_label;
static TextLayer *s_moon_rise;
static TextLayer *s_moon_set;
static TextLayer *s_battery;

static AppTimer *s_solar_timer;
static int s_solar_step;
static time_t s_last_solar_calc;
static double s_calc_lat;
static double s_calc_lon;
static time_t s_calc_now;
static bool s_solar_request_pending;

static void schedule_solar_calc(void *data);
static void request_solar_from_phone(void);
static void force_solar_recalc(void);
static bool solar_inputs_changed(void);
static void remember_solar_inputs(void);

void view_solar_create(Layer *parent, GRect bounds) {
	GFont font_label = fonts_get_system_font(FONT_KEY_LABEL);
	GFont font_battery = fonts_get_system_font(FONT_KEY_BATTERY);
	const int half_w = bounds.size.w / 2;
	const int bottom_y = bounds.size.h - SOLAR_BLOCK_H - BOTTOM_MARGIN;
	const int rise_y = SOLAR_LABEL_H + SOLAR_LABEL_TIME_GAP;
	const int bottom_row_y = rise_y + SOLAR_TIME_H + SOLAR_TIME_ROW_GAP;

	s_block = layer_create(GRect(0, bottom_y, bounds.size.w, SOLAR_BLOCK_H));
	layer_add_child(parent, s_block);

	s_sun_label = util_create_text_layer(
		s_block,
		GRect(SOLAR_SIDE_MARGIN, 0, half_w - SOLAR_SIDE_MARGIN, SOLAR_LABEL_H),
		GTextAlignmentLeft, font_label);
	text_layer_set_text(s_sun_label, "SUN");
	s_sun_rise = util_create_text_layer(
		s_block,
		GRect(SOLAR_SIDE_MARGIN, rise_y, half_w - SOLAR_SIDE_MARGIN, SOLAR_TIME_H),
		GTextAlignmentLeft, font_battery);
	text_layer_set_text(s_sun_rise, "--:--");
	s_sun_set = util_create_text_layer(
		s_block,
		GRect(SOLAR_SIDE_MARGIN, bottom_row_y, half_w - SOLAR_SIDE_MARGIN, SOLAR_BOTTOM_ROW_H),
		GTextAlignmentLeft, font_battery);
	text_layer_set_text(s_sun_set, "--:--");

	s_moon_label = util_create_text_layer(
		s_block,
		GRect(half_w, 0, half_w - SOLAR_SIDE_MARGIN, SOLAR_LABEL_H),
		GTextAlignmentRight, font_label);
	text_layer_set_text(s_moon_label, "MOON");
	s_moon_rise = util_create_text_layer(
		s_block,
		GRect(half_w, rise_y, half_w - SOLAR_SIDE_MARGIN, SOLAR_TIME_H),
		GTextAlignmentRight, font_battery);
	text_layer_set_text(s_moon_rise, "--:--");
	s_moon_set = util_create_text_layer(
		s_block,
		GRect(half_w, bottom_row_y, half_w - SOLAR_SIDE_MARGIN, SOLAR_BOTTOM_ROW_H),
		GTextAlignmentRight, font_battery);
	text_layer_set_text(s_moon_set, "--:--");

	s_battery = util_create_text_layer(
		s_block,
		GRect(0, bottom_row_y, bounds.size.w, SOLAR_BOTTOM_ROW_H),
		GTextAlignmentCenter, font_battery);
	text_layer_set_text(s_battery, "---%");
	remember_solar_inputs();
}

void view_solar_destroy(void) {
	if (s_solar_timer) {
		app_timer_cancel(s_solar_timer);
	}
	text_layer_destroy(s_sun_label);
	text_layer_destroy(s_sun_rise);
	text_layer_destroy(s_sun_set);
	text_layer_destroy(s_moon_label);
	text_layer_destroy(s_moon_rise);
	text_layer_destroy(s_moon_set);
	text_layer_destroy(s_battery);
	layer_destroy(s_block);
}

void view_solar_apply_visibility(void) {
	layer_set_hidden(text_layer_get_layer(s_sun_label), !settings_show_sun() || !settings_show_labels());
	layer_set_hidden(text_layer_get_layer(s_sun_rise), !settings_show_sun());
	layer_set_hidden(text_layer_get_layer(s_sun_set), !settings_show_sun());
	layer_set_hidden(text_layer_get_layer(s_moon_label), !settings_show_moon() || !settings_show_labels());
	layer_set_hidden(text_layer_get_layer(s_moon_rise), !settings_show_moon());
	layer_set_hidden(text_layer_get_layer(s_moon_set), !settings_show_moon());
	layer_set_hidden(text_layer_get_layer(s_battery), !settings_show_battery());
}

void view_solar_update_display(void) {
	if (!s_sun_rise) {
		return;
	}

	static char sun_rise_buffer[8];
	static char sun_set_buffer[8];
	static char moon_rise_buffer[8];
	static char moon_set_buffer[8];
	const int32_t tz = settings_tz_offset_min();

	util_format_hhmm(settings_next_sunrise(), tz, sun_rise_buffer, sizeof(sun_rise_buffer));
	util_format_hhmm(settings_next_sunset(), tz, sun_set_buffer, sizeof(sun_set_buffer));
	util_format_hhmm(settings_next_moonrise(), tz, moon_rise_buffer, sizeof(moon_rise_buffer));
	util_format_hhmm(settings_next_moonset(), tz, moon_set_buffer, sizeof(moon_set_buffer));

	text_layer_set_text(s_sun_rise, sun_rise_buffer);
	text_layer_set_text(s_sun_set, sun_set_buffer);
	text_layer_set_text(s_moon_rise, moon_rise_buffer);
	text_layer_set_text(s_moon_set, moon_set_buffer);
}

void view_solar_update_battery(void) {
	if (!settings_show_battery()) {
		return;
	}

	BatteryChargeState state = battery_state_service_peek();
	static char battery_buffer[12];

	snprintf(battery_buffer, sizeof(battery_buffer), "%d%%", state.charge_percent);
	text_layer_set_text(s_battery, battery_buffer);
}

void view_solar_on_battery_change(BatteryChargeState state) {
	(void) state;
	view_solar_update_battery();
}

static void run_solar_step(void *data) {
	(void) data;

	s_solar_timer = NULL;

	if (!settings_solar_display_enabled()) {
		s_solar_step = 0;
		return;
	}

	if (settings_lat_e6() == 0 && settings_lon_e6() == 0) {
		s_solar_step = 0;
		return;
	}

	switch (s_solar_step) {
		case 0:
			s_calc_lat = settings_lat_e6() / 1000000.0;
			s_calc_lon = settings_lon_e6() / 1000000.0;
			s_calc_now = time(NULL);
			if (!settings_show_sun()) {
				s_solar_step = settings_show_moon() ? 2 : 0;
				if (settings_show_moon()) {
					s_solar_timer = app_timer_register(250, run_solar_step, NULL);
				}
				return;
			}
			settings_set_solar_times(
				solar_calc_next_sunrise(s_calc_lat, s_calc_lon, settings_altitude_m(),
					settings_tz_offset_min(), s_calc_now),
				settings_next_sunset(), settings_next_moonrise(), settings_next_moonset());
			s_solar_step = 1;
			s_solar_timer = app_timer_register(250, run_solar_step, NULL);
			return;
		case 1: {
			time_t sunset = solar_calc_next_sunset(s_calc_lat, s_calc_lon, settings_altitude_m(),
				settings_tz_offset_min(), s_calc_now);
			settings_set_solar_times(settings_next_sunrise(), sunset,
				settings_next_moonrise(), settings_next_moonset());
			if (!settings_show_moon()) {
				s_solar_step = 0;
				s_last_solar_calc = time(NULL);
				view_solar_update_display();
				settings_save();
				return;
			}
			s_solar_step = 2;
			s_solar_timer = app_timer_register(250, run_solar_step, NULL);
			return;
		}
		case 2: {
			time_t moon_rise = 0;
			time_t moon_set = 0;
			solar_calc_next_moon(s_calc_lat, s_calc_lon, settings_altitude_m(),
				settings_tz_offset_min(), s_calc_now, &moon_rise, &moon_set);
			settings_set_solar_times(settings_next_sunrise(), settings_next_sunset(), moon_rise, moon_set);
			s_solar_step = 0;
			s_last_solar_calc = time(NULL);
			view_solar_update_display();
			settings_save();
			return;
		}
		default:
			s_solar_step = 0;
			return;
	}
}

static bool solar_event_passed(time_t now) {
	if (settings_show_sun()) {
		if (settings_next_sunrise() > 0 && now >= settings_next_sunrise()) {
			return true;
		}
		if (settings_next_sunset() > 0 && now >= settings_next_sunset()) {
			return true;
		}
	}
	if (settings_show_moon()) {
		if (settings_next_moonrise() > 0 && now >= settings_next_moonrise()) {
			return true;
		}
		if (settings_next_moonset() > 0 && now >= settings_next_moonset()) {
			return true;
		}
	}
	return false;
}

static void force_solar_recalc(void) {
	if (!settings_solar_display_enabled()) {
		return;
	}

	s_last_solar_calc = 0;
	schedule_solar_calc(NULL);

	if (settings_use_phone_gps()) {
		request_solar_from_phone();
	}
}

static void schedule_solar_calc(void *data) {
	(void) data;

	if (!settings_solar_display_enabled()) {
		return;
	}

	if (settings_lat_e6() == 0 && settings_lon_e6() == 0) {
		return;
	}

	if (s_solar_timer) {
		app_timer_cancel(s_solar_timer);
	}

	s_solar_step = 0;
	s_solar_timer = app_timer_register(300, run_solar_step, NULL);
	debug_profile_count(DBG_SOLAR_SCHEDULED);
}

static time_t s_last_phone_request;

static bool phone_request_allowed(bool force) {
	const time_t now = time(NULL);

	if (force) {
		return true;
	}
	if (s_last_phone_request <= 0) {
		return true;
	}
	return (now - s_last_phone_request) >= SOLAR_PHONE_REQUEST_MIN_SEC;
}

static void request_solar_from_phone(void) {
	if (s_solar_request_pending) {
		return;
	}

	if (!phone_request_allowed(false)) {
		return;
	}

	DictionaryIterator *iter;
	AppMessageResult result = app_message_outbox_begin(&iter);
	if (result != APP_MSG_OK) {
		return;
	}

	dict_write_int32(iter, MESSAGE_KEY_REQUEST_SOLAR, 1);
	dict_write_int32(iter, MESSAGE_KEY_UsePhoneGPS, settings_use_phone_gps() ? 1 : 0);
	dict_write_int32(iter, MESSAGE_KEY_LatitudeE6, settings_lat_e6());
	dict_write_int32(iter, MESSAGE_KEY_LongitudeE6, settings_lon_e6());
	dict_write_int32(iter, MESSAGE_KEY_AltitudeM, settings_altitude_m());
	dict_write_int32(iter, MESSAGE_KEY_TimezoneOffsetM, settings_tz_offset_min());
	result = app_message_outbox_send();

	if (result == APP_MSG_OK) {
		s_solar_request_pending = true;
		s_last_phone_request = time(NULL);
		debug_profile_count(DBG_SOLAR_PHONE_REQ);
	}
}

void view_solar_maybe_refresh(void) {
	time_t now = time(NULL);

	debug_profile_count(DBG_SOLAR_MAYBE_REFRESH);

	if (!settings_solar_display_enabled()) {
		return;
	}

	if (timeline_user_is_sleeping()) {
		return;
	}

	if (s_solar_timer) {
		return;
	}

	const bool event_passed = solar_event_passed(now);
	const bool hourly_due = s_last_solar_calc <= 0
		|| (now - s_last_solar_calc) >= SOLAR_REFRESH_INTERVAL_SEC;

	if (!event_passed && !hourly_due) {
		return;
	}

	schedule_solar_calc(NULL);

	if (settings_use_phone_gps()) {
		request_solar_from_phone();
	}
}

static bool s_last_gps;
static int32_t s_last_lat_e6;
static int32_t s_last_lon_e6;
static int32_t s_last_altitude_m;
static bool s_last_show_sun;
static bool s_last_show_moon;

static bool solar_inputs_changed(void) {
	return settings_use_phone_gps() != s_last_gps
		|| settings_lat_e6() != s_last_lat_e6
		|| settings_lon_e6() != s_last_lon_e6
		|| settings_altitude_m() != s_last_altitude_m
		|| settings_show_sun() != s_last_show_sun
		|| settings_show_moon() != s_last_show_moon;
}

static void remember_solar_inputs(void) {
	s_last_gps = settings_use_phone_gps();
	s_last_lat_e6 = settings_lat_e6();
	s_last_lon_e6 = settings_lon_e6();
	s_last_altitude_m = settings_altitude_m();
	s_last_show_sun = settings_show_sun();
	s_last_show_moon = settings_show_moon();
}

void view_solar_on_settings_changed(void) {
	if (!solar_inputs_changed()) {
		return;
	}

	remember_solar_inputs();
	s_last_phone_request = 0;
	force_solar_recalc();
}

void view_solar_on_connection(bool connected) {
	if (connected && !timeline_user_is_sleeping()) {
		s_solar_request_pending = false;
		request_solar_from_phone();
	}
}

void view_solar_startup(void) {
	if (settings_solar_display_enabled() && !timeline_user_is_sleeping()) {
		force_solar_recalc();
	}
	if (!timeline_user_is_sleeping()) {
		request_solar_from_phone();
	}
}

void view_solar_schedule_retry(void *data) {
	(void) data;
	if (timeline_user_is_sleeping()) {
		return;
	}

	if (settings_lat_e6() == 0 && settings_lon_e6() == 0) {
		s_solar_request_pending = false;
		request_solar_from_phone();
		return;
	}

	if (settings_next_sunrise() <= 0 && settings_next_sunset() <= 0 && settings_solar_display_enabled()) {
		force_solar_recalc();
	}
}

void view_solar_clear_request_pending(void) {
	s_solar_request_pending = false;
}
