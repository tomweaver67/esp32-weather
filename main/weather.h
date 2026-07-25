#pragma once
#include <stdbool.h>

typedef enum {
    WEATHER_CLEAR_SKY,
    WEATHER_PARTLY_CLOUDY,
    WEATHER_OVERCAST,
    WEATHER_FOG,
    WEATHER_DRIZZLE,
    WEATHER_RAIN,
    WEATHER_SNOW,
    WEATHER_THUNDERSTORM,
    WEATHER_UNKNOWN
} weather_condition_t;

typedef struct {
    bool valid;
    float temperature;
    float feels_like;
    float temp_min;
    float temp_max;
    int humidity;
    float wind_speed;
    int weather_code;
    bool is_day;
} weather_data_t;

// Fetches current conditions + today's high/low from Open-Meteo.
// Returns true and fills `out` on success; leaves `out` untouched on failure.
bool weather_fetch(weather_data_t *out);

weather_condition_t weather_classify_code(int code);
const char *weather_describe_code(int code);
