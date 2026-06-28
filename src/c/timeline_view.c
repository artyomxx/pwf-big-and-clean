#include "timeline_view.h"
#include "timeline_wake.h"
#include "config.h"

static TimelineSettings s_settings;
static int32_t s_tz_offset_min;
static int s_gap_above_time = TIMELINE_GAP_ABOVE_TIME;
static int s_gap_below_time = TIMELINE_GAP_BELOW_TIME;

static Layer *s_block_layer;
static Layer *s_line_layer;
static TextLayer *s_wake_label_layer;
static TextLayer *s_bed_label_layer;
static TimelineLayoutCallback s_layout_callback;

static int32_t awake_minutes(void) {
	return timeline_wake_awake_minutes(&s_settings);
}

static bool wake_valid_now(void) {
	TimelineWakeSnapshot wake;
	timeline_wake_snapshot(&wake);
	return wake.wake_valid;
}

static time_t wake_utc_now(void) {
	TimelineWakeSnapshot wake;
	timeline_wake_snapshot(&wake);
	return wake.wake_utc;
}

static void fill_dim_rect(GContext *ctx, GRect rect) {
#ifdef PBL_COLOR
	graphics_context_set_fill_color(ctx, GColorDarkGray);
	graphics_fill_rect(ctx, rect, 0, GCornerNone);
#else
	graphics_context_set_stroke_color(ctx, GColorWhite);
	for (int y = rect.origin.y; y < rect.origin.y + rect.size.h; y++) {
		for (int x = rect.origin.x; x < rect.origin.x + rect.size.w; x++) {
			if (((x - rect.origin.x) + (y - rect.origin.y)) % 2 == 0) {
				graphics_draw_pixel(ctx, GPoint(x, y));
			}
		}
	}
#endif
}

static void fill_bright_rect(GContext *ctx, GRect rect) {
	graphics_context_set_fill_color(ctx, GColorWhite);
	graphics_fill_rect(ctx, rect, 0, GCornerNone);
}

static void line_layer_update(Layer *layer, GContext *ctx) {
	if (!s_settings.show) {
		return;
	}

	TimelineWakeSnapshot wake;
	timeline_wake_snapshot(&wake);
	if (!wake.wake_valid) {
		return;
	}

	GRect bounds = layer_get_bounds(layer);
	const int line_w = bounds.size.w * TIMELINE_LINE_FRAC / 100;
	const int line_x = (bounds.size.w - line_w) / 2;
	const int line_y = (bounds.size.h - TIMELINE_LINE_H) / 2;

	const int32_t awake_min = awake_minutes();
	int32_t wake_min = s_settings.wake_duration_min;
	int32_t wind_min = s_settings.wind_down_min;
	int32_t main_min = awake_min - wake_min - wind_min;
	if (main_min < 0) {
		main_min = 0;
	}

	int w_wake = line_w * wake_min / awake_min;
	int w_wind = line_w * wind_min / awake_min;
	int w_main = line_w - w_wake - w_wind;
	if (w_main < 0) {
		w_main = 0;
	}

	fill_dim_rect(ctx, GRect(line_x, line_y, w_wake, TIMELINE_LINE_H));
	fill_bright_rect(ctx, GRect(line_x + w_wake, line_y, w_main, TIMELINE_LINE_H));
	fill_dim_rect(ctx, GRect(line_x + w_wake + w_main, line_y, w_wind, TIMELINE_LINE_H));

	time_t now = time(NULL);
	int32_t elapsed_sec = (int32_t) (now - wake.wake_utc);
	int32_t total_sec = awake_min * 60;
	int marker_x = line_x;
	if (total_sec > 0) {
		marker_x = line_x + (line_w * elapsed_sec) / total_sec;
	}
	if (marker_x < line_x) {
		marker_x = line_x;
	}
	if (marker_x > line_x + line_w) {
		marker_x = line_x + line_w;
	}

	const int marker_y = line_y - (TIMELINE_MARKER_H - TIMELINE_LINE_H) / 2;
	fill_bright_rect(ctx, GRect(marker_x, marker_y, 1, TIMELINE_MARKER_H));
}

