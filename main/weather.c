#include "weather.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "cJSON.h"
#include "config.h"

static const char *TAG = "weather";

weather_condition_t weather_classify_code(int code) {
    switch (code) {
        case 0: return WEATHER_CLEAR_SKY;
        case 1:
        case 2: return WEATHER_PARTLY_CLOUDY;
        case 3: return WEATHER_OVERCAST;
        case 45:
        case 48: return WEATHER_FOG;
        case 51: case 53: case 55:
        case 56: case 57: return WEATHER_DRIZZLE;
        case 61: case 63: case 65:
        case 66: case 67:
        case 80: case 81: case 82: return WEATHER_RAIN;
        case 71: case 73: case 75: case 77:
        case 85: case 86: return WEATHER_SNOW;
        case 95: case 96: case 99: return WEATHER_THUNDERSTORM;
        default: return WEATHER_UNKNOWN;
    }
}

const char *weather_describe_code(int code) {
    switch (code) {
        case 0: return "Clear sky";
        case 1: return "Mainly clear";
        case 2: return "Partly cloudy";
        case 3: return "Overcast";
        case 45: return "Fog";
        case 48: return "Rime fog";
        case 51: return "Light drizzle";
        case 53: return "Drizzle";
        case 55: return "Dense drizzle";
        case 56: case 57: return "Freezing drizzle";
        case 61: return "Light rain";
        case 63: return "Rain";
        case 65: return "Heavy rain";
        case 66: case 67: return "Freezing rain";
        case 71: return "Light snow";
        case 73: return "Snow";
        case 75: return "Heavy snow";
        case 77: return "Snow grains";
        case 80: return "Light showers";
        case 81: return "Showers";
        case 82: return "Violent showers";
        case 85: case 86: return "Snow showers";
        case 95: return "Thunderstorm";
        case 96: case 99: return "Thunderstorm w/ hail";
        default: return "Unknown";
    }
}

static double json_num(const cJSON *obj, const char *key, double fallback) {
    cJSON *item = cJSON_GetObjectItem(obj, key);
    return cJSON_IsNumber(item) ? item->valuedouble : fallback;
}

#define RESPONSE_BUF_SIZE 4096

bool weather_fetch(weather_data_t *out) {
    char url[256];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
             "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,weather_code,wind_speed_10m"
             "&daily=temperature_2m_max,temperature_2m_min&timezone=auto"
#if USE_FAHRENHEIT
             "&temperature_unit=fahrenheit&wind_speed_unit=mph"
#endif
             ,
             LATITUDE, LONGITUDE);

    esp_http_client_config_t config = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return false;
    }

    char *buf = malloc(RESPONSE_BUF_SIZE);
    if (!buf) {
        esp_http_client_cleanup(client);
        return false;
    }

    bool ok = false;

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "open failed: %s", esp_err_to_name(err));
        goto done;
    }

    esp_http_client_fetch_headers(client);

    if (esp_http_client_get_status_code(client) != 200) {
        ESP_LOGW(TAG, "unexpected status %d", esp_http_client_get_status_code(client));
        esp_http_client_close(client);
        goto done;
    }

    {
        int total_read = 0;
        int r;
        while (total_read < RESPONSE_BUF_SIZE - 1 &&
               (r = esp_http_client_read(client, buf + total_read, RESPONSE_BUF_SIZE - 1 - total_read)) > 0) {
            total_read += r;
        }
        buf[total_read] = '\0';
    }
    esp_http_client_close(client);

    {
        cJSON *root = cJSON_Parse(buf);
        if (!root) {
            ESP_LOGW(TAG, "JSON parse failed");
            goto done;
        }

        cJSON *current = cJSON_GetObjectItem(root, "current");
        if (!cJSON_IsObject(current)) {
            cJSON_Delete(root);
            goto done;
        }

        out->temperature = json_num(current, "temperature_2m", 0);
        out->feels_like = json_num(current, "apparent_temperature", out->temperature);
        out->humidity = (int)json_num(current, "relative_humidity_2m", 0);
        out->wind_speed = json_num(current, "wind_speed_10m", 0);
        out->weather_code = (int)json_num(current, "weather_code", 0);
        out->is_day = json_num(current, "is_day", 1) == 1;

        cJSON *daily = cJSON_GetObjectItem(root, "daily");
        cJSON *tmax_arr = cJSON_IsObject(daily) ? cJSON_GetObjectItem(daily, "temperature_2m_max") : NULL;
        cJSON *tmin_arr = cJSON_IsObject(daily) ? cJSON_GetObjectItem(daily, "temperature_2m_min") : NULL;
        cJSON *tmax0 = (cJSON_IsArray(tmax_arr) && cJSON_GetArraySize(tmax_arr) > 0) ? cJSON_GetArrayItem(tmax_arr, 0) : NULL;
        cJSON *tmin0 = (cJSON_IsArray(tmin_arr) && cJSON_GetArraySize(tmin_arr) > 0) ? cJSON_GetArrayItem(tmin_arr, 0) : NULL;
        out->temp_max = cJSON_IsNumber(tmax0) ? tmax0->valuedouble : out->temperature;
        out->temp_min = cJSON_IsNumber(tmin0) ? tmin0->valuedouble : out->temperature;

        out->valid = true;
        ok = true;
        cJSON_Delete(root);
    }

done:
    free(buf);
    esp_http_client_cleanup(client);
    return ok;
}
