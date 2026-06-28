#include "solar_calc.h"

// On-watch sun/moon math without libm; double Julian dates (float loses ~6 h/day).
#include <pebble.h>
#include <stdbool.h>
#include <string.h>

#define SC_PI 3.14159265358979323846
#define SC_TAU (2.0 * SC_PI)
#define SC_RAD (SC_PI / 180.0)
#define SC_J1970 2440588.0
#define SC_J2000 2451545.0
#define SC_J0 0.0009
#define SC_DAY_SEC 86400
#define SC_E (SC_RAD * 23.4397)
#define SC_EARTH_R 6371000.0
#define SC_PI_2 (SC_PI * 0.5)
#define SC_EPS 1e-9

static double sc_normalize_tau(double radians) {
	while (radians < 0.0) {
		radians += SC_TAU;
	}
	while (radians >= SC_TAU) {
		radians -= SC_TAU;
	}
	return radians;
}

static double sc_sin_taylor(double x) {
	double x2 = x * x;
	return x * (1.0 - x2 * (1.0 / 6.0 - x2 * (1.0 / 120.0 - x2 / 5040.0)));
}

static double sc_cos_taylor(double x) {
	double x2 = x * x;
	return 1.0 - x2 * (0.5 - x2 * (1.0 / 24.0 - x2 / 720.0));
}

static double sc_wrap_angle(double radians) {
	while (radians > SC_PI) {
		radians -= SC_TAU;
	}
	while (radians < -SC_PI) {
		radians += SC_TAU;
	}
	return radians;
}

static double sc_fabs(double value) {
	return value < 0.0 ? -value : value;
}

static double sc_sin(double radians) {
	double x = sc_normalize_tau(radians);
	int quadrant = (int) (x / SC_PI_2);
	double r = x - quadrant * SC_PI_2;

	switch (quadrant % 4) {
		case 0:
			return sc_sin_taylor(r);
		case 1:
			return sc_cos_taylor(r);
		case 2:
			return -sc_sin_taylor(r);
		default:
			return -sc_cos_taylor(r);
	}
}

static double sc_cos(double radians) {
	double x = sc_normalize_tau(radians);
	int quadrant = (int) (x / SC_PI_2);
	double r = x - quadrant * SC_PI_2;

	switch (quadrant % 4) {
		case 0:
			return sc_cos_taylor(r);
		case 1:
			return -sc_sin_taylor(r);
		case 2:
			return -sc_cos_taylor(r);
		default:
			return sc_sin_taylor(r);
	}
}

static double sc_sqrt(double value) {
	double guess;
	int i;

	if (value <= 0.0) {
		return 0.0;
	}

	guess = value;
	for (i = 0; i < 8; i++) {
		guess = 0.5 * (guess + value / guess);
	}
	return guess;
}

static double sc_atan(double x) {
	double ax = sc_fabs(x);
	double result;

	if (ax > 1.0) {
		result = SC_PI_2 - sc_atan(1.0 / ax);
	} else {
		double x2 = ax * ax;
		result = ax * (1.0 - x2 * (1.0 / 3.0 - x2 * (1.0 / 5.0 - x2 / 7.0)));
	}

	return x < 0.0 ? -result : result;
}

static double sc_atan2(double y, double x) {
	if (x > 0.0) {
		return sc_atan(y / x);
	}
	if (x < 0.0 && y >= 0.0) {
		return sc_atan(y / x) + SC_PI;
	}
	if (x < 0.0 && y < 0.0) {
		return sc_atan(y / x) - SC_PI;
	}
	if (x == 0.0 && y > 0.0) {
		return SC_PI * 0.5;
	}
	if (x == 0.0 && y < 0.0) {
		return -SC_PI * 0.5;
	}
	return 0.0;
}

static double sc_asin(double value) {
	double root;

	if (value > 1.0) {
		value = 1.0;
	}
	if (value < -1.0) {
		value = -1.0;
	}

	root = sc_sqrt(1.0 - value * value);
	return sc_atan2(value, root);
}