void timeline_view_update_labels(void) {
	if (!s_wake_label_layer || !s_bed_label_layer) {
		return;
	}

	static char wake_buffer[8];
	static char bed_buffer[8];

	if (!s_settings.show || !s_settings.show_labels || !wake_valid_now()) {
		text_layer_set_text(s_wake_label_layer, "");
		text_layer_set_text(s_bed_label_layer, "");
		layer_set_hidden(text_layer_get_layer(s_wake_label_layer), true);
		layer_set_hidden(text_layer_get_layer(s_bed_label_layer), true);
		return;
	}

	const time_t wake_utc = wake_utc_now();
	time_t bed_utc = wake_utc + (time_t) awake_minutes() * 60;
	timeline_wake_format_local(wake_utc, s_tz_offset_min, wake_buffer, sizeof(wake_buffer));
	timeline_wake_format_local(bed_utc, s_tz_offset_min, bed_buffer, sizeof(bed_buffer));
	text_layer_set_text(s_wake_label_layer, wake_buffer);
	text_layer_set_text(s_bed_label_layer, bed_buffer);
	layer_set_hidden(text_layer_get_layer(s_wake_label_layer), false);
	layer_set_hidden(text_layer_get_layer(s_bed_label_layer), false);
}

static int timeline_label_section_height(void) {
	return TIMELINE_LABEL_H + TIMELINE_LABEL_LINE_GAP;
}

static bool timeline_labels_reserve_space(TimelineAnchorMode anchor) {
	if (anchor == TimelineAnchorAboveTime) {
		return true;
	}
	return s_settings.show_labels && wake_valid_now();
}

static int timeline_compact_top_gap(TimelineAnchorMode anchor) {
	if (anchor == TimelineAnchorBelowTime && !timeline_labels_reserve_space(anchor)) {
		return TIMELINE_COMPACT_TOP_GAP;
	}
	return 0;
}

int timeline_layout_height(TimelineAnchorMode anchor) {
	int height = TIMELINE_MARKER_H + timeline_compact_top_gap(anchor);

	if (timeline_labels_reserve_space(anchor)) {
		height += timeline_label_section_height();
	}
	return height;
}

int timeline_stack_extent(TimelineAnchorMode anchor) {
	if (anchor == TimelineAnchorAboveTime) {
		return timeline_layout_height(anchor) + s_gap_above_time;
	}
	return s_gap_below_time + timeline_layout_height(anchor);
}

bool timeline_layout_active(void) {
	return s_settings.show && wake_valid_now() && s_block_layer;
}

int timeline_default_gap_above_time(void) {
	return TIMELINE_GAP_ABOVE_TIME;
}

int timeline_default_gap_below_time(void) {
	return TIMELINE_GAP_BELOW_TIME;
}

void timeline_set_time_gaps(int above_time_gap, int below_time_gap) {
	s_gap_above_time = above_time_gap;
	s_gap_below_time = below_time_gap;
}

int timeline_gap_below_time(void) {
	return s_gap_below_time;
}

static void timeline_layout_internal_layers(int block_w, TimelineAnchorMode anchor) {
	int line_y = timeline_compact_top_gap(anchor);

	if (timeline_labels_reserve_space(anchor)) {
		line_y = timeline_label_section_height();
	}

	if (s_line_layer) {
		layer_set_frame(s_line_layer, GRect(0, line_y, block_w, TIMELINE_MARKER_H));
	}
}

void timeline_set_layout_anchor(TimelineAnchorMode anchor, int time_edge_y) {
	if (!s_block_layer) {
		return;
	}

	const int block_h = timeline_layout_height(anchor);
	GRect frame = layer_get_frame(s_block_layer);

	frame.size.h = block_h;
	if (anchor == TimelineAnchorAboveTime) {
		frame.origin.y = time_edge_y - s_gap_above_time - block_h;
	} else {
		frame.origin.y = time_edge_y + s_gap_below_time;
	}
	layer_set_frame(s_block_layer, frame);
	timeline_layout_internal_layers(frame.size.w, anchor);
}

