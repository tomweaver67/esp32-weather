#include "display.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lvgl_port.h"
#include "config.h"

// Landscape: physical panel is 172x320 portrait, rotated 90 deg by the LVGL
// port (swap_xy), so the LVGL coordinate space is 320 wide x 172 tall.
#define SCREEN_W LCD_V_RES
#define SCREEN_H LCD_H_RES

// LVGL renders in horizontal bands of this many lines. The SPI bus must be
// able to carry a whole band in one transfer, so both are derived from it.
#define DRAW_BUF_LINES  40
#define DRAW_BUF_PIXELS (SCREEN_W * DRAW_BUF_LINES)
#define ICON_SIZE 72

static lv_display_t *s_disp;

static lv_obj_t *s_label_location;
static lv_obj_t *s_label_clock;
static lv_obj_t *s_dot_stale;
static lv_obj_t *s_label_temp;
static lv_obj_t *s_label_condition;
static lv_obj_t *s_label_feels;
static lv_obj_t *s_label_hilo;
static lv_obj_t *s_label_humidity;
static lv_obj_t *s_label_wind;
static lv_obj_t *s_canvas_icon;

static lv_obj_t *s_status_overlay;
static lv_obj_t *s_status_label1;
static lv_obj_t *s_status_label2;

static uint8_t s_icon_buf[LV_CANVAS_BUF_SIZE(ICON_SIZE, ICON_SIZE, 16, 1)];

static const lv_color_t COLOR_BG = LV_COLOR_MAKE(0x00, 0x00, 0x00);
static const lv_color_t COLOR_TEXT = LV_COLOR_MAKE(0xff, 0xff, 0xff);
static const lv_color_t COLOR_DIM = LV_COLOR_MAKE(0x96, 0x9b, 0xa5);
static const lv_color_t COLOR_STALE = LV_COLOR_MAKE(0xdc, 0x3c, 0x3c);

// ---------------------------------------------------------------------
// Panel bring-up
// ---------------------------------------------------------------------

static esp_lcd_panel_io_handle_t s_io_handle;
static esp_lcd_panel_handle_t s_panel_handle;

static void init_panel(void) {
    gpio_config_t bl_cfg = {
        .pin_bit_mask = 1ULL << LCD_PIN_BL,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&bl_cfg);
    gpio_set_level(LCD_PIN_BL, 1);

    spi_bus_config_t buscfg = {
        .sclk_io_num = LCD_PIN_SCK,
        .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DRAW_BUF_PIXELS * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = LCD_PIN_CS,
        .dc_gpio_num = LCD_PIN_DC,
        .spi_mode = 0,
        .pclk_hz = 40 * 1000 * 1000,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &s_io_handle));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(s_io_handle, &panel_config, &s_panel_handle));

    esp_lcd_panel_reset(s_panel_handle);
    esp_lcd_panel_init(s_panel_handle);
    esp_lcd_panel_invert_color(s_panel_handle, true);

    // The 172px-wide panel is centered in the ST7789's 240px-wide GRAM, so
    // that axis needs a (240-172)/2 = 34px offset. The driver adds the gap to
    // the coordinates it receives, which are post-swap_xy - and we run
    // landscape (swap_xy), so the 172 axis is y here, not x. Passing this as
    // an x gap instead shifts the image along the 320 axis and reads the
    // wrong 172-row window of GRAM.
    esp_lcd_panel_set_gap(s_panel_handle, 0, LCD_GAP_SHORT_AXIS);

    esp_lcd_panel_disp_on_off(s_panel_handle, true);
}

static void init_lvgl(void) {
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = s_io_handle,
        .panel_handle = s_panel_handle,
        .buffer_size = DRAW_BUF_PIXELS,
        .double_buffer = false,
        .hres = SCREEN_W,
        .vres = SCREEN_H,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = true,
            .mirror_x = false,
            .mirror_y = true,
        },
        .flags = {
            .buff_dma = true,
            .swap_bytes = true,
        },
    };
    s_disp = lvgl_port_add_disp(&disp_cfg);
}

// ---------------------------------------------------------------------
// Icon drawing - simple vector shapes on a small canvas, no bitmap assets
// ---------------------------------------------------------------------

