#pragma once
#include <Arduino.h>

// Self-update of THIS ESP firmware from the osos-esp32 GitHub releases. Fetches a
// small version.json from the "latest" release, compares to FIRMWARE_VERSION_STR,
// and pulls firmware.bin via HTTPS when newer. (Distinct from github_update.{h,cpp},
// which mirrors the ZX81's menu.p.)

void   selfUpdateLoop();       // auto-check shortly after boot, then daily
String selfUpdateCheckNow();   // manual trigger (web UI); returns a status line
