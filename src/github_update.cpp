#include "github_update.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Preferences.h>

static Preferences prefs;
static uint16_t storedMenuVer = 0;   // version of the menu.p currently in LittleFS

uint16_t githubUpdateMenuVersion() { return storedMenuVer; }

static void loadStored() {
  prefs.begin("osos", true);
  storedMenuVer = prefs.getUShort("menuver", 0);
  prefs.end();
  if (!LittleFS.exists(MENU_FS_PATH)) storedMenuVer = 0;   // file gone -> force re-fetch
}

// Download the menu.p asset to LittleFS. Frees the manifest TLS client BEFORE this
// (see doCheck) so two large TLS buffers never coexist and exhaust the heap.
static bool downloadMenu(uint16_t newVer) {
  WiFiClientSecure client; client.setInsecure();      // GitHub TLS, no pinning
  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setConnectTimeout(8000);
  if (!http.begin(client, ZXMENU_FILE_URL)) return false;
  int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); return false; }

  File f = LittleFS.open(MENU_FS_PATH, "w");
  if (!f) { http.end(); return false; }
  int written = http.writeToStream(&f);
  f.close(); http.end();
  if (written <= 0) { LittleFS.remove(MENU_FS_PATH); return false; }

  storedMenuVer = newVer;
  prefs.begin("osos", false); prefs.putUShort("menuver", newVer); prefs.end();
  Serial.printf("[zxupd] menu.p v%u downloaded (%d bytes)\n", newVer, written);
  return true;
}

static String doCheck() {
  if (WiFi.status() != WL_CONNECTED) return "offline";

  uint16_t remote = 0;
  {
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http;
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http.setConnectTimeout(8000);
    if (!http.begin(client, ZXMENU_VERSION_URL)) return "manifest begin failed";
    int code = http.GET();
    if (code != HTTP_CODE_OK) { http.end(); return String("manifest HTTP ") + code; }
    String body = http.getString();
    http.end();

    JsonDocument d;
    if (deserializeJson(d, body)) return "bad manifest JSON";
    String v = d["version"] | "0";                 // tolerate string or number
    remote = (uint16_t)v.toInt();
  }
  if (remote == 0) return "no version in manifest";

  if (remote <= storedMenuVer && LittleFS.exists(MENU_FS_PATH))
    return String("menu.p up to date (v") + storedMenuVer + ")";

  return downloadMenu(remote) ? String("menu.p updated to v") + remote
                              : "menu.p download failed";
}

String githubUpdateCheckNow() {
  String s = doCheck();
  Serial.printf("[zxupd] %s\n", s.c_str());
  return s;
}

void githubUpdateBegin() {
  if (!LittleFS.begin(true)) Serial.println("[zxupd] LittleFS mount failed");
  loadStored();
  Serial.printf("[zxupd] stored menu.p version: %u\n", storedMenuVer);
}

void githubUpdateLoop() {
  static uint32_t last = 0;
  static bool first = true;
  uint32_t now = millis();
  if (first && WiFi.status() == WL_CONNECTED && now > 8000) {
    first = false; last = now; githubUpdateCheckNow();      // shortly after boot
  } else if (now - last > ZXMENU_CHECK_INTERVAL_MS) {
    last = now; githubUpdateCheckNow();                     // periodic
  }
}
