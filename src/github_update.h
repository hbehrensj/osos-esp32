#pragma once
#include <Arduino.h>

// Mirror of the latest OSOS menu.p from the OpenSpand-OS GitHub releases. Fetches
// version.json, and when it names a newer integer version than the menu.p stored
// in LittleFS, downloads the menu.p asset to MENU_FS_PATH. This is independent of
// selfupdate.{h,cpp}, which updates THIS ESP's own firmware.

void     githubUpdateBegin();        // mounts LittleFS, loads the stored version
void     githubUpdateLoop();         // first check after boot, then periodic
String   githubUpdateCheckNow();     // manual trigger (web UI); status line
uint16_t githubUpdateMenuVersion();  // version of the menu.p in LittleFS (0 = none)
