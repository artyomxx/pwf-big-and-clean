#include "util.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

// Clay/PKJS often sends toggles as 1-byte tuples; accept all integer widths.
int32_t util_tuple_read_int32(const Tuple *tuple) {
	if (!tuple) {
		return 0;
	}

	if (tuple->type == TUPLE_INT) {
		if (tuple->length == 1) {
			return tuple->value->int8;
		}
		if (tuple->length == 2) {
			return tuple->value->int16;
		}
		if (tuple->length == 4) {
			return tuple->value->int32;
		}
	}

	if (tuple->type == TUPLE_UINT) {
		if (tuple->length == 1) {
			return tuple->value->uint8;
		}
		if (tuple->length == 2) {
			return tuple->value->uint16;
		}
		if (tuple->length == 4) {
			return tuple->value->uint32;
		}
	}

	return 0;
}

int32_t util_coord_str_to_e6(const char *str) {
	if (!str || !str[0]) {
		return 0;
	}

	bool negative = false;
	if (*str == '-') {
		negative = true;
		str++;
	} else if (*str == '+') {
		str++;
	}

	int32_t whole = 0;
	while (*str >= '0' && *str <= '9') {
		whole = (whole * 10) + (*str - '0');
		str++;
	}

	int32_t frac = 0;
	int frac_digits = 0;
	if (*str == '.') {
		str++;
		while (*str >= '0' && *str <= '9' && frac_digits < 6) {
			frac = (frac * 10) + (*str - '0');
			frac_digits++;
			str++;
		}
	}

	while (frac_digits < 6) {
		frac *= 10;
		frac_digits++;
	}

	int32_t e6 = (whole * 1000000) + frac;
	return negative ? -e6 : e6;
}

void util_format_hhmm(time_t timestamp, int32_t tz_offset_min, char *buffer, size_t buffer_size) {
	struct tm tick_time;
	struct tm *tm_ptr;
	time_t local;

	if (timestamp <= 0) {
		snprintf(buffer, buffer_size, "--:--");
		return;
	}

	local = timestamp - (time_t) tz_offset_min * 60;
	tm_ptr = gmtime(&local);
	if (!tm_ptr) {
		snprintf(buffer, buffer_size, "--:--");
		return;
	}

	tick_time = *tm_ptr;
	strftime(buffer, buffer_size, "%H:%M", &tick_time);
}

TextLayer *util_create_text_layer(Layer *parent, GRect frame, GTextAlignment alignment, GFont font) {
	TextLayer *layer = text_layer_create(frame);
	text_layer_set_background_color(layer, GColorClear);
	text_layer_set_text_color(layer, GColorWhite);
	text_layer_set_font(layer, font);
	text_layer_set_text_alignment(layer, alignment);
	layer_add_child(parent, text_layer_get_layer(layer));
	return layer;
}