void timeline_set_block_top(int top_y) {
	if (!s_block_layer) {
		return;
	}

	const int block_h = timeline_layout_height(TimelineAnchorBelowTime);
	GRect frame = layer_get_frame(s_block_layer);

	frame.origin.y = top_y;
	frame.size.h = block_h;
	layer_set_frame(s_block_layer, frame);
	timeline_layout_internal_layers(frame.size.w, TimelineAnchorBelowTime);
}

void timeline_view_set_layout_callback(TimelineLayoutCallback callback) {
	s_layout_callback = callback;
}

Layer *timeline_view_create(Layer *parent, GRect screen_bounds, int above_time_y) {
	

	const int line_w = screen_bounds.size.w * TIMELINE_LINE_FRAC / 100;
	const int line_x = (screen_bounds.size.w - line_w) / 2;
	const int block_h = timeline_layout_height(TimelineAnchorAboveTime);
	const int block_y = above_time_y - block_h;

	s_block_layer = layer_create(GRect(0, block_y, screen_bounds.size.w, block_h));
	layer_add_child(parent, s_block_layer);

	GFont label_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);

	s_wake_label_layer = text_layer_create(GRect(line_x, 0, line_w / 2, TIMELINE_LABEL_H));
	text_layer_set_background_color(s_wake_label_layer, GColorClear);
	text_layer_set_text_color(s_wake_label_layer, GColorWhite);
	text_layer_set_font(s_wake_label_layer, label_font);
	text_layer_set_text_alignment(s_wake_label_layer, GTextAlignmentLeft);
	layer_add_child(s_block_layer, text_layer_get_layer(s_wake_label_layer));

	s_bed_label_layer = text_layer_create(GRect(line_x + line_w / 2, 0, line_w / 2, TIMELINE_LABEL_H));
	text_layer_set_background_color(s_bed_label_layer, GColorClear);
	text_layer_set_text_color(s_bed_label_layer, GColorWhite);
	text_layer_set_font(s_bed_label_layer, label_font);
	text_layer_set_text_alignment(s_bed_label_layer, GTextAlignmentRight);
	layer_add_child(s_block_layer, text_layer_get_layer(s_bed_label_layer));

	const int line_layer_y = TIMELINE_LABEL_H + TIMELINE_LABEL_LINE_GAP;
	s_line_layer = layer_create(GRect(0, line_layer_y, screen_bounds.size.w, TIMELINE_MARKER_H));
	layer_set_update_proc(s_line_layer, line_layer_update);
	layer_add_child(s_block_layer, s_line_layer);

	timeline_view_refresh();
	return s_block_layer;
}

void timeline_view_destroy(void) {
	if (s_wake_label_layer) {
		text_layer_destroy(s_wake_label_layer);
		s_wake_label_layer = NULL;
	}
	if (s_bed_label_layer) {
		text_layer_destroy(s_bed_label_layer);
		s_bed_label_layer = NULL;
	}
	if (s_line_layer) {
		layer_destroy(s_line_layer);
		s_line_layer = NULL;
	}
	if (s_block_layer) {
		layer_destroy(s_block_layer);
		s_block_layer = NULL;
	}
}


void timeline_view_set_settings(const TimelineSettings *settings, int32_t tz_offset_min) {
	if (settings) {
		s_settings = *settings;
	}
	s_tz_offset_min = tz_offset_min;
}

void timeline_view_refresh(void) {
	timeline_view_update_labels();
	if (s_line_layer) {
		layer_mark_dirty(s_line_layer);
	}
	const bool visible = s_settings.show;
	TimelineWakeSnapshot wake;
	timeline_wake_snapshot(&wake);
	layer_set_hidden(s_block_layer, !(visible && wake.wake_valid));
}

Layer *timeline_view_block_layer(void) {
	return s_block_layer;
}

bool timeline_view_line_ready(void) {
	return s_line_layer != NULL;
}

void timeline_view_mark_line_dirty(void) {
	if (s_line_layer) {
		layer_mark_dirty(s_line_layer);
	}
}

