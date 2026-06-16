#include "library.h"
#include "config.h"
#include "serial_server.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include "miniz.h"

#define CATALOG_PATH "/catalog.tsv"
#define CATALOG_URL  "https://raw.githubusercontent.com/hbehrensj/osos-esp32/main/data/zx81-catalog.tsv"
// HTTP (not HTTPS) on purpose: archive.org's download + data nodes serve over plain HTTP,
// so the ESP avoids the mbedtls TLS handshake that was hanging it for ~60s.
#define TOSEC_BASE   "http://archive.org/download/Sinclair_ZX81_TOSEC_2012_04_23/Sinclair_ZX81_TOSEC_2012_04_23.zip/"

void libraryBegin() {}

bool libraryReady() { return LittleFS.exists(CATALOG_PATH); }

// GET url -> file, following redirects MANUALLY (fresh connection per hop + User-Agent),
// with short timeouts and a hard total budget so a stalled host fails fast (no WDT reset).
// Fills `diag` with where it got to (visible in the web UI).
static bool downloadToFile(String url, const char* path, String& diag) {
  uint32_t start = millis();
  for (int hop = 0; hop < 6; hop++) {
    if (millis() - start > 15000) { diag = "timeout"; return false; }
    bool https = url.startsWith("https");
    WiFiClientSecure sc; WiFiClient cc;
    if (https) { sc.setInsecure(); sc.setHandshakeTimeout(8); }   // bound the TLS handshake
    HTTPClient http;
    http.setReuse(false);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.setConnectTimeout(6000);
    http.setTimeout(10000);
    if (!(https ? http.begin(sc, url) : http.begin(cc, url))) { diag = "begin fail h" + String(hop); return false; }
    http.setUserAgent("OSOS-ZX81/1.0");
    const char* col[] = { "Location" };
    http.collectHeaders(col, 1);
    int code = http.GET();
    if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
      String loc = http.header("Location"); http.end();
      if (!loc.length()) { diag = "h" + String(hop) + " redir no-loc"; return false; }
      url = loc; continue;
    }
    if (code != HTTP_CODE_OK) { http.end(); diag = "h" + String(hop) + " code " + String(code); return false; }
    File f = LittleFS.open(path, "w");
    if (!f) { http.end(); diag = "fs error"; return false; }
    WiFiClient* st = http.getStreamPtr();
    uint8_t buf[512]; size_t total = 0; uint32_t t0 = millis();
    while (total < 256 * 1024) {         // raw read (chunk framing kept; dechunked in RAM later)
      int a = st->available();
      if (a > 0) { int n = st->readBytes(buf, a > (int)sizeof(buf) ? sizeof(buf) : a); if (n > 0) { f.write(buf, n); total += n; t0 = millis(); } }
      else if (!http.connected()) break;
      else if (millis() - t0 > 4000) break;
      else if (millis() - start > 15000) break;
      else delay(2);
    }
    f.close(); http.end();
    diag = "ok " + String((unsigned)total) + "B";
    if (total == 0) { LittleFS.remove(path); return false; }
    return true;
  }
  diag = "too many redirects"; return false;
}

String libraryEnsureCatalog() {
  if (libraryReady()) return libraryStatus();
  if (WiFi.status() != WL_CONNECTED) return "offline";
  String diag;
  bool ok = downloadToFile(CATALOG_URL, CATALOG_PATH, diag);
  Serial.printf("[lib] catalog download: %s (%s)\n", ok ? "ok" : "FAILED", diag.c_str());
  return ok ? libraryStatus() : String("catalog dl failed: " + diag);
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

// ---- download a title from archive.org, inflate the .P, arm the program slot ----------
static bool catalogLine(int index, String& title, String& path) {
  File f = LittleFS.open(CATALOG_PATH, "r");
  if (!f) return false;
  int i = -1; bool ok = false;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    if (++i != index) continue;
    int t1 = line.indexOf('\t'), t2 = line.indexOf('\t', t1 + 1);
    if (t1 > 0 && t2 > 0) { title = line.substring(t1 + 1, t2); path = line.substring(t2 + 1); path.trim(); ok = true; }
    break;
  }
  f.close();
  return ok;
}

