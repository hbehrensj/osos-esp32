#include "browser.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <vector>

static String              currentUrl;
static std::vector<String> links;     // resolved hrefs; [N] -> links[N-1]
static std::vector<String> history;   // back stack

String browserCurrentUrl() { return currentUrl; }
int    browserLinkCount()  { return links.size(); }
void   browserBegin()      { currentUrl = BROWSE_HOME_URL; }

// ---- text helpers ------------------------------------------------------------
static char filterChar(char c) {            // -> uppercased ZX81-printable ASCII or space
  if (c >= 'a' && c <= 'z') return c - 32;
  if (c >= 0x20 && c <= 0x7E) return c;
  return ' ';
}

static String decodeEntity(const String& e) {
  if (e == "amp")  return "&";
  if (e == "lt")   return "<";
  if (e == "gt")   return ">";
  if (e == "quot") return "\"";
  if (e == "apos" || e == "#39") return "'";
  if (e == "nbsp") return " ";
  if (e == "mdash" || e == "ndash" || e == "#8211" || e == "#8212") return "-";
  if (e.startsWith("#")) { int n = e.substring(1).toInt(); return (n >= 32 && n < 127) ? String((char)n) : String(" "); }
  return " ";
}

static String attr(const String& tag, const char* name) {
  String tl = tag; tl.toLowerCase();
  int p = tl.indexOf(String(name) + "=");
  if (p < 0) return "";
  p += strlen(name) + 1;
  if (p >= (int)tag.length()) return "";
  char q = tag[p];
  if (q == '"' || q == '\'') { int e = tag.indexOf(q, p + 1); return e < 0 ? "" : tag.substring(p + 1, e); }
  int e = p; while (e < (int)tag.length() && tag[e] != ' ' && tag[e] != '>') e++;
  return tag.substring(p, e);
}

static bool blockTag(const String& name) {
  static const char* B[] = {"p","/p","br","div","/div","li","/li","tr","/tr","ul","/ul",
    "ol","/ol","h1","/h1","h2","/h2","h3","/h3","h4","/h4","h5","/h5","h6","/h6","hr",
    "table","/table","section","/section","article","/article","header","/header",
    "footer","/footer","nav","/nav","blockquote","/blockquote","pre","/pre","title"};
  for (auto b : B) if (name == b) return true;
  return false;
}

static String resolveUrl(const String& base, String href) {
  href.trim();
  if (href.startsWith("http://") || href.startsWith("https://")) return href;
  int p = base.indexOf("://");
  if (p < 0) return href;
  if (href.startsWith("//")) return base.substring(0, base.indexOf(':') + 1) + href;
  int hostStart = p + 3;
  int pathStart = base.indexOf('/', hostStart);
  String origin = (pathStart < 0) ? base : base.substring(0, pathStart);
  if (href.startsWith("/")) return origin + href;
  String dir = base;
  int q = dir.indexOf('?'); if (q >= 0) dir = dir.substring(0, q);
  int ls = dir.lastIndexOf('/');
  dir = (ls > hostStart) ? dir.substring(0, ls + 1) : origin + "/";
  return dir + href;
}

// Word-wrap "rough" (words space-separated, '\n' = hard break) to BROWSE_COLS -> file.
static void wrapTo(File& f, const String& rough) {
  int col = 0; size_t written = 0; int i = 0, n = rough.length();
  while (i < n && written < BROWSE_MAX_BYTES) {
    while (i < n && rough[i] == ' ') i++;
    if (i >= n) break;
    if (rough[i] == '\n') {
      f.write('\n'); written++; col = 0;
      while (i < n && (rough[i] == '\n' || rough[i] == ' ')) i++;
      continue;
    }
    int s = i; while (i < n && rough[i] != ' ' && rough[i] != '\n') i++;
    int len = i - s;
    if (col > 0 && col + 1 + len > BROWSE_COLS) { f.write('\n'); written++; col = 0; }
    else if (col > 0) { f.write(' '); written++; col++; }
    for (int k = s; k < i && written < BROWSE_MAX_BYTES; k++) { f.write((uint8_t)rough[k]); written++; }
    col += len;
  }
}

// ---- streaming HTML -> ZX81 text parser --------------------------------------
// Fed char-by-char from the download so a huge <head> (eb.dk's is 71 KB!) is consumed
// and discarded while only the bounded rendered text is kept in RAM.
static int    pstate;        // 0 TEXT, 1 TAG, 2 ENTITY
static String ptag, pent;    // current tag / entity text
static bool   pskip;         // inside <script>/<style> (scan for pskipclose verbatim)
static String pskipclose;        // "</script>" or "</style>" to scan for
static int    pmatch;        // chars of pskipclose matched so far
static String prough;        // accumulated rendered text (bounded)
static String pbase;         // base URL for link resolution

static void parserReset(const String& base) {
  pstate = 0; ptag = ""; pent = ""; pskip = false; pskipclose = ""; pmatch = 0;
  prough = ""; prough.reserve(8192); pbase = base; links.clear();
}

