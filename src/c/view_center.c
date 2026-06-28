#include "view_center.h"

#include "config.h"
#include "settings.h"
#include "timeline.h"
#include "debug_profile.h"

#include <stdio.h>
#include <time.h>

static Window *s_window;
static TextLayer *s_time_layer;
static TextLayer *s_date_layer;
static int s_time_row_h;
static int s_last_date_mday = -1;
static int s_last_date_mon = -1;
static int s_last_date_format = -1;
static bool s_last_human_era_year;

typedef struct {
	int16_t screen_h;
	int16_t screen_w;
	bool show_time;
	bool show_date;
	bool date_above_time;
	bool show_labels;
	bool timeline_active;
} CenterLayoutSig;

static CenterLayoutSig s_layout_sig;
static bool s_layout_sig_valid;

void view_center_invalidate_layout(void) {
	s_layout_sig_valid = false;
	s_last_date_mday = -1;
	s_last_date_mon = -1;
}

static CenterLayoutSig current_layout_sig(GRect bounds) {
	CenterLayoutSig sig;

	sig.screen_h = bounds.size.h;
	sig.screen_w = bounds.size.w;
	sig.show_time = settings_show_time();
	sig.show_date = settings_show_date();
	sig.date_above_time = settings_date_above_time();
	sig.show_labels = settings_show_labels();
	sig.timeline_active = timeline_layout_active();
	return sig;
}

static bool layout_sig_equal(const CenterLayoutSig *a, const CenterLayoutSig *b) {
	return a->screen_h == b->screen_h
		&& a->screen_w == b->screen_w
		&& a->show_time == b->show_time
		&& a->show_date == b->show_date
		&& a->date_above_time == b->date_above_time
		&& a->show_labels == b->show_labels
		&& a->timeline_active == b->timeline_active;
}

static void sync_center_time_gaps(void) {
	timeline_set_time_gaps(
		settings_show_labels() ? timeline_default_gap_above_time() : CENTER_GAP_ABOVE_TIME_NO_LABELS,
		settings_show_labels() ? timeline_default_gap_below_time() : CENTER_GAP_BELOW_TIME_NO_LABELS);
}

static int center_gap_above_time(void) {
	if (!settings_show_labels()) {
		return CENTER_GAP_ABOVE_TIME_NO_LABELS;
	}
	return DATE_GAP_ABOVE_TIME;
}

static int center_gap_below_time(void) {
	if (!settings_show_labels()) {
		return CENTER_GAP_BELOW_TIME_NO_LABELS;
	}
	return DATE_GAP_BELOW_TIME;
}

static int date_gap_below_anchor(void) {
	if (!settings_show_time()) {
		return DATE_GAP_WITHOUT_TIME;
	}
	return center_gap_below_time();
}

static int gap_after_date_swapped(void) {
	// When time is hidden, date and timeline share the vertical center.
	if (settings_show_time()) {
		return center_gap_above_time();
	}
	if (timeline_layout_active()) {
		return DATE_GAP_WITHOUT_TIME;
	}
	return 0;
}

static int center_block_height_no_time(void) {
	if (settings_date_above_time()) {
		int height = 0;
		if (settings_show_date()) {
			height += DATE_ROW_H + gap_after_date_swapped();
		}
		if (timeline_layout_active()) {
			height += timeline_stack_extent(TimelineAnchorBelowTime);
		}
		return height;
	}

	int height = 0;
	if (timeline_layout_active()) {
		height += timeline_stack_extent(TimelineAnchorAboveTime);
	}
	if (settings_show_date()) {
		height += date_gap_below_anchor() + DATE_ROW_H;
	}
	return height;
}

static void layout_around_fixed_time(GRect bounds, int time_y) {
	layer_set_frame(text_layer_get_layer(s_time_layer), GRect(0, time_y, bounds.size.w, s_time_row_h));

	if (settings_date_above_time()) {
		if (settings_show_date()) {
			const int date_y = time_y - DATE_ROW_H - center_gap_above_time();
			layer_set_frame(text_layer_get_layer(s_date_layer), GRect(0, date_y, bounds.size.w, DATE_ROW_H));
		}
		if (timeline_layout_active()) {
			timeline_set_layout_anchor(TimelineAnchorBelowTime, time_y + s_time_row_h);
		}
		return;
	}

	if (timeline_layout_active()) {
		timeline_set_layout_anchor(TimelineAnchorAboveTime, time_y);
	}
	if (settings_show_date()) {
		const int date_y = time_y + s_time_row_h + date_gap_below_anchor();
		layer_set_frame(text_layer_get_layer(s_date_layer), GRect(0, date_y, bounds.size.w, DATE_ROW_H));
	}
}

