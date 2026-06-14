#include "library.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <LittleFS.h>

#define CATALOG_PATH "/catalog.tsv"
#define CATALOG_URL  "https://raw.githubusercontent.com/hbehrensj/osos-esp32/main/data/zx81-catalog.tsv"

void libraryBegin() {}

bool libraryReady() { return LittleFS.exists(CATALOG_PATH); }

static bool httpGetToFile(const String& url, const char* path) {
  WiFiClientSecure c; c.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);   // raw.githubusercontent -> CDN
  http.setConnectTimeout(8000);
  if (!http.begin(c, url)) return false;
  int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); return false; }
  File f = LittleFS.open(path, "w");
  if (!f) { http.end(); return false; }
  int n = http.writeToStream(&f);
  f.close(); http.end();
  if (n <= 0) { LittleFS.remove(path); return false; }
  return true;
}

String libraryEnsureCatalog() {
  if (libraryReady()) return libraryStatus();
  if (WiFi.status() != WL_CONNECTED) return "offline";
  bool ok = httpGetToFile(CATALOG_URL, CATALOG_PATH);
  Serial.printf("[lib] catalog download: %s\n", ok ? "ok" : "FAILED");
  return ok ? libraryStatus() : String("catalog download failed");
}

String libraryStatus() {
  if (!libraryReady()) return "no catalog (press Load)";
  File f = LittleFS.open(CATALOG_PATH, "r");
  size_t s = f ? f.size() : 0; if (f) f.close();
  return String("catalog ready (") + s + " bytes)";
}

int librarySearch(int cat, const String& query, int offset, int count, String& out) {
  out = "";
  File f = LittleFS.open(CATALOG_PATH, "r");
  if (!f) return 0;
  String q = query; q.toUpperCase(); q.trim();
  int idx = -1, matches = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n'); idx++;
    int t1 = line.indexOf('\t'); if (t1 < 0) continue;
    int t2 = line.indexOf('\t', t1 + 1); if (t2 < 0) continue;
    int c = line.substring(0, t1).toInt();
    if (cat >= 0 && c != cat) continue;
    String title = line.substring(t1 + 1, t2);
    if (q.length() && title.indexOf(q) < 0) continue;
    if (matches >= offset && matches < offset + count) {
      out += String(idx); out += '\t'; out += title; out += '\n';
    }
    matches++;
  }
  f.close();
  return matches;
}