static inline void px(int x, int y, lv_color_t color) {
    if (x < 0 || y < 0 || x >= ICON_SIZE || y >= ICON_SIZE) {
        return;
    }
    lv_canvas_set_px(s_canvas_icon, x, y, color, LV_OPA_COVER);
}

static void draw_line(int x0, int y0, int x1, int y1, lv_color_t color) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;
    for (;;) {
        px(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void fill_circle(int cx, int cy, int r, lv_color_t color) {
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if (x * x + y * y <= r * r) {
                px(cx + x, cy + y, color);
            }
        }
    }
}

static void fill_rect(int x, int y, int w, int h, lv_color_t color) {
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            px(x + i, y + j, color);
        }
    }
}

static const lv_color_t COLOR_SUN = LV_COLOR_MAKE(0xff, 0xb9, 0x28);
static const lv_color_t COLOR_MOON = LV_COLOR_MAKE(0xdc, 0xdc, 0xe6);
static const lv_color_t COLOR_CLOUD = LV_COLOR_MAKE(0xc8, 0xcd, 0xd2);
static const lv_color_t COLOR_CLOUD_DARK = LV_COLOR_MAKE(0x8c, 0x94, 0x9e);
static const lv_color_t COLOR_RAIN = LV_COLOR_MAKE(0x5a, 0xa0, 0xeb);
static const lv_color_t COLOR_SNOW = LV_COLOR_MAKE(0xff, 0xff, 0xff);
static const lv_color_t COLOR_BOLT = LV_COLOR_MAKE(0xff, 0xd2, 0x3c);
static const lv_color_t COLOR_FOG = LV_COLOR_MAKE(0xaa, 0xaf, 0xb4);

static void draw_sun(int cx, int cy, int r) {
    fill_circle(cx, cy, r, COLOR_SUN);
    for (int i = 0; i < 8; i++) {
        float a = i * (M_PI / 4.0f);
        int x0 = cx + (int)(cosf(a) * (r + 4));
        int y0 = cy + (int)(sinf(a) * (r + 4));
        int x1 = cx + (int)(cosf(a) * (r + 9));
        int y1 = cy + (int)(sinf(a) * (r + 9));
        draw_line(x0, y0, x1, y1, COLOR_SUN);
    }
}

static void draw_moon(int cx, int cy, int r) {
    fill_circle(cx, cy, r, COLOR_MOON);
    fill_circle(cx + r / 2, cy - r / 3, r, COLOR_BG);
}

static void draw_cloud(int cx, int cy, int size, lv_color_t color) {
    int r = size / 3;
    fill_circle(cx - r, cy, r, color);
    fill_circle(cx + r / 2, cy - r / 2, (int)(r * 1.2f), color);
    fill_circle(cx + r, cy, r, color);
    fill_rect(cx - r - r / 2, cy, (int)(r * 3.5f), r + 2, color);
}

static void draw_rain(int cx, int cy, int size) {
    int spacing = size / 3;
    for (int i = -1; i <= 1; i++) {
        int x = cx + i * spacing;
        draw_line(x, cy, x - 3, cy + 8, COLOR_RAIN);
    }
}

static void draw_drizzle(int cx, int cy, int size) {
    int spacing = size / 3;
    for (int i = -1; i <= 1; i++) {
        int x = cx + i * spacing;
        px(x, cy, COLOR_RAIN);
        px(x, cy + 3, COLOR_RAIN);
    }
}

static void draw_snow(int cx, int cy, int size) {
    int spacing = size / 3;
    for (int i = -1; i <= 1; i++) {
        int x = cx + i * spacing;
        int y = cy + 4;
        draw_line(x - 3, y, x + 3, y, COLOR_SNOW);
        draw_line(x, y - 3, x, y + 3, COLOR_SNOW);
        draw_line(x - 2, y - 2, x + 2, y + 2, COLOR_SNOW);
        draw_line(x - 2, y + 2, x + 2, y - 2, COLOR_SNOW);
    }
}

static void draw_fog(int cx, int cy, int size) {
    int half_w = size / 2;
    for (int i = 0; i < 3; i++) {
        draw_line(cx - half_w, cy + i * 4, cx + half_w, cy + i * 4, COLOR_FOG);
    }
}