static void layout_without_time(GRect bounds) {
	const int block_height = center_block_height_no_time();
	int y = (bounds.size.h - block_height) / 2 + MAIN_TIME_Y_OFFSET;

	if (settings_date_above_time()) {
		if (settings_show_date()) {
			layer_set_frame(text_layer_get_layer(s_date_layer), GRect(0, y, bounds.size.w, DATE_ROW_H));
			y += DATE_ROW_H + gap_after_date_swapped();
		} else if (timeline_layout_active()) {
			y += timeline_gap_below_time();
		}
		if (timeline_layout_active()) {
			timeline_set_block_top(y);
		}
		return;
	}

	if (timeline_layout_active()) {
		const int edge_y = y + timeline_stack_extent(TimelineAnchorAboveTime);
		timeline_set_layout_anchor(TimelineAnchorAboveTime, edge_y);
		y = edge_y;
	}
	if (settings_show_date()) {
		const int date_y = y + date_gap_below_anchor();
		layer_set_frame(text_layer_get_layer(s_date_layer), GRect(0, date_y, bounds.size.w, DATE_ROW_H));
	}
}

void view_center_apply_layout(void) {
	// Deferred while Clay settings batch-apply to avoid date/timeline flicker.
	if (settings_defer_center_layout() || !s_time_layer || !s_window) {
		return;
	}

	Layer *window_layer = window_get_root_layer(s_window);
	GRect bounds = layer_get_bounds(window_layer);
	const CenterLayoutSig sig = current_layout_sig(bounds);

	if (s_layout_sig_valid && layout_sig_equal(&sig, &s_layout_sig)) {
		debug_profile_count(DBG_CENTER_LAYOUT_CACHED);
		return;
	}

	debug_profile_count(DBG_CENTER_LAYOUT);
	sync_center_time_gaps();

	if (settings_show_time()) {
		const int time_y = (bounds.size.h - s_time_row_h) / 2 + MAIN_TIME_Y_OFFSET;
		layout_around_fixed_time(bounds, time_y);
	} else {
		layout_without_time(bounds);
	}

	s_layout_sig = sig;
	s_layout_sig_valid = true;
}

void view_center_create(Window *window) {
	s_window = window;
	Layer *window_layer = window_get_root_layer(window);
	GRect bounds = layer_get_bounds(window_layer);
	GFont font_battery = fonts_get_system_font(FONT_KEY_BATTERY);

#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO) || defined(PBL_PLATFORM_FLINT)
	s_time_row_h = 72;
	GFont time_font = fonts_get_system_font(FONT_KEY_LECO_60_NUMBERS_AM_PM);
#else
	s_time_row_h = 50;
	GFont time_font = fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
#endif

	timeline_set_layout_callback(view_center_apply_layout);
	timeline_create(window_layer, bounds, 0);

	s_time_layer = text_layer_create(GRect(0, 0, bounds.size.w, s_time_row_h));
	text_layer_set_background_color(s_time_layer, GColorClear);
	text_layer_set_text_color(s_time_layer, GColorWhite);
	text_layer_set_text(s_time_layer, "--:--");
	text_layer_set_font(s_time_layer, time_font);
	text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
	layer_add_child(window_layer, text_layer_get_layer(s_time_layer));

	s_date_layer = text_layer_create(GRect(0, 0, bounds.size.w, DATE_ROW_H));
	text_layer_set_background_color(s_date_layer, GColorClear);
	text_layer_set_text_color(s_date_layer, GColorWhite);
	text_layer_set_text(s_date_layer, "--");
	text_layer_set_font(s_date_layer, font_battery);
	text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
	layer_add_child(window_layer, text_layer_get_layer(s_date_layer));
}

void view_center_destroy(void) {
	text_layer_destroy(s_date_layer);
	text_layer_destroy(s_time_layer);
	timeline_destroy();
}

void view_center_apply_visibility(void) {
	if (s_time_layer) {
		layer_set_hidden(text_layer_get_layer(s_time_layer), !settings_show_time());
	}
	if (s_date_layer) {
		layer_set_hidden(text_layer_get_layer(s_date_layer), !settings_show_date());
	}
}

