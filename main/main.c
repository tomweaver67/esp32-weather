#include <time.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_netif_sntp.h"
#include "esp_log.h"

#include "config.h"
#include "secrets.h"
#include "wifi_connect.h"
#include "weather.h"
#include "display.h"
#include "rgb_status.h"

static const char *TAG = "weather_display";

static void init_nvs(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

static void init_time(void) {
    esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&sntp_config);
    // Best-effort: if this times out we just show a wrong clock until the
    // background sync catches up - not worth blocking startup over.
    esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000));
}

void app_main(void) {
    init_nvs();

    display_init();
    rgb_status_init();
    display_show_message("Weather Display", "Starting...");

    wifi_connect_init();
    display_show_message("Connecting to WiFi...", WIFI_SSID);
    if (!wifi_connect_wait(20000)) {
        display_show_message("Still connecting to WiFi...", "Will keep retrying");
    }

    init_time();

    weather_data_t current = {0};
    int last_minute = -1;
    time_t next_fetch_due = 0;

    while (1) {
        time_t now = time(NULL);

        if (now >= next_fetch_due) {
            if (wifi_connect_is_up()) {
                weather_data_t fetched;
                if (weather_fetch(&fetched)) {
                    current = fetched;
                    ESP_LOGI(TAG, "%s: %d deg, %s, feels %d, H:%d L:%d, humidity %d%%, wind %d",
                             LOCATION_NAME, (int)lroundf(current.temperature),
                             weather_describe_code(current.weather_code),
                             (int)lroundf(current.feels_like),
                             (int)lroundf(current.temp_max), (int)lroundf(current.temp_min),
                             current.humidity, (int)lroundf(current.wind_speed));
                    display_show_weather(&current, false);
                    rgb_status_show_weather(weather_classify_code(current.weather_code), current.is_day);
                    next_fetch_due = now + REFRESH_INTERVAL_SEC;
                } else {
                    rgb_status_show_error();
                    if (current.valid) {
                        display_show_weather(&current, true);
                    } else {
                        display_show_message("Fetching weather...", "No data yet");
                    }
                    next_fetch_due = now + RETRY_INTERVAL_SEC;
                }
            } else {
                next_fetch_due = now + RETRY_INTERVAL_SEC;
            }
        }

        if (current.valid) {
            time_t local = now + (UTC_OFFSET_MINUTES * 60);
            struct tm tm_info;
            gmtime_r(&local, &tm_info);
            if (tm_info.tm_min != last_minute) {
                last_minute = tm_info.tm_min;
                display_update_clock(tm_info.tm_hour, tm_info.tm_min);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