static double sc_acos(double value) {
	return (SC_PI * 0.5) - sc_asin(value);
}

static double sc_floor(double value) {
	int whole = (int) value;
	if (value < 0.0 && value != (double) whole) {
		return (double) (whole - 1);
	}
	return (double) whole;
}

static double sc_horizon_dip(double elev_m) {
	if (elev_m <= 0.0) {
		return 0.0;
	}
	return sc_sqrt(2.0 * elev_m / SC_EARTH_R);
}

static double sc_to_julian(time_t timestamp) {
	return ((double) timestamp / (double) SC_DAY_SEC) - 0.5 + SC_J1970;
}

static time_t sc_from_julian(double j) {
	return (time_t) (((j + 0.5 - SC_J1970) * (double) SC_DAY_SEC) + 0.5);
}

static double sc_to_days(time_t timestamp) {
	return sc_to_julian(timestamp) - SC_J2000;
}

static time_t sc_to_local(time_t utc, int tz_offset_min) {
	return utc - (time_t) tz_offset_min * 60;
}

static time_t sc_to_utc(time_t local, int tz_offset_min) {
	return local + (time_t) tz_offset_min * 60;
}

static time_t sc_start_of_local_day(time_t utc, int tz_offset_min) {
	time_t local = sc_to_local(utc, tz_offset_min);
	time_t local_midnight = (local / SC_DAY_SEC) * SC_DAY_SEC;

	if (local < 0 && (local % SC_DAY_SEC) != 0) {
		local_midnight -= SC_DAY_SEC;
	}

	return sc_to_utc(local_midnight, tz_offset_min);
}

static time_t sc_next_local_day(time_t day_start_utc, int tz_offset_min) {
	(void) tz_offset_min;
	return day_start_utc + SC_DAY_SEC;
}

static double sc_declination(double l, double b) {
	return sc_asin(sc_sin(b) * sc_cos(SC_E) + sc_cos(b) * sc_sin(SC_E) * sc_sin(l));
}

static double sc_solar_mean_anomaly(double d) {
	return SC_RAD * (357.5291 + 0.98560028 * d);
}

static double sc_ecliptic_longitude(double m) {
	double c = SC_RAD * (1.9148 * sc_sin(m) + 0.02 * sc_sin(2.0 * m) + 0.0003 * sc_sin(3.0 * m));
	double p = SC_RAD * 102.9372;
	return m + c + p + SC_PI;
}

static double sc_round(double value) {
	return sc_floor(value + 0.5);
}

static double sc_julian_cycle(double d, double lw) {
	return sc_round(d - SC_J0 - lw / SC_TAU);
}

static double sc_approx_transit(double ht, double lw, double n) {
	return SC_J0 + (ht + lw) / SC_TAU + n;
}

static double sc_solar_transit_j(double ds, double m, double l) {
	return SC_J2000 + ds + 0.0053 * sc_sin(m) - 0.0069 * sc_sin(2.0 * l);
}

static double sc_hour_angle(double h, double phi, double dec) {
	double denom = sc_cos(phi) * sc_cos(dec);

	if (sc_fabs(denom) < SC_EPS) {
		return 0.0;
	}

	return sc_acos((sc_sin(h) - sc_sin(phi) * sc_sin(dec)) / denom);
}

static double sc_get_set_j(double h, double lw, double phi, double dec, double n, double m, double l) {
	double w = sc_hour_angle(h, phi, dec);
	double a = sc_approx_transit(w, lw, n);
	return sc_solar_transit_j(a, m, l);
}

static void sc_sun_rise_set(time_t day_start, double lat, double lon, double elev_m, time_t *rise, time_t *set) {
	double lw = SC_RAD * -lon;
	double phi = SC_RAD * lat;
	double d = sc_to_days(day_start);
	double n = sc_julian_cycle(d, lw);
	double ds = sc_approx_transit(0.0, lw, n);
	double m = sc_solar_mean_anomaly(ds);
	double l = sc_ecliptic_longitude(m);
	double dec = sc_declination(l, 0.0);
	double jnoon = sc_solar_transit_j(ds, m, l);
	double h = (-0.833) * SC_RAD - sc_horizon_dip(elev_m);
	double jset = sc_get_set_j(h, lw, phi, dec, n, m, l);
	double jrise = jnoon - (jset - jnoon);

	*rise = sc_from_julian(jrise);
	*set = sc_from_julian(jset);
}

