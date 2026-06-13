#include "browser.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <vector>

static String              currentUrl;
static std::vector<String> links;     // resolved hrefs, 1-based as shown ([N] -> links[N-1])
static std::vector<String> history;   // back stack

String browserCurrentUrl() { return currentUrl; }
int    browserLinkCount()  { return links.size(); }
void   browserBegin()      { currentUrl = BROWSE_HOME_URL; }

// ---- HTTP fetch (http or https), capped --------------------------------------
static bool fetchUrl(const String& url, String& body) {
  body = "";
  if (WiFi.status() != WL_CONNECTED) return false;
  bool https = url.startsWith("https");
  WiFiClientSecure sclient;
  WiFiClient       cclient;
  if (https) sclient.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setConnectTimeout(8000);
  http.useHTTP10(true);
  bool ok = https ? http.begin(sclient, url) : http.begin(cclient, url);
  if (!ok) return false;
  http.setUserAgent("OSOS-ZX81/1.0");
  int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); return false; }

  const size_t CAP = 40 * 1024;
  body.reserve(8192);
  WiFiClient* stream = http.getStreamPtr();
  uint8_t buf[512];
  uint32_t t0 = millis();
  while (body.length() < CAP) {
    size_t avail = stream->available();
    if (avail) {
      int n = stream->readBytes(buf, avail > sizeof(buf) ? sizeof(buf) : avail);
      if (n > 0) { body.concat((const char*)buf, n); t0 = millis(); }
    } else if (!http.connected()) {
      break;
    } else if (millis() - t0 > 8000) {
      break;
    } else {
      delay(5);
    }
  }
  http.end();
  return body.length() > 0;
}

// ---- helpers -----------------------------------------------------------------
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

// Word-wrap the rough text (words separated by spaces, '\n' = hard break) to BROWSE_COLS,
// writing to the browse-slot file. Collapses runs of whitespace.
static void wrapTo(File& f, const String& rough) {
  int col = 0; size_t written = 0; int i = 0, n = rough.length();
  while (i < n && written < BROWSE_MAX_BYTES) {
    while (i < n && rough[i] == ' ') i++;                 // collapse spaces
    if (i >= n) break;
    if (rough[i] == '\n') { f.write('\n'); written++; col = 0; while (i < n && rough[i] == '\n') i++; continue; }
    int s = i; while (i < n && rough[i] != ' ' && rough[i] != '\n') i++;
    int len = i - s;
    if (col > 0 && col + 1 + len > BROWSE_COLS) { f.write('\n'); written++; col = 0; }
    else if (col > 0) { f.write(' '); written++; col++; }
    for (int k = s; k < i && written < BROWSE_MAX_BYTES; k++) { f.write((uint8_t)rough[k]); written++; }
    col += len;
  }
}

// ---- HTML -> ZX81 text, with numbered links ----------------------------------
static void renderHtml(const String& html, const String& baseUrl) {
  links.clear();
  String rough; rough.reserve(8192);
  int n = html.length(), i = 0, skip = 0;
  while (i < n && (int)rough.length() < BROWSE_MAX_BYTES) {
    char c = html[i];
    if (c == '<') {
      int j = html.indexOf('>', i + 1);
      if (j < 0) break;
      String tag = html.substring(i + 1, j);
      i = j + 1;
      String tl = tag; tl.toLowerCase(); tl.trim();
      String name = tl;                                  // tag name = up to first whitespace
      for (int k = 0; k < (int)name.length(); k++) {
        char ch = name[k];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') { name = name.substring(0, k); break; }
      }
      if (name.endsWith("/")) name = name.substring(0, name.length() - 1);
      if (name == "script" || name == "style") { skip++; continue; }
      if (name == "/script" || name == "/style") { if (skip) skip--; continue; }
      if (skip) continue;
      if (name == "a") {
        String href = attr(tag, "href");
        if (href.length() && !href.startsWith("#") && !href.startsWith("javascript:")
            && !href.startsWith("mailto:") && links.size() < BROWSE_MAX_LINKS) {
          links.push_back(resolveUrl(baseUrl, href));
          rough += '['; rough += String((int)links.size()); rough += ']';
        }
      } else if (blockTag(name)) {
        rough += '\n';
      }
      continue;
    }
    if (skip) { i++; continue; }
    if (c == '&') {
      int j = html.indexOf(';', i + 1);
      if (j > 0 && j - i <= 10) { rough += decodeEntity(html.substring(i + 1, j)); i = j + 1; continue; }
    }
    rough += filterChar(c);
    i++;
  }
  File f = LittleFS.open(BROWSE_FS_PATH, "w");
  if (f) { wrapTo(f, rough); f.close(); }
}

static bool fetchAndRender(const String& url) {
  String body;
  if (!fetchUrl(url, body)) {
    links.clear();
    File f = LittleFS.open(BROWSE_FS_PATH, "w");
    if (f) { f.print("COULD NOT LOAD\n"); f.print(url); f.close(); }
    return false;
  }
  renderHtml(body, url);
  currentUrl = url;
  return true;
}

void browserSetUrl(const String& url) { history.clear(); fetchAndRender(url); }

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