static String eightThree(const String& title) {
  String n = "";
  for (size_t i = 0; i < title.length() && n.length() < 8; i++) {
    char c = title[i];
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) n += c;
  }
  return (n.length() ? n : String("GAME")) + ".P";
}

static inline uint16_t rd16(const uint8_t* p) { return p[0] | (p[1] << 8); }
static inline uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

// Find the largest *.P inside a zip (in RAM) via its central directory.
static bool zipFindMainP(const uint8_t* z, size_t zl, const uint8_t*& data,
                         uint32_t& comp, uint32_t& uncomp, uint16_t& method, String& inner) {
  int e = -1;
  for (int i = (int)zl - 22; i >= 0 && i > (int)zl - 22 - 65536; i--)
    if (z[i] == 0x50 && z[i+1] == 0x4b && z[i+2] == 0x05 && z[i+3] == 0x06) { e = i; break; }
  if (e < 0) return false;
  uint16_t n = rd16(z + e + 10);
  uint32_t cd = rd32(z + e + 16);
  uint32_t best = 0; bool found = false; size_t p = cd;
  for (int k = 0; k < n && p + 46 <= zl; k++) {
    if (!(z[p] == 0x50 && z[p+1] == 0x4b && z[p+2] == 0x01 && z[p+3] == 0x02)) break;
    uint16_t mth = rd16(z + p + 10);
    uint32_t cs = rd32(z + p + 20), us = rd32(z + p + 24);
    uint16_t fnl = rd16(z + p + 28), efl = rd16(z + p + 30), cml = rd16(z + p + 32);
    uint32_t lho = rd32(z + p + 42);
    String fn = ""; for (int j = 0; j < fnl; j++) fn += (char)z[p + 46 + j];
    String up = fn; up.toUpperCase();
    if (up.endsWith(".P") && us > best && us < 80 * 1024 && lho + 30 <= zl) {
      uint16_t lfnl = rd16(z + lho + 26), lefl = rd16(z + lho + 28);
      const uint8_t* d = z + lho + 30 + lfnl + lefl;
      if (d + cs <= z + zl) { data = d; comp = cs; uncomp = us; method = mth; inner = fn; best = us; found = true; }
    }
    p += 46 + fnl + efl + cml;
  }
  return found;
}

#define GAMEZIP_PATH "/game.zip"
#define LIB_PAGE 18
static int libPage[LIB_PAGE];
static int libPageCount = 0;

int libraryRenderSearch(int cat, const String& query) {
  libraryEnsureCatalog();                       // auto-download the catalog on first use
  String out;
  int total = librarySearch(cat, query, 0, LIB_PAGE, out);
  libPageCount = 0;
  File f = LittleFS.open(BROWSE_FS_PATH, "w");
  if (!f) return 0;
  int s = 0;
  while (s < (int)out.length() && libPageCount < LIB_PAGE) {
    int nl = out.indexOf('\n', s); if (nl < 0) break;
    String line = out.substring(s, nl); s = nl + 1;
    int t = line.indexOf('\t'); if (t < 0) continue;
    libPage[libPageCount++] = line.substring(0, t).toInt();
    f.print(libPageCount); f.print(' '); f.println(line.substring(t + 1));
  }
  if (libPageCount == 0) f.print("NO MATCHES");
  f.close();
  Serial.printf("[lib] search '%s' -> %d shown, %d total\n", query.c_str(), libPageCount, total);
  return libPageCount;
}

String libraryDownloadByPage(int num) {
  if (num < 1 || num > libPageCount) return "bad number";
  return libraryDownload(libPage[num - 1]);
}

// A ZX81 .p is the RAM image 0x4009..E_LINE-1. TOSEC dumps frequently carry trailing padding
// past E_LINE; loaded verbatim that pushes the program end past RAMTOP (0x8000) and the game
// crashes on a 16K machine. Trim to the true length from the E_LINE system variable (offset 11-12).
static size_t zxPTrueLen(const uint8_t* d, size_t n) {
  if (n < 13) return n;
  uint32_t eline = d[11] | ((uint32_t)d[12] << 8);
  if (eline <= 0x4009) return n;            // missing/garbage E_LINE -> leave as-is
  size_t len = eline - 0x4009;
  return (len >= 9 && len <= n) ? len : n;  // only ever trim, never grow; sanity floor
}

