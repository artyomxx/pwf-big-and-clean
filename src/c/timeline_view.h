#pragma once

#include "config.h"
#include "timeline.h"

#include <pebble.h>

#define TIMELINE_LINE_FRAC 80
#define TIMELINE_LINE_H 2
#define TIMELINE_MARKER_H 10
#define TIMELINE_LABEL_H 14
#define TIMELINE_LABEL_LINE_GAP 4
#define TIMELINE_GAP_ABOVE_TIME -4
#define TIMELINE_GAP_BELOW_TIME -8
#define TIMELINE_COMPACT_TOP_GAP 8

Layer *timeline_view_create(Layer *parent, GRect screen_bounds, int above_time_y);
void timeline_view_destroy(void);
void timeline_view_set_layout_callback(TimelineLayoutCallback callback);
void timeline_view_set_settings(const TimelineSettings *settings, int32_t tz_offset_min);
void timeline_view_refresh(void);
void timeline_view_update_labels(void);
void timeline_view_mark_line_dirty(void);
bool timeline_view_line_ready(void);
Layer *timeline_view_block_layer(void);
