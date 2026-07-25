#pragma once
#include <stdbool.h>
#include "weather.h"

void display_init(void);

// Full-screen status message, used for boot/connecting/error states.
void display_show_message(const char *line1, const char *line2);

// Full redraw of the weather layout (labels + icon).
void display_show_weather(const weather_data_t *data, bool stale);

// Updates just the clock label - cheap to call from a periodic task.
void display_update_clock(int hour, int minute);
