#pragma once

#include <pebble.h>

void view_health_create(Layer *parent, GRect bounds);
void view_health_destroy(void);
void view_health_apply_visibility(void);
void view_health_update(void);
void view_health_update_steps(void);
void view_health_update_hr_peek_throttled(void);
