#include <pebble.h>

#include "settings.h"
#include "timeline.h"
#include "view_center.h"
#include "view_health.h"
#include "view_solar.h"
#include "debug_profile.h"
#include "config.h"

static Window *s_window;
static AppTimer *s_startup_timer;

static void main_window_load(Window *window) {
	Layer *window_layer = window_get_root_layer(window);
	GRect bounds = layer_get_bounds(window_layer);

	view_health_create(window_layer, bounds);
	view_center_create(window);
	view_solar_create(window_layer, bounds);

	view_center_update_time();
	view_center_update_date();
	view_solar_update_battery();
	view_health_update();
	view_solar_update_display();

	view_health_apply_visibility();
	view_solar_apply_visibility();
	view_center_apply_visibility();
	view_center_invalidate_layout();
	view_center_apply_layout();

	if (settings_solar_display_enabled()
			&& settings_next_sunrise() <= 0 && settings_next_sunset() <= 0) {
		view_solar_on_settings_changed();
	}
}

static void main_window_unload(Window *window) {
	(void) window;
	view_health_destroy();
	view_solar_destroy();
	view_center_destroy();
}

static void startup_callback(void *data) {
	(void) data;
	s_startup_timer = NULL;
	view_solar_startup();
}

static void health_handler(HealthEventType event, void *context) {
	(void) context;
	debug_profile_count(DBG_HEALTH_EVENT);

	if (event == HealthEventSleepUpdate) {
		timeline_on_health_event(event);
		view_health_update();
		return;
	}

	if (event == HealthEventSignificantUpdate) {
		view_health_update_steps();
		return;
	}

	if (event == HealthEventHeartRateUpdate) {
		view_health_update_hr_peek_throttled();
		return;
	}
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
	(void) tick_time;
	(void) units_changed;

	debug_profile_count(DBG_TICK);

	view_center_update_time();
	view_center_update_date();
	view_solar_update_battery();
	view_health_update();

	if (timeline_user_is_sleeping()) {
		debug_profile_count(DBG_TICK_SLEEP_SKIP);
		return;
	}

	timeline_tick();
	view_solar_maybe_refresh();
}

static void connection_handler(bool connected) {
	debug_profile_count(DBG_CONNECTION);
	view_solar_on_connection(connected);
}

static void init(void) {
	settings_load_defaults();
	settings_load();
	timeline_bootstrap(settings_tz_offset_min(), settings_timeline());

	s_window = window_create();
	window_set_background_color(s_window, GColorBlack);
	window_set_window_handlers(s_window, (WindowHandlers) {
		.load = main_window_load,
		.unload = main_window_unload,
	});
	window_stack_push(s_window, true);

	settings_init();
	connection_service_subscribe((ConnectionHandlers) {
		.pebble_app_connection_handler = connection_handler,
	});

	tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
	battery_state_service_subscribe(view_solar_on_battery_change);
	health_service_events_subscribe(health_handler, NULL);

	debug_profile_init();
	s_startup_timer = app_timer_register(1000, startup_callback, NULL);
	app_timer_register(3000, view_solar_schedule_retry, NULL);
}

static void deinit(void) {
	if (s_startup_timer) {
		app_timer_cancel(s_startup_timer);
	}
	debug_profile_deinit();
	settings_deinit();
	connection_service_unsubscribe();
	health_service_events_unsubscribe();
	battery_state_service_unsubscribe();
	window_destroy(s_window);
}

int main(void) {
	init();
	app_event_loop();
	deinit();
}
