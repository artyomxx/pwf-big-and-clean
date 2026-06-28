#include "view_health.h"

#include "config.h"
#include "settings.h"
#include "util.h"

#include <pebble.h>
#include <stdio.h>

#define HEALTH_HR_HISTORY_CHUNK 30
#define HEALTH_HR_LOOKBACK_SEC (30 * 60)
#define HEALTH_HR_UI_MIN_INTERVAL_SEC 30

// HeartRateUpdate can fire often; peek only + throttle avoids 30-min history scans.
static Layer *s_block;
static TextLayer *s_steps_value;
static TextLayer *s_steps_label;
static TextLayer *s_hr_value;
static TextLayer *s_hr_label;
static time_t s_last_hr_ui_utc;

static HealthValue read_heart_rate_peek(void) {
#if defined(_PBL_API_EXISTS_health_service_peek_current_value)
	HealthValue hr = health_service_peek_current_value(HealthMetricHeartRateBPM);
	if (hr > 0) {
		return hr;
	}

	hr = health_service_peek_current_value(HealthMetricHeartRateRawBPM);
	if (hr > 0) {
		return hr;
	}
#endif

	return 0;
}

static HealthValue read_heart_rate_bpm(void) {
	const HealthValue peek = read_heart_rate_peek();
	if (peek > 0) {
		return peek;
	}

	const time_t now = time(NULL);

#if defined(_PBL_API_EXISTS_health_service_get_minute_history)
	HealthMinuteData history[HEALTH_HR_HISTORY_CHUNK];
	time_t time_start = now - HEALTH_HR_LOOKBACK_SEC;
	time_t time_end = now;
	const uint32_t count = health_service_get_minute_history(
		history, HEALTH_HR_HISTORY_CHUNK, &time_start, &time_end);

	for (int32_t i = (int32_t) count - 1; i >= 0; i--) {
		if (!history[i].is_invalid && history[i].heart_rate_bpm > 0) {
			return history[i].heart_rate_bpm;
		}
	}
#endif

#if defined(_PBL_API_EXISTS_health_service_aggregate_averaged)
	const HealthValue avg = health_service_aggregate_averaged(
		HealthMetricHeartRateBPM,
		now - HEALTH_HR_LOOKBACK_SEC, now,
		HealthAggregationAvg,
		HealthServiceTimeScopeOnce);
	if (avg > 0) {
		return avg;
	}
#endif

	return 0;
}

static void view_health_set_hr_text(HealthValue hr) {
	static char hr_buffer[16];

	if (hr > 0) {
		snprintf(hr_buffer, sizeof(hr_buffer), "%d", (int) hr);
	} else {
		snprintf(hr_buffer, sizeof(hr_buffer), "--");
	}
	text_layer_set_text(s_hr_value, hr_buffer);
}

void view_health_create(Layer *parent, GRect bounds) {
	GFont font_label = fonts_get_system_font(FONT_KEY_LABEL);
	GFont font_value = fonts_get_system_font(FONT_KEY_VALUE);
	const int half_w = bounds.size.w / 2;

	s_last_hr_ui_utc = 0;

	s_block = layer_create(GRect(0, HEALTH_TOP_MARGIN, bounds.size.w, HEALTH_BLOCK_H));
	layer_add_child(parent, s_block);

	s_steps_value = util_create_text_layer(
		s_block,
		GRect(HEALTH_SIDE_MARGIN, 0, half_w - HEALTH_SIDE_MARGIN, HEALTH_VALUE_H),
		GTextAlignmentLeft, font_value);
	text_layer_set_text(s_steps_value, "--");
	s_steps_label = util_create_text_layer(
		s_block,
		GRect(HEALTH_SIDE_MARGIN, HEALTH_VALUE_H + HEALTH_VALUE_LABEL_GAP,
			half_w - HEALTH_SIDE_MARGIN, HEALTH_LABEL_H),
		GTextAlignmentLeft, font_label);
	text_layer_set_text(s_steps_label, "STEPS");

	s_hr_value = util_create_text_layer(
		s_block,
		GRect(half_w, 0, half_w - HEALTH_SIDE_MARGIN, HEALTH_VALUE_H),
		GTextAlignmentRight, font_value);
	text_layer_set_text(s_hr_value, "--");
	s_hr_label = util_create_text_layer(
		s_block,
		GRect(half_w, HEALTH_VALUE_H + HEALTH_VALUE_LABEL_GAP,
			half_w - HEALTH_SIDE_MARGIN, HEALTH_LABEL_H),
		GTextAlignmentRight, font_label);
	text_layer_set_text(s_hr_label, "BPM");
}

void view_health_destroy(void) {
	text_layer_destroy(s_steps_value);
	text_layer_destroy(s_steps_label);
	text_layer_destroy(s_hr_value);
	text_layer_destroy(s_hr_label);
	layer_destroy(s_block);
}

void view_health_apply_visibility(void) {
	const bool show_health = settings_show_steps() || settings_show_bpm();

	layer_set_hidden(s_block, !show_health);
	layer_set_hidden(text_layer_get_layer(s_steps_value), !settings_show_steps());
	layer_set_hidden(text_layer_get_layer(s_steps_label), !settings_show_steps() || !settings_show_labels());
	layer_set_hidden(text_layer_get_layer(s_hr_value), !settings_show_bpm());
	layer_set_hidden(text_layer_get_layer(s_hr_label), !settings_show_bpm() || !settings_show_labels());
}

void view_health_update_steps(void) {
	if (!settings_show_steps()) {
		return;
	}

	static char steps_buffer[16];
	const HealthValue steps = health_service_sum_today(HealthMetricStepCount);

	snprintf(steps_buffer, sizeof(steps_buffer), "%d", (int) steps);
	text_layer_set_text(s_steps_value, steps_buffer);
}

void view_health_update_hr_peek_throttled(void) {
	if (!settings_show_bpm()) {
		return;
	}

	const time_t now = time(NULL);
	if (s_last_hr_ui_utc > 0
			&& (now - s_last_hr_ui_utc) < (time_t) HEALTH_HR_UI_MIN_INTERVAL_SEC) {
		return;
	}

	const HealthValue hr = read_heart_rate_peek();
	if (hr <= 0) {
		return;
	}

	view_health_set_hr_text(hr);
	s_last_hr_ui_utc = now;
}

void view_health_update(void) {
	if (!settings_show_steps() && !settings_show_bpm()) {
		return;
	}

	view_health_update_steps();

	if (settings_show_bpm()) {
		const time_t now = time(NULL);
		const HealthValue hr = read_heart_rate_bpm();

		view_health_set_hr_text(hr);
		s_last_hr_ui_utc = now;
	}
}
