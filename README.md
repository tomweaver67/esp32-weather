# ESP32-C6-LCD-1.47 Weather Display

Current-conditions display for the [Waveshare ESP32-C6-LCD-1.47](https://docs.waveshare.com/ESP32-C6-LCD-1.47) —
temperature, condition icon, feels-like, today's high/low, humidity, and
wind, with a live clock and an onboard RGB LED that reflects the current
condition. Weather data comes from [Open-Meteo](https://open-meteo.com/),
which is free and requires no account or API key.

Built on **ESP-IDF** (native Espressif framework) using `esp_lcd` +
[`esp_lvgl_port`](https://components.espressif.com/components/espressif/esp_lvgl_port)
+ LVGL 9 for the display, and the
[`led_strip`](https://components.espressif.com/components/espressif/led_strip)
component for the onboard WS2812.

## Hardware

Nothing to wire up — the LCD, RGB LED, and microSD slot are all onboard.
Pin mapping used by this project (from Waveshare's docs):

| Function | GPIO |
|---|---|
| LCD SCLK | 7 |
| LCD MOSI | 6 |
| LCD DC | 15 |
| LCD CS | 14 |
| LCD RST | 21 |
| LCD Backlight | 22 |
| RGB LED (WS2812) | 8 |

## Setup

1. Install [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/index.html)
   v5.3 or newer (this was built and tested against v6.0.1) and source its
   environment script (`export.sh` / `export.ps1` / `export.bat`).
2. Copy `main/secrets.h.example` to `main/secrets.h` and fill in your WiFi
   SSID/password. `secrets.h` is gitignored so it never gets committed.
3. Edit `main/config.h`:
   - `LATITUDE` / `LONGITUDE` — look yours up at [latlong.net](https://www.latlong.net/)
   - `LOCATION_NAME` — label shown in the top-left corner
   - `USE_FAHRENHEIT` — `1` for °F/mph, `0` for °C/km/h
   - `UTC_OFFSET_MINUTES` — minutes offset from UTC for your local time
     (no automatic DST — update this by hand when your clocks change)
4. Connect the board over USB and build/flash:

   ```bash
   idf.py set-target esp32c6
   idf.py build flash monitor
   ```

   The first build downloads the managed components (LVGL, esp_lvgl_port,
   led_strip, cJSON) via the component manager, so it needs network access.

## How it works

- Fetches current conditions + today's high/low from Open-Meteo every 10
  minutes (`REFRESH_INTERVAL_SEC` in `config.h`), retrying after 1 minute on
  failure instead of waiting the full interval.
- If a fetch fails, the last known-good reading stays on screen with a
  small red dot next to the clock indicating it's stale, rather than
  blanking the display.
- The clock in the top-right corner updates once a minute from the app's
  main loop.
- Weather icons are drawn as vector shapes (circles/lines/triangles) onto
  a small LVGL canvas rather than bitmaps, so there are no image assets to
  manage.
- The onboard RGB LED changes color to match the current condition
  (yellow = clear, blue = rain, purple = thunderstorm, etc.) and turns
  solid red if a fetch fails.
- HTTPS requests use ESP-IDF's bundled certificate store
  (`esp_crt_bundle_attach`) for proper TLS verification.

## Notes

- **Partition table**: the default single-app partition (1–1.5MB) is too
  small once WiFi, LVGL, and the mbedTLS cert bundle are linked in — this
  project ships a custom `partitions.csv` with a 2MB app partition (well
  within the board's 4MB flash).
- **Console**: this board only exposes USB-Serial-JTAG over its single
  USB-C port (no separate UART-to-USB bridge), so `sdkconfig.defaults`
  points the console there.
- **Screen orientation**: the panel is native portrait (172×320); this
  project rotates it to landscape via `esp_lvgl_port`'s `swap_xy`/`mirror_y`
  flags in `display.c`. If the image comes out mirrored or upside-down on
  your unit, flip those two booleans — that's a normal one-time tuning
  step for custom panel orientations.
- Temperatures are shown as plain numbers with a unit letter (e.g. `22C`)
  rather than a degree symbol, since relying on an unverified glyph in the
  LVGL font wasn't worth the risk of a missing-character box.