static void draw_bolt(int cx, int cy) {
    draw_line(cx + 2, cy - 2, cx - 4, cy + 8, COLOR_BOLT);
    draw_line(cx - 4, cy + 8, cx + 1, cy + 8, COLOR_BOLT);
    draw_line(cx + 1, cy + 8, cx - 1, cy + 18, COLOR_BOLT);
}

static void draw_weather_icon(weather_condition_t cond, bool is_day) {
    lv_canvas_fill_bg(s_canvas_icon, COLOR_BG, LV_OPA_COVER);

    int cx = ICON_SIZE / 2;
    int cy = ICON_SIZE / 2;
    int r = ICON_SIZE / 4;

    switch (cond) {
        case WEATHER_CLEAR_SKY:
            is_day ? draw_sun(cx, cy, r + 2) : draw_moon(cx, cy, r + 2);
            break;
        case WEATHER_PARTLY_CLOUDY:
            if (is_day) {
                draw_sun(cx - ICON_SIZE / 5, cy - ICON_SIZE / 6, r);
            } else {
                draw_moon(cx - ICON_SIZE / 5, cy - ICON_SIZE / 6, r);
            }
            draw_cloud(cx + ICON_SIZE / 8, cy + ICON_SIZE / 8, ICON_SIZE, COLOR_CLOUD);
            break;
        case WEATHER_OVERCAST:
            draw_cloud(cx, cy, ICON_SIZE, COLOR_CLOUD_DARK);
            break;
        case WEATHER_FOG:
            draw_cloud(cx, cy - ICON_SIZE / 6, (int)(ICON_SIZE * 0.8f), COLOR_CLOUD);
            draw_fog(cx, cy + ICON_SIZE / 3, ICON_SIZE);
            break;
        case WEATHER_DRIZZLE:
            draw_cloud(cx, cy - ICON_SIZE / 6, ICON_SIZE, COLOR_CLOUD_DARK);
            draw_drizzle(cx, cy + ICON_SIZE / 3, ICON_SIZE);
            break;
        case WEATHER_RAIN:
            draw_cloud(cx, cy - ICON_SIZE / 6, ICON_SIZE, COLOR_CLOUD_DARK);
            draw_rain(cx, cy + ICON_SIZE / 3, ICON_SIZE);
            break;
        case WEATHER_SNOW:
            draw_cloud(cx, cy - ICON_SIZE / 6, ICON_SIZE, COLOR_CLOUD);
            draw_snow(cx, cy + ICON_SIZE / 3, ICON_SIZE);
            break;
        case WEATHER_THUNDERSTORM:
            draw_cloud(cx, cy - ICON_SIZE / 6, ICON_SIZE, COLOR_CLOUD_DARK);
            draw_bolt(cx, cy + ICON_SIZE / 4);
            break;
        default:
            for (int a = 0; a < 360; a++) {
                float rad = a * (float)M_PI / 180.0f;
                px(cx + (int)(cosf(rad) * (r + 2)), cy + (int)(sinf(rad) * (r + 2)), COLOR_TEXT);
            }
            break;
    }
}

// ---------------------------------------------------------------------
// Widget layout
// ---------------------------------------------------------------------

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, int x, int y) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

