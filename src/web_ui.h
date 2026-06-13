#pragma once
#include <Arduino.h>

// Browser control UI on port 80: status, .p upload (-> program slot), and manual
// "check for menu.p / firmware updates" buttons.
//
//   GET  /              -> HTML page
//   GET  /api/state     -> JSON: wifi + fw + armed program size + mirrored menu version
//   POST /upload        -> multipart .p upload, stored as the program slot
//   POST /api/menucheck -> force a OpenSpand-OS (menu.p) mirror check
//   POST /api/fwupdate  -> force an ESP firmware self-update check

void webUiBegin();
void webUiLoop();
void webUiStop();     // release port 80 (before the WiFi config portal binds it)
void webUiResume();   // re-bind after the portal closes
