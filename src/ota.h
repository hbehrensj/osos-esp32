#pragma once
#include <Arduino.h>

// Arduino OTA (over-the-air) firmware updates over WiFi. After otaBegin(), the
// device is flashable with:  pio run -e ota -t upload  (espota, OTA_PASSWORD).

void otaBegin();   // no-op until WiFi is connected
void otaLoop();