void view_center_update_time(void) {
	time_t now = time(NULL);
	struct tm *tick_time = localtime(&now);
	static char time_buffer[8];

	if (!tick_time) {
		snprintf(time_buffer, sizeof(time_buffer), "--:--");
	} else {
		strftime(time_buffer, sizeof(time_buffer), "%H:%M", tick_time);
	}

	text_layer_set_text(s_time_layer, time_buffer);
}

static int date_display_year(const struct tm *tick_time) {
	int year = tick_time->tm_year + 1900;
	if (settings_human_era_year()) {
		year += 10000;
	}
	return year;
}

void view_center_update_date(void) {
	if (!s_date_layer || !settings_show_date()) {
		return;
	}

	time_t now = time(NULL);
	struct tm *tick_time = localtime(&now);

	const int format = settings_date_format();
	const bool human_era = settings_human_era_year();

	// Skip redraw when day/month/format/human-era unchanged.
	if (tick_time
			&& tick_time->tm_mday == s_last_date_mday
			&& tick_time->tm_mon == s_last_date_mon
			&& format == s_last_date_format
			&& human_era == s_last_human_era_year) {
		return;
	}
	static char date_buffer[32];
	static const char *const weekday_abbr[] = {
		"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT",
	};
	static const char *const weekday_full[] = {
		"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY",
	};
	static const char *const month_abbr[] = {
		"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
		"JUL", "AUG", "SEP", "OCT", "NOV", "DEC",
	};
	static const char *const month_full[] = {
		"JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE",
		"JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER",
	};

	if (!tick_time) {
		snprintf(date_buffer, sizeof(date_buffer), "--");
		text_layer_set_text(s_date_layer, date_buffer);
		return;
	}

	const int wday = tick_time->tm_wday;
	const int mon = tick_time->tm_mon;
	const int month = mon + 1;
	const int day = tick_time->tm_mday;
	const int year = date_display_year(tick_time);
	int display_format = format;

	if (display_format < 0 || display_format > DATE_FORMAT_LAST) {
		display_format = DateFormatWeekdayOnly;
	}

	switch (display_format) {
		case DateFormatAlwaysFriday:
			snprintf(date_buffer, sizeof(date_buffer), "FRIDAY");
			break;
		case DateFormatFullWeekdayDMYFullMonth:
			snprintf(date_buffer, sizeof(date_buffer), "%s %d %s",
				weekday_full[wday], day, month_full[mon]);
			break;
		case DateFormatShortWeekdayDMYFullMonth:
			snprintf(date_buffer, sizeof(date_buffer), "%s %d %s",
				weekday_abbr[wday], day, month_full[mon]);
			break;
		case DateFormatShortWeekdayDMYAbbrYear:
			snprintf(date_buffer, sizeof(date_buffer), "%s %d %s %d",
				weekday_abbr[wday], day, month_abbr[mon], year);
			break;
		case DateFormatDMYAbbrYear:
			snprintf(date_buffer, sizeof(date_buffer), "%d %s %d",
				day, month_abbr[mon], year);
			break;
		case DateFormatDMYFullMonth:
			snprintf(date_buffer, sizeof(date_buffer), "%d %s", day, month_full[mon]);
			break;
		case DateFormatISOSlashWeekdaySuffix:
			snprintf(date_buffer, sizeof(date_buffer), "%d/%02d/%02d %s",
				year, month, day, weekday_abbr[wday]);
			break;
		case DateFormatISOSlash:
			snprintf(date_buffer, sizeof(date_buffer), "%d/%02d/%02d", year, month, day);
			break;
		case DateFormatISOSpaceWeekdaySuffix:
			snprintf(date_buffer, sizeof(date_buffer), "%d %02d %02d %s",
				year, month, day, weekday_abbr[wday]);
			break;
		case DateFormatISOSpace:
			snprintf(date_buffer, sizeof(date_buffer), "%d %02d %02d", year, month, day);
			break;
		default:
			snprintf(date_buffer, sizeof(date_buffer), "%s", weekday_full[wday]);
			break;
	}

	if (tick_time) {
		s_last_date_mday = tick_time->tm_mday;
		s_last_date_mon = tick_time->tm_mon;
		s_last_date_format = format;
		s_last_human_era_year = human_era;
	}

	text_layer_set_text(s_date_layer, date_buffer);
}
