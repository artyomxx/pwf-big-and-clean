#pragma once

#include <pebble.h>
#include <stddef.h>
#include <time.h>

int32_t util_tuple_read_int32(const Tuple *tuple);
int32_t util_coord_str_to_e6(const char *str);
void util_format_hhmm(time_t timestamp, int32_t tz_offset_min, char *buffer, size_t buffer_size);

TextLayer *util_create_text_layer(Layer *parent, GRect frame, GTextAlignment alignment, GFont font);
