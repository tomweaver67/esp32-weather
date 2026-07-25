#pragma once
#include "weather.h"

void rgb_status_init(void);
void rgb_status_show_weather(weather_condition_t cond, bool is_day);
void rgb_status_show_error(void);
