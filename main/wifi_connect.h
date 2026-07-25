#pragma once
#include <stdbool.h>
#include <stdint.h>

// Brings up WiFi station mode and blocks until connected (or gives up).
// Safe to call again after a disconnect - it reuses the existing driver
// init and just waits for the next connection.
void wifi_connect_init(void);
bool wifi_connect_wait(uint32_t timeout_ms);
bool wifi_connect_is_up(void);
