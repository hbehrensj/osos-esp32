#include <Arduino.h>
#include <LittleFS.h>
#include "config.h"
#include "net_config.h"
#include "serial_server.h"
#include "github_update.h"
#include "browser.h"
#include "web_ui.h"
#include "ota.h"
#include "selfupdate.h"

// ---- Status LED ----------------------------------------------------------
enum LedMode { LED_BOOT, LED_CONNECTED };
static LedMode ledMode = LED_BOOT;

static inline void ledWrite(bool on) {
#if STATUS_LED_ACTIVE_HIGH
  digitalWrite(STATUS_LED_PIN, on ? HIGH : LOW);
#else
  digitalWrite(STATUS_LED_PIN, on ? LOW : HIGH);
#endif
}

static void statusLedLoop() {
  static uint32_t last = 0;
  static bool on = false;
  uint32_t period = (ledMode == LED_CONNECTED) ? 2000 : 250;   // slow blink when OK
  if (millis() - last >= period) { last = millis(); on = !on; ledWrite(on); }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("\n[OSOS] boot  fw=%s\n", FIRMWARE_VERSION_STR);

  pinMode(STATUS_LED_PIN, OUTPUT);
  ledWrite(false);

  LittleFS.begin(true);     // program slot + menu.p mirror live here
  serialServerBegin();      // OpenSpand UART — serves files even while offline
  githubUpdateBegin();      // load the stored menu.p version
  browserBegin();           // default home URL for the ZX81 text browser

  netConfigBegin();         // connect WiFi or run the captive portal
  ledMode = LED_CONNECTED;

  otaBegin();               // espota firmware uploads
  webUiBegin();             // control UI on :80
}

void loop() {
  netConfigLoop();
  otaLoop();
  selfUpdateLoop();         // this firmware, from osos-esp32 releases
  githubUpdateLoop();       // mirror the ZX81 menu.p from OpenSpand-OS releases
  serialServerLoop();       // answer the ZX81's zxsvr verbs (I/T/X/U)
  webUiLoop();
  statusLedLoop();
}