void display_init(void) {
    init_panel();
    init_lvgl();

    lvgl_port_lock(0);

    lv_obj_t *scr = lv_disp_get_scr_act(s_disp);
    lv_obj_set_style_bg_color(scr, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    s_label_location = make_label(scr, &lv_font_montserrat_14, COLOR_TEXT, 4, 4);
    lv_label_set_text(s_label_location, LOCATION_NAME);

    s_label_clock = make_label(scr, &lv_font_montserrat_14, COLOR_TEXT, 278, 4);
    lv_label_set_text(s_label_clock, "--:--");

    s_dot_stale = lv_obj_create(scr);
    lv_obj_remove_style_all(s_dot_stale);
    lv_obj_set_size(s_dot_stale, 6, 6);
    lv_obj_set_pos(s_dot_stale, 263, 6);
    lv_obj_set_style_radius(s_dot_stale, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_dot_stale, COLOR_STALE, 0);
    lv_obj_set_style_bg_opa(s_dot_stale, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_dot_stale, LV_OBJ_FLAG_HIDDEN);

    s_canvas_icon = lv_canvas_create(scr);
    lv_canvas_set_buffer(s_canvas_icon, s_icon_buf, ICON_SIZE, ICON_SIZE, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_canvas_icon, 20, 50);

    s_label_temp = make_label(scr, &lv_font_montserrat_32, COLOR_TEXT, 110, 40);
    lv_label_set_text(s_label_temp, "--");

    s_label_condition = make_label(scr, &lv_font_montserrat_20, COLOR_DIM, 110, 90);
    lv_label_set_text(s_label_condition, "");

    s_label_feels = make_label(scr, &lv_font_montserrat_14, COLOR_DIM, 6, 138);
    s_label_hilo = make_label(scr, &lv_font_montserrat_14, COLOR_DIM, 165, 138);
    s_label_humidity = make_label(scr, &lv_font_montserrat_14, COLOR_DIM, 6, 154);
    s_label_wind = make_label(scr, &lv_font_montserrat_14, COLOR_DIM, 165, 154);

    lv_obj_t *divider = lv_obj_create(scr);
    lv_obj_remove_style_all(divider);
    lv_obj_set_size(divider, SCREEN_W - 8, 1);
    lv_obj_set_pos(divider, 4, 130);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0x3c414b), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);

    s_status_overlay = lv_obj_create(scr);
    lv_obj_remove_style_all(s_status_overlay);
    lv_obj_set_size(s_status_overlay, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(s_status_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_status_overlay, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_status_overlay, LV_OPA_COVER, 0);

    s_status_label1 = make_label(s_status_overlay, &lv_font_montserrat_20, COLOR_TEXT, 0, 70);
    lv_obj_set_width(s_status_label1, SCREEN_W);
    lv_obj_set_style_text_align(s_status_label1, LV_TEXT_ALIGN_CENTER, 0);

    s_status_label2 = make_label(s_status_overlay, &lv_font_montserrat_14, COLOR_DIM, 0, 96);
    lv_obj_set_width(s_status_label2, SCREEN_W);
    lv_obj_set_style_text_align(s_status_label2, LV_TEXT_ALIGN_CENTER, 0);

    lvgl_port_unlock();
}

void display_show_message(const char *line1, const char *line2) {
    lvgl_port_lock(0);
    lv_label_set_text(s_status_label1, line1);
    lv_label_set_text(s_status_label2, line2 ? line2 : "");
    lv_obj_clear_flag(s_status_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_status_overlay);
    lvgl_port_unlock();
}

void display_show_weather(const weather_data_t *data, bool stale) {
    char buf[32];
    char unit = USE_FAHRENHEIT ? 'F' : 'C';

    lvgl_port_lock(0);

    lv_obj_add_flag(s_status_overlay, LV_OBJ_FLAG_HIDDEN);

    if (stale) {
        lv_obj_clear_flag(s_dot_stale, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_dot_stale, LV_OBJ_FLAG_HIDDEN);
    }

    weather_condition_t cond = weather_classify_code(data->weather_code);
    draw_weather_icon(cond, data->is_day);

    snprintf(buf, sizeof(buf), "%d%c", (int)lroundf(data->temperature), unit);
    lv_label_set_text(s_label_temp, buf);

    lv_label_set_text(s_label_condition, weather_describe_code(data->weather_code));

    snprintf(buf, sizeof(buf), "Feels %d%c", (int)lroundf(data->feels_like), unit);
    lv_label_set_text(s_label_feels, buf);

    snprintf(buf, sizeof(buf), "H:%d%c  L:%d%c", (int)lroundf(data->temp_max), unit,
             (int)lroundf(data->temp_min), unit);
    lv_label_set_text(s_label_hilo, buf);

    snprintf(buf, sizeof(buf), "Humidity %d%%", data->humidity);
    lv_label_set_text(s_label_humidity, buf);

    snprintf(buf, sizeof(buf), "Wind %d %s", (int)lroundf(data->wind_speed), USE_FAHRENHEIT ? "mph" : "km/h");
    lv_label_set_text(s_label_wind, buf);

    lvgl_port_unlock();
}

void display_update_clock(int hour, int minute) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", hour, minute);

    lvgl_port_lock(0);
    lv_label_set_text(s_label_clock, buf);
    lvgl_port_unlock();
}
