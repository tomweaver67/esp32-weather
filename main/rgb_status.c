#include "rgb_status.h"
#include "led_strip.h"
#include "config.h"

static led_strip_handle_t s_strip;

void rgb_status_init(void) {
    led_strip_config_t strip_config = {
        .strip_gpio_num = RGB_LED_GPIO,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
    };
    led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    led_strip_clear(s_strip);
}

// Brightness is scaled down (max component ~40/255) since the LED sits
// right next to the screen and full brightness is glaring.
static void set_color(uint8_t r, uint8_t g, uint8_t b) {
    led_strip_set_pixel(s_strip, 0, r, g, b);
    led_strip_refresh(s_strip);
}

void rgb_status_show_weather(weather_condition_t cond, bool is_day) {
    switch (cond) {
        case WEATHER_CLEAR_SKY:
            is_day ? set_color(40, 30, 3) : set_color(10, 10, 30);
            break;
        case WEATHER_PARTLY_CLOUDY:
            set_color(30, 30, 10);
            break;
        case WEATHER_OVERCAST:
        case WEATHER_FOG:
            set_color(20, 20, 20);
            break;
        case WEATHER_DRIZZLE:
        case WEATHER_RAIN:
            set_color(5, 15, 40);
            break;
        case WEATHER_SNOW:
            set_color(35, 35, 40);
            break;
        case WEATHER_THUNDERSTORM:
            set_color(25, 5, 35);
            break;
        default:
            set_color(40, 40, 40);
            break;
    }
}

void rgb_status_show_error(void) {
    set_color(40, 0, 0);
}
