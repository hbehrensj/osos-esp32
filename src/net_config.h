#pragma once
#include <Arduino.h>

// WiFi connection + on-device configuration portal (WiFiManager captive portal).
// Boots into one of two modes:
//   * Normal      - connects to stored WiFi, then starts services.
//   * Config (AP) - captive portal at "OSOS-Setup" to enter WiFi credentials.
// Config mode is entered on first boot (no creds) or by holding BOOT (GPIO 0)
// for CONFIG_HOLD_MS. A reconnect watchdog recovers a dropped/wedged link.

void netConfigBegin();   // blocks until connected (may run the captive portal)
void netConfigLoop();    // button-hold -> re-enter portal; reconnect watchdog