static void processTag(const String& tag) {
  String tl = tag; tl.toLowerCase(); tl.trim();
  String name = tl;
  for (int k = 0; k < (int)name.length(); k++) {
    char ch = name[k];
    if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') { name = name.substring(0, k); break; }
  }
  if (name.endsWith("/")) name = name.substring(0, name.length() - 1);
  if (name == "script" || name == "style") {     // skip to the verbatim close tag
    pskip = true; pskipclose = "</" + name + ">"; pmatch = 0; return;
  }
  if ((int)prough.length() >= BROWSE_MAX_BYTES) return;
  if (name == "a") {
    String href = attr(tag, "href");
    if (href.length() && !href.startsWith("#") && !href.startsWith("javascript:")
        && !href.startsWith("mailto:") && (int)links.size() < BROWSE_MAX_LINKS) {
      links.push_back(resolveUrl(pbase, href));
      prough += '['; prough += String((int)links.size()); prough += ']';
    }
  } else if (blockTag(name)) {
    prough += '\n';
  }
}

static void feed(char c) {
  if (pskip) {                                // inside <script>/<style>: scan for pskipclose
    char lc = (c >= 'A' && c <= 'Z') ? c + 32 : c;   // verbatim, JS '<'/'>' ignored
    if (lc == pskipclose[pmatch]) { if (++pmatch == (int)pskipclose.length()) { pskip = false; pstate = 0; pmatch = 0; } }
    else pmatch = (lc == pskipclose[0]) ? 1 : 0;
    return;
  }
  if (pstate == 1) {                          // TAG
    if (c == '>') { processTag(ptag); pstate = 0; ptag = ""; }
    else if (ptag.length() < 400) ptag += c;
    return;
  }
  if (pstate == 2) {                          // ENTITY
    if (c == ';') { if (!pskip && (int)prough.length() < BROWSE_MAX_BYTES) prough += decodeEntity(pent); pstate = 0; pent = ""; return; }
    if (c == '<') { pstate = 1; ptag = ""; pent = ""; return; }
    pent += c;
    if (pent.length() > 10) { pstate = 0; pent = ""; }
    return;
  }
  if (c == '<') { pstate = 1; ptag = ""; return; }   // TEXT
  if (pskip) return;
  if (c == '&') { pstate = 2; pent = ""; return; }
  if ((int)prough.length() < BROWSE_MAX_BYTES) prough += filterChar(c);
}

// ---- fetch (manual redirect following) + stream-render -----------------------
static void writeError(const String& url) {
  links.clear();
  File f = LittleFS.open(BROWSE_FS_PATH, "w");
  if (f) { f.print("COULD NOT LOAD\n"); f.print(url); f.close(); }
}

static bool fetchStream(String url) {
  for (int hop = 0; hop < 6; hop++) {
    bool https = url.startsWith("https");
    WiFiClientSecure sclient;
    WiFiClient       cclient;
    if (https) sclient.setInsecure();
    HTTPClient http;
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.setConnectTimeout(8000);
    http.useHTTP10(true);
    bool ok = https ? http.begin(sclient, url) : http.begin(cclient, url);
    if (!ok) return false;
    http.setUserAgent("OSOS-ZX81/1.0");
    const char* col[] = { "Location" };
    http.collectHeaders(col, 1);
    int code = http.GET();

    if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
      String loc = http.header("Location"); http.end();
      if (!loc.length()) return false;
      url = resolveUrl(url, loc);
      continue;
    }
    if (code != HTTP_CODE_OK) { http.end(); return false; }

    parserReset(url);
    currentUrl = url;                          // final URL (post-redirect)
    WiFiClient* st = http.getStreamPtr();
    uint8_t buf[512];
    uint32_t t0 = millis(); size_t total = 0; const size_t MAXREAD = 512 * 1024;
    while ((int)prough.length() < BROWSE_MAX_BYTES && total < MAXREAD) {
      size_t avail = st->available();
      if (avail) {
        int n = st->readBytes(buf, avail > sizeof(buf) ? sizeof(buf) : avail);
        for (int i = 0; i < n; i++) feed((char)buf[i]);
        total += n; t0 = millis();
      } else if (!http.connected()) {
        break;
      } else if (millis() - t0 > 8000) {
        break;
      } else {
        delay(3);
      }
    }
    http.end();
    File f = LittleFS.open(BROWSE_FS_PATH, "w");
    if (f) { wrapTo(f, prough); f.close(); }
    Serial.printf("[brws] %s -> %u text bytes, %d links (read %u)\n",
                  url.c_str(), (unsigned)prough.length(), (int)links.size(), (unsigned)total);
    return true;
  }
  return false;
}

static bool fetchAndRender(const String& url) {
  if (WiFi.status() != WL_CONNECTED) { writeError(url); return false; }
  if (!fetchStream(url)) { writeError(url); return false; }
  return true;
}

static String normalizeUrl(String u) {
  u.trim();
  if (!u.startsWith("http://") && !u.startsWith("https://")) u = "http://" + u;
  return u;
}

void browserSetUrl(const String& url) { history.clear(); fetchAndRender(normalizeUrl(url)); }

bool browserGo(int cmd) {
  if (cmd == 0)   return fetchAndRender(currentUrl.length() ? currentUrl : String(BROWSE_HOME_URL));
  if (cmd == 255) {
    if (history.size()) { String u = history.back(); history.pop_back(); return fetchAndRender(u); }
    return fetchAndRender(currentUrl);
  }
  if (cmd >= 1 && cmd <= (int)links.size()) {
    String target = links[cmd - 1];
    history.push_back(currentUrl);
    return fetchAndRender(target);
  }
  return false;
}
