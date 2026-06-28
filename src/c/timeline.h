#pragma once

#include "config.h"

#include <pebble.h>

void timeline_bootstrap(int32_t tz_offset_min, const TimelineSettings *settings);
void timeline_settings_apply(const TimelineSettings *settings);
void timeline_set_tz_offset(int32_t tz_offset_min);
bool timeline_user_is_sleeping(void);

typedef void (*TimelineLayoutCallback)(void);

typedef enum {
	TimelineAnchorAboveTime,
	TimelineAnchorBelowTime,
} TimelineAnchorMode;

int timeline_layout_height(TimelineAnchorMode anchor);
int timeline_stack_extent(TimelineAnchorMode anchor);
bool timeline_layout_active(void);
int timeline_default_gap_above_time(void);
int timeline_default_gap_below_time(void);
void timeline_set_time_gaps(int above_time_gap, int below_time_gap);
int timeline_gap_below_time(void);
void timeline_set_layout_anchor(TimelineAnchorMode anchor, int time_edge_y);
void timeline_set_block_top(int top_y);
void timeline_set_layout_callback(TimelineLayoutCallback callback);

Layer *timeline_create(Layer *parent, GRect screen_bounds, int above_time_y);
void timeline_destroy(void);

void timeline_on_health_event(HealthEventType event);
void timeline_tick(void);