static time_t sc_next_sun_event(double lat, double lon, double elev_m, int tz_offset_min, time_t now, bool want_rise) {
	time_t day = sc_start_of_local_day(now, tz_offset_min);
	int offset;

	for (offset = 0; offset < 3; offset++) {
		time_t rise;
		time_t set;
		time_t event_time;

		sc_sun_rise_set(day, lat, lon, elev_m, &rise, &set);
		event_time = want_rise ? rise : set;
		if (event_time > now) {
			return event_time;
		}
		day = sc_next_local_day(day, tz_offset_min);
	}

	return 0;
}

static double sc_tan_approx(double radians);

static double sc_right_ascension(double l, double b) {
	return sc_atan2(sc_sin(l) * sc_cos(SC_E) - sc_tan_approx(b) * sc_sin(SC_E), sc_cos(l));
}

static time_t sc_hours_later(time_t timestamp, double hours) {
	return timestamp + (time_t) (hours * 3600.0 + 0.5);
}

static double sc_tan_approx(double radians) {
	double x = sc_wrap_angle(radians);
	double x2 = x * x;
	return x * (1.0 + x2 * (1.0 / 3.0 + x2 * (2.0 / 15.0)));
}

static double sc_astro_refraction(double h) {
	if (h < 0.0) {
		h = 0.0;
	}
	return 0.0002967 / sc_tan_approx(h + 0.00312536 / (h + 0.08901179));
}

static double sc_sidereal_time(double d, double lw) {
	return SC_RAD * (280.16 + 360.9856235 * d) - lw;
}

static void sc_moon_coords(double d, double *ra, double *dec) {
	double L = SC_RAD * (218.316 + 13.176396 * d);
	double M = SC_RAD * (134.963 + 13.064993 * d);
	double F = SC_RAD * (93.272 + 13.229350 * d);
	double l = L + SC_RAD * 6.289 * sc_sin(M);
	double b = SC_RAD * 5.128 * sc_sin(F);

	*dec = sc_declination(l, b);
	*ra = sc_right_ascension(l, b);
}

static double sc_moon_altitude(time_t timestamp, double lat, double lon) {
	double lw = SC_RAD * -lon;
	double phi = SC_RAD * lat;
	double d = sc_to_days(timestamp);
	double ra;
	double dec;
	double h;

	sc_moon_coords(d, &ra, &dec);
	h = sc_asin(sc_sin(phi) * sc_sin(dec) + sc_cos(phi) * sc_cos(dec) * sc_cos(sc_sidereal_time(d, lw) - ra));
	return h + sc_astro_refraction(h);
}

static double sc_moon_threshold(double elev_m) {
	return (0.133 * SC_RAD) - sc_horizon_dip(elev_m);
}

static double sc_moon_delta(time_t day_start, double hours, double lat, double lon, double threshold) {
	return sc_moon_altitude(sc_hours_later(day_start, hours), lat, lon) - threshold;
}

static double sc_refine_moon_crossing(time_t day_start, double lat, double lon, double threshold, double hours) {
	double t0 = hours - 0.5;
	double t1 = hours + 0.5;
	double a0;
	double a1;
	double delta;

	if (t0 < 0.0) {
		t0 = 0.0;
	}
	if (t1 > 24.0) {
		t1 = 24.0;
	}

	a0 = sc_moon_delta(day_start, t0, lat, lon, threshold);
	a1 = sc_moon_delta(day_start, t1, lat, lon, threshold);
	delta = a1 - a0;
	if (sc_fabs(delta) < SC_EPS) {
		return hours;
	}

	return t0 + (-a0 / delta) * (t1 - t0);
}