String libraryDownload(int index) {
  String title, path;
  if (!catalogLine(index, title, path)) return "bad index";
  if (WiFi.status() != WL_CONNECTED) return "offline";

  // Stream the (small) game zip to LittleFS first (one attempt, fast-failing diag).
  String diag;
  if (!downloadToFile(String(TOSEC_BASE) + path, GAMEZIP_PATH, diag)) return "dl: " + diag;
  File zf = LittleFS.open(GAMEZIP_PATH, "r");
  size_t zl = zf ? zf.size() : 0;
  if (zl < 22) { if (zf) zf.close(); LittleFS.remove(GAMEZIP_PATH); return "download failed (empty)"; }
  uint8_t* z = (uint8_t*)malloc(zl);
  if (!z) { zf.close(); LittleFS.remove(GAMEZIP_PATH); return "out of memory"; }
  zf.read(z, zl); zf.close(); LittleFS.remove(GAMEZIP_PATH);

  // archive.org serves chunked; if this isn't a PK zip yet, de-chunk in RAM.
  if (zl >= 2 && !(z[0] == 0x50 && z[1] == 0x4b)) {
    uint8_t* o = (uint8_t*)malloc(zl);
    if (o) {
      size_t op = 0, p = 0;
      while (p < zl) {
        size_t ls = p; while (p < zl && z[p] != '\n') p++; if (p >= zl) break;
        char hx[16]; int hi = 0;
        for (size_t k = ls; k < p && hi < 15; k++) { char c = z[k]; if ((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')) hx[hi++] = c; }
        hx[hi] = 0; p++;
        long cs = strtol(hx, nullptr, 16);
        if (cs <= 0) break;
        if (p + cs > zl) cs = zl - p;
        memcpy(o + op, z + p, cs); op += cs; p += cs;
        if (p < zl && z[p] == '\r') p++; if (p < zl && z[p] == '\n') p++;
      }
      free(z); z = o; zl = op;
    }
  }

  char mg[6]; snprintf(mg, sizeof(mg), "%02X%02X", z[0], z[1]);
  const uint8_t* data; uint32_t comp, uncomp; uint16_t method; String inner;
  if (!zipFindMainP(z, zl, data, comp, uncomp, method, inner)) {
    String d = String("no .P (zl=") + (unsigned)zl + " magic=" + mg + ")";
    free(z); return d;
  }
  uint8_t* out = (uint8_t*)malloc(uncomp ? uncomp : 1);
  if (!out) { free(z); return "out of memory"; }
  size_t got = 0;
  if (method == 0) { memcpy(out, data, comp); got = comp; }
  else if (method == 8) {
    // Heap-allocate the ~11KB tinfl decompressor — it does NOT fit on the 8KB loopTask
    // stack, which is why the stack-using tinfl_decompress_mem_to_mem() hung the ESP.
    tinfl_decompressor* dc = (tinfl_decompressor*)malloc(sizeof(tinfl_decompressor));
    if (!dc) { free(out); free(z); return "out of memory (tinfl)"; }
    tinfl_init(dc);
    size_t in_n = comp, out_n = uncomp;
    tinfl_status st = tinfl_decompress(dc, data, &in_n, out, out, &out_n,
                                       TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    free(dc);
    if (st != TINFL_STATUS_DONE) { free(out); free(z); return "inflate failed"; }
    got = out_n;
  } else { free(out); free(z); return "unsupported zip method"; }

  size_t trueLen = zxPTrueLen(out, got);
  if (trueLen != got) Serial.printf("[lib] trimmed padding: %u -> %u bytes (E_LINE)\n",
                                    (unsigned)got, (unsigned)trueLen);
  File f = LittleFS.open(PROGRAM_FS_PATH, "w");
  if (!f) { free(out); free(z); return "fs error"; }
  f.write(out, trueLen); f.close();
  free(out); free(z);

  String name = eightThree(title);
  serialSetProgramName(name);
  Serial.printf("[lib] #%d '%s' -> %s (%u bytes, inner '%s')\n",
                index, title.c_str(), name.c_str(), (unsigned)got, inner.c_str());
  return name + " ready (" + got + " bytes) - press S on the ZX81";
}
