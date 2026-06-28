#pragma once

#include <pebble.h>

void view_center_create(Window *window);
void view_center_destroy(void);
void view_center_apply_visibility(void);
void view_center_invalidate_layout(void);
void view_center_apply_layout(void);
void view_center_update_time(void);
void view_center_update_date(void);
