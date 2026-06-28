#pragma once

#include <pebble.h>

void view_solar_create(Layer *parent, GRect bounds);
void view_solar_destroy(void);
void view_solar_apply_visibility(void);
void view_solar_update_display(void);
void view_solar_update_battery(void);
void view_solar_on_battery_change(BatteryChargeState state);
void view_solar_on_settings_changed(void);
void view_solar_maybe_refresh(void);
void view_solar_on_connection(bool connected);
void view_solar_startup(void);
void view_solar_schedule_retry(void *data);
void view_solar_clear_request_pending(void);
