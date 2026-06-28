#include "timeline.h"
#include "timeline_wake.h"
#include "timeline_view.h"
#include "config.h"
#include "debug_profile.h"

#include <string.h>
#include <time.h>

static TimelineSettings s_settings = {
	.show = false,
	.show_labels = true,
	.sleep_hours_x10 = TIMELINE_DEFAULT_SLEEP_HOURS_X10,
	.wake_from_sleep = TIMELINE_DEFAULT_WAKE_FROM_SLEEP,
	.manual_wake_min = TIMELINE_DEFAULT_MANUAL_WAKE_MIN,
	.wake_duration_min = TIMELINE_DEFAULT_WAKE_DURATION_MIN,
	.wind_down_min = TIMELINE_DEFAULT_WIND_DOWN_MIN,
};

static int32_t s_tz_offset_min;
static time_t s_last_logic_utc;
static time_t s_last_line_dirty_utc;
static TimelineLayoutCallback s_layout_callback;

static void on_wake_changed(void) {
	s_last_line_dirty_utc = 0;
	timeline_view_refresh();
}

static void refresh_display(void) {
	if (!timeline_view_block_layer()) {
		return;
	}

	debug_profile_count(DBG_TIMELINE_REFRESH);

	const bool was_visible = !layer_get_hidden(timeline_view_block_layer());
	const bool prev_show = s_settings.show;
	const bool prev_show_labels = s_settings.show_labels;

	timeline_wake_run();
	timeline_view_update_labels();
	timeline_view_mark_line_dirty();

	TimelineWakeSnapshot wake;
	timeline_wake_snapshot(&wake);
	const bool now_visible = s_settings.show && wake.wake_valid;
	layer_set_hidden(timeline_view_block_layer(), !now_visible);

	const bool need_layout = was_visible != now_visible
		|| prev_show != s_settings.show
		|| prev_show_labels != s_settings.show_labels;

	if (need_layout && s_layout_callback) {
		debug_profile_count(DBG_TIMELINE_LAYOUT_CB);
		s_layout_callback();
	}
}

void timeline_bootstrap(int32_t tz_offset_min, const TimelineSettings *settings) {
	if (settings) {
		s_settings = *settings;
	}

	const TimelineWakeHooks hooks = {
		.on_wake_changed = on_wake_changed,
	};
	timeline_wake_set_hooks(&hooks);
	timeline_wake_bootstrap(tz_offset_min, &s_settings);
	timeline_wake_persist_load();
	s_tz_offset_min = tz_offset_min;
	timeline_view_set_settings(&s_settings, tz_offset_min);
}

void timeline_settings_apply(const TimelineSettings *settings) {
	if (!settings) {
		return;
	}

	if (memcmp(&s_settings, settings, sizeof(TimelineSettings)) == 0) {
		return;
	}

	const bool wake_relevant = timeline_wake_settings_relevant_diff(&s_settings, settings);
	s_settings = *settings;
	timeline_wake_set_settings(&s_settings);
	timeline_view_set_settings(&s_settings, s_tz_offset_min);

	if (wake_relevant) {
		timeline_wake_invalidate();
		s_last_logic_utc = 0;
		s_last_line_dirty_utc = 0;
	}

	refresh_display();
}

void timeline_set_tz_offset(int32_t tz_offset_min) {
	s_tz_offset_min = tz_offset_min;
	timeline_wake_set_tz_offset(tz_offset_min);
	timeline_view_set_settings(&s_settings, tz_offset_min);
	timeline_wake_invalidate();
	s_last_logic_utc = 0;
	s_last_line_dirty_utc = 0;
	refresh_display();
}

bool timeline_user_is_sleeping(void) {
	return timeline_wake_user_is_sleeping();
}

void timeline_set_layout_callback(TimelineLayoutCallback callback) {
	s_layout_callback = callback;
	timeline_view_set_layout_callback(callback);
}

void timeline_on_health_event(HealthEventType event) {
	if (!s_settings.show) {
		return;
	}

	if (event != HealthEventSleepUpdate && timeline_wake_user_is_sleeping()) {
		return;
	}

	if (event == HealthEventSleepUpdate) {
		timeline_wake_on_sleep_update();
	}
}

void timeline_tick(void) {
	if (!s_settings.show || !timeline_view_line_ready()) {
		return;
	}

	const time_t now = time(NULL);

	if (s_last_logic_utc == 0
			|| (now - s_last_logic_utc) >= (time_t) TIMELINE_LOGIC_INTERVAL_MIN * 60) {
		s_last_logic_utc = now;
		timeline_wake_run();
	}

	if (!timeline_wake_marker_updates_allowed()) {
		return;
	}

	if (TIMELINE_REDRAW_INTERVAL_MIN > 1 && s_last_line_dirty_utc > 0
			&& (now - s_last_line_dirty_utc) < (time_t) TIMELINE_REDRAW_INTERVAL_MIN * 60) {
		return;
	}

	s_last_line_dirty_utc = now;
	debug_profile_count(DBG_TIMELINE_DIRTY);
	timeline_view_mark_line_dirty();
}

Layer *timeline_create(Layer *parent, GRect screen_bounds, int above_time_y) {
	return timeline_view_create(parent, screen_bounds, above_time_y);
}

void timeline_destroy(void) {
	timeline_view_destroy();
}