static void sc_moon_rise_set(time_t day_start, double lat, double lon, double elev_m, time_t *rise, time_t *set) {
	static double threshold, h0, h1, h2, a, b, xe, disc, x1, x2, ye, rise_hours, set_hours, dx;
	static int i, roots;
	static bool has_rise, has_set;

	threshold = sc_moon_threshold(elev_m);
	h0 = sc_moon_delta(day_start, 0.0, lat, lon, threshold);
	rise_hours = 0.0;
	set_hours = 0.0;
	has_rise = false;
	has_set = false;
	*rise = 0;
	*set = 0;

	for (i = 1; i <= 24; i += 2) {
		h1 = sc_moon_delta(day_start, i, lat, lon, threshold);
		h2 = sc_moon_delta(day_start, i + 1, lat, lon, threshold);
		a = (h0 + h2) / 2.0 - h1;
		b = (h2 - h0) / 2.0;

		if (sc_fabs(a) < SC_EPS) {
			h0 = h2;
			continue;
		}

		xe = -b / (2.0 * a);
		disc = b * b - 4.0 * a * h1;
		roots = 0;
		x1 = 0.0;
		x2 = 0.0;
		ye = (a * xe + b) * xe + h1;

		if (disc >= 0.0) {
			dx = sc_sqrt(disc) / (sc_fabs(a) * 2.0);
			x1 = xe - dx;
			x2 = xe + dx;
			if (sc_fabs(x1) <= 1.0) {
				roots++;
			}
			if (sc_fabs(x2) <= 1.0) {
				roots++;
			}
			if (x1 < -1.0) {
				x1 = x2;
			}
		}

		if (roots == 1) {
			if (h0 < 0.0) {
				rise_hours = i + x1;
				has_rise = true;
			} else {
				set_hours = i + x1;
				has_set = true;
			}
		} else if (roots == 2) {
			rise_hours = i + (ye < 0.0 ? x2 : x1);
			set_hours = i + (ye < 0.0 ? x1 : x2);
			has_rise = true;
			has_set = true;
		}

		if (has_rise && has_set) {
			break;
		}

		h0 = h2;
	}

	if (has_rise) {
		rise_hours = sc_refine_moon_crossing(day_start, lat, lon, threshold, rise_hours);
		*rise = sc_hours_later(day_start, rise_hours);
	}
	if (has_set) {
		set_hours = sc_refine_moon_crossing(day_start, lat, lon, threshold, set_hours);
		*set = sc_hours_later(day_start, set_hours);
	}
}

static time_t sc_next_moon_event(double lat, double lon, double elev_m, int tz_offset_min, time_t now, bool want_rise) {
	time_t day = sc_start_of_local_day(now, tz_offset_min);
	int offset;

	for (offset = 0; offset < 4; offset++) {
		time_t rise;
		time_t set;
		time_t event_time;

		sc_moon_rise_set(day, lat, lon, elev_m, &rise, &set);
		event_time = want_rise ? rise : set;
		if (event_time > 0 && event_time > now) {
			return event_time;
		}
		day = sc_next_local_day(day, tz_offset_min);
	}

	return 0;
}

time_t solar_calc_next_sunrise(double lat, double lon, double elev_m, int tz_offset_min, time_t now) {
	return sc_next_sun_event(lat, lon, elev_m, tz_offset_min, now, true);
}

time_t solar_calc_next_sunset(double lat, double lon, double elev_m, int tz_offset_min, time_t now) {
	return sc_next_sun_event(lat, lon, elev_m, tz_offset_min, now, false);
}

void solar_calc_next_moon(double lat, double lon, double elev_m, int tz_offset_min, time_t now, time_t *rise, time_t *set) {
	if (rise) {
		*rise = sc_next_moon_event(lat, lon, elev_m, tz_offset_min, now, true);
	}
	if (set) {
		*set = sc_next_moon_event(lat, lon, elev_m, tz_offset_min, now, false);
	}
}
