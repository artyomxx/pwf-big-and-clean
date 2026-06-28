#pragma once

#include <time.h>

time_t solar_calc_next_sunrise(double lat, double lon, double elev_m, int tz_offset_min, time_t now);
time_t solar_calc_next_sunset(double lat, double lon, double elev_m, int tz_offset_min, time_t now);
void solar_calc_next_moon(double lat, double lon, double elev_m, int tz_offset_min, time_t now, time_t *rise, time_t *set);
