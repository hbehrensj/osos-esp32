#include "net_config.h"
#include "config.h"
#include "web_ui.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>

// Optional local WiFi credentials for direct bring-up (gitignored).
#if defined(__has_include)
#  if __has_include("secrets.h")
#    include "secrets.h"
#  endif
#endif

static String staSsid;   // remembered for the reconnect watchdog (persistent off)
static String staPsk;

// Run WiFiManager. onDemand=true forces the portal (button); otherwise autoConnect
// uses saved creds and only opens the portal if none work.
static void runPortal(bool onDemand) {
  WiFiManager wm;
  wm.setConfigPortalTimeout(onDemand ? 300 : 0);   // on-demand auto-exits after 5 min

  bool ok = onDemand ? wm.startConfigPortal(WIFI_AP_NAME)
                     : wm.autoConnect(WIFI_AP_NAME);
  if (!ok) {
    Serial.println("[net] not connected after portal; rebooting");
    delay(1000);
    ESP.restart();
  }
}

// (Re)start the mDNS responder so http://osos.local/ resolves.
static void startMdns() {
  MDNS.end();
  if (MDNS.begin(MDNS_HOSTNAME)) {
    MDNS.addService("http", "tcp", WEB_PORT);
    Serial.printf("[net] mDNS: http://%s.local/\n", MDNS_HOSTNAME);
  } else {
    Serial.println("[net] mDNS start failed");
  }
}

void netConfigBegin() {
  pinMode(CONFIG_BUTTON_PIN, INPUT_PULLUP);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(MDNS_HOSTNAME);

  bool buttonHeld = (digitalRead(CONFIG_BUTTON_PIN) == LOW);

#ifdef WIFI_SSID
  bool haveSecrets = (strlen(WIFI_SSID) > 0);
#else
  bool haveSecrets = false;
#endif

  bool connected = false;
  if (buttonHeld) {
    runPortal(true);
    connected = (WiFi.status() == WL_CONNECTED);
  } else if (haveSecrets) {
#ifdef WIFI_SSID
    // Bring-up fast path. On failure we DO NOT open the blocking portal, so the
    // device still reaches loop() and the USB serial console stays usable.
    Serial.printf("[net] secrets.h: connecting directly to '%s'\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) delay(250);
    connected = (WiFi.status() == WL_CONNECTED);
    if (!connected)
      Serial.println("[net] secrets.h connect FAILED — running offline");
#endif
  } else {
    runPortal(false);
    connected = (WiFi.status() == WL_CONNECTED);
  }

  if (connected) {
    staSsid = WiFi.SSID();
    staPsk  = WiFi.psk();
    WiFi.setAutoReconnect(true);
    startMdns();
    Serial.printf("[net] connected: %s  IP=%s\n",
                  WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
  } else {
    Serial.println("[net] OFFLINE — no WiFi; USB console + serial server still work");
  }
}

void netConfigLoop() {
  // BOOT-button hold -> re-enter the config portal (release port 80 first).
  static uint32_t pressStart = 0;
  if (digitalRead(CONFIG_BUTTON_PIN) == LOW) {
    if (pressStart == 0) pressStart = millis();
    else if (millis() - pressStart >= CONFIG_HOLD_MS) {
      Serial.println("[net] BOOT held -> entering config portal");
      webUiStop();                 // free port 80 for the portal's web UI
      runPortal(true);
      webUiResume();
      if (WiFi.status() == WL_CONNECTED) startMdns();
      pressStart = 0;
    }
  } else {
    pressStart = 0;
  }

  // Escalating reconnect watchdog: gentle retry -> radio reset -> reboot.
  static uint32_t lastCheck = 0;
  static bool     wasConnected = true;
  static uint32_t downSince = 0;
  static uint8_t  attempts = 0;

  if (millis() - lastCheck > 5000) {
    lastCheck = millis();
    bool linkUp = (WiFi.status() == WL_CONNECTED);
    bool hasIp  = (WiFi.localIP() != IPAddress((uint32_t)0));
    bool up     = linkUp && hasIp;
    if (linkUp && !hasIp)
      Serial.println("[net] associated but no IP (DHCP not renewed) — treating as down");

    if (up) {
      if (!wasConnected) {
        Serial.printf("[net] reconnected  IP=%s\n", WiFi.localIP().toString().c_str());
        startMdns();
      }
      downSince = 0; attempts = 0;
    } else if (staSsid.length()) {
      if (downSince == 0) downSince = millis();
      uint32_t downMs = millis() - downSince;
      attempts++;
      if (downMs > 300000UL) {
        Serial.println("[net] WiFi down >5 min — rebooting");
        delay(100); ESP.restart();
      } else if (attempts % 6 == 0) {
        Serial.println("[net] WiFi still down — full radio reset");
        WiFi.disconnect(true, false);
        WiFi.mode(WIFI_OFF); delay(100);
        WiFi.mode(WIFI_STA);
        WiFi.setHostname(MDNS_HOSTNAME);
        WiFi.setAutoReconnect(true);
        WiFi.begin(staSsid.c_str(), staPsk.c_str());
      } else {
        Serial.printf("[net] WiFi down %us — reconnecting to %s\n",
                      (unsigned)(downMs / 1000), staSsid.c_str());
        WiFi.begin(staSsid.c_str(), staPsk.c_str());
      }
    } else {
      WiFi.reconnect();
    }
    wasConnected = up;
  }
}
