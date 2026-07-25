#pragma once
#include "driver/gpio.h"

// ---------------------------------------------------------------------
// Location & units
// ---------------------------------------------------------------------
// Look up your coordinates at https://www.latlong.net/
#define LOCATION_NAME       "London"
#define LATITUDE             "51.5074"
#define LONGITUDE            "-0.1278"

#define USE_FAHRENHEIT        0   // 0 = Celsius + km/h, 1 = Fahrenheit + mph

// Minutes to add to UTC for your local time (no DST auto-adjust; update
// this by hand when your clocks change, e.g. GMT=0, BST=60).
#define UTC_OFFSET_MINUTES    60

// ---------------------------------------------------------------------
// Behavior
// ---------------------------------------------------------------------
#define REFRESH_INTERVAL_SEC   (10 * 60)   // re-fetch weather every 10 min
#define RETRY_INTERVAL_SEC     (60)         // retry sooner after a failed fetch

// ---------------------------------------------------------------------
// Hardware pins (Waveshare ESP32-C6-LCD-1.47)
// https://docs.waveshare.com/ESP32-C6-LCD-1.47
// ---------------------------------------------------------------------
// The panel is write-only, so no MISO line is wired to it - GPIO5 on this
// board is the microSD slot's MISO, unused here since this project doesn't
// touch the SD card.
#define LCD_PIN_SCK    GPIO_NUM_7
#define LCD_PIN_MOSI   GPIO_NUM_6
#define LCD_PIN_DC     GPIO_NUM_15
#define LCD_PIN_CS     GPIO_NUM_14
#define LCD_PIN_RST    GPIO_NUM_21
#define LCD_PIN_BL     GPIO_NUM_22

#define RGB_LED_GPIO   GPIO_NUM_8

// Native panel resolution (portrait). The app runs landscape, so the LVGL
// display is configured 320x172 - see display.c.
#define LCD_H_RES      172
#define LCD_V_RES      320

// The 172px panel axis is centered in the ST7789's 240px GRAM window.
#define LCD_GAP_SHORT_AXIS  ((240 - LCD_H_RES) / 2)   // = 34
