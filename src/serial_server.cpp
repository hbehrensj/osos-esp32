#include "serial_server.h"
#include "config.h"
#include "github_update.h"
#include "browser.h"
#include "library.h"
#include <LittleFS.h>

static HardwareSerial S1(OSOS_UART_NUM);
static File   serveFile;                       // open across an I..T..X session
static String servePath = PROGRAM_FS_PATH;     // armed slot (reverts on 'X')
static String programName = "INBOX.P";         // original name of the web-uploaded .p

#define PROGRAM_NAME_PATH "/program.nam"

void serialSetProgramName(const String& fn) {
  int s = fn.lastIndexOf('/'); if (s < 0) s = fn.lastIndexOf('\\');
  String n = (s >= 0) ? fn.substring(s + 1) : fn;
  n.trim();
  programName = n.length() ? n : String("INBOX.P");
  File f = LittleFS.open(PROGRAM_NAME_PATH, "w");   // persist across reboots (like the .p)
  if (f) { f.print(programName); f.close(); }
}

String serialProgramName() { return programName; }

// ASCII -> ZX81 character code, or -1 to skip (for filename chars).
static int asciiToZx(char c) {
  if (c >= 'a' && c <= 'z') c -= 32;
  if (c >= 'A' && c <= 'Z') return 38 + (c - 'A');
  if (c >= '0' && c <= '9') return 28 + (c - '0');
  if (c == '.') return 27;
  if (c == '/') return 24;
  if (c == '-') return 22;
  return -1;
}

void serialServerBegin() {
  S1.begin(OSOS_BAUD, SERIAL_8N1, OSOS_RX_PIN, OSOS_TX_PIN);
  File f = LittleFS.open(PROGRAM_NAME_PATH, "r");   // restore last upload's name
  if (f) { String n = f.readString(); n.trim(); if (n.length()) programName = n; f.close(); }
  Serial.printf("[srv] UART%d @ %d 8N1 (RX=%d TX=%d), program '%s'\n",
                OSOS_UART_NUM, OSOS_BAUD, OSOS_RX_PIN, OSOS_TX_PIN, programName.c_str());
}

// The ZX81 sends a verb's argument bytes right behind the verb; wait briefly.
static int readByteBlocking(uint32_t timeoutMs) {
  uint32_t t0 = millis();
  while (!S1.available()) {
    if (millis() - t0 > timeoutMs) return -1;
    delay(1);
  }
  return S1.read();
}

// 'I' — (re)open the armed slot and report its length.
static void handleInfo() {
  if (serveFile) serveFile.close();
  serveFile = LittleFS.open(servePath, "r");
  uint32_t n = serveFile ? serveFile.size() : 0;
  S1.write((uint8_t)(n & 0xff));
  S1.write((uint8_t)((n >> 8) & 0xff));
  Serial.printf("[srv] I -> %u bytes (%s)\n", (unsigned)n, servePath.c_str());
}

// 'T',num,len — send one block from offset num*256 plus a 2-byte checksum.
static void handleBlock() {
  int num = readByteBlocking(2000);
  int len = readByteBlocking(2000);
  if (num < 0 || len < 0) { Serial.println("[srv] T: arg timeout"); return; }
  int n = (len == 0) ? 256 : len;

  static uint8_t buf[256];
  int got = 0;
  if (serveFile) {
    serveFile.seek((uint32_t)num * 256);
    got = serveFile.read(buf, n);
  }
  for (int i = (got < 0 ? 0 : got); i < n; i++) buf[i] = 0;   // pad a short tail

  uint16_t sum = 0;
  for (int i = 0; i < n; i++) sum += buf[i];
  S1.write(buf, n);
  S1.write((uint8_t)(sum & 0xff));
  S1.write((uint8_t)((sum >> 8) & 0xff));
}

// 'U',verLo,verHi — auto-update query. Arm the update slot if a newer menu.p exists.
static void handleUpdateQuery() {
  int lo = readByteBlocking(2000);
  int hi = readByteBlocking(2000);
  if (lo < 0 || hi < 0) { Serial.println("[srv] U: arg timeout"); return; }
  uint16_t zxVer   = (uint16_t)lo | ((uint16_t)hi << 8);
  uint16_t menuVer = githubUpdateMenuVersion();   // 0 if no mirror present
  bool avail = (menuVer > zxVer) && LittleFS.exists(MENU_FS_PATH);
  if (avail) servePath = MENU_FS_PATH;            // arm update slot for the next 'I'
  S1.write((uint8_t)(avail ? 1 : 0));
  Serial.printf("[srv] U zx=%u menu=%u -> %s\n",
                zxVer, menuVer, avail ? "UPDATE" : "uptodate");
}

// 'B',cmd — browser command (0=reload, 1..N=follow link N, 255=back). Render the page
// into the browse slot and arm it for the next I/T/X pull. Reply 1 status byte.
static void handleBrowse() {
  int cmd = readByteBlocking(2000);
  if (cmd < 0) { Serial.println("[srv] B: arg timeout"); return; }
  bool ok = browserGo(cmd);
  if (ok) servePath = BROWSE_FS_PATH;        // arm browse slot for the next 'I'
  S1.write((uint8_t)(ok ? 1 : 0));
  Serial.printf("[srv] B cmd=%d -> %s (%d links)\n", cmd, ok ? "ok" : "fail", browserLinkCount());
}

// 'G',len,bytes… — set the browser URL typed on the ZX81 (ASCII). Fetch+render it,
// arm the browse slot, reply 1 status byte.
static void handleSetUrl() {
  int len = readByteBlocking(2000);
  if (len < 0) { Serial.println("[srv] G: len timeout"); return; }
  String url;
  for (int i = 0; i < len; i++) {
    int c = readByteBlocking(2000);
    if (c < 0) { Serial.println("[srv] G: body timeout"); return; }
    url += (char)c;
  }
  browserSetUrl(url);                          // normalizes scheme, fetches, renders
  servePath = BROWSE_FS_PATH;                  // arm browse slot for the next 'I'
  S1.write((uint8_t)1);
  Serial.printf("[srv] G '%s'\n", url.c_str());
}

// 'N' — reply with the program slot's filename as ZX81 char codes (len byte + bytes).
static void handleName() {
  uint8_t out[32]; int n = 0;
  for (size_t i = 0; i < programName.length() && n < 31; i++) {
    int z = asciiToZx(programName[i]);
    if (z >= 0) out[n++] = (uint8_t)z;
  }
  S1.write((uint8_t)n);
  if (n) S1.write(out, n);
  Serial.printf("[srv] N -> '%s' (%d codes)\n", programName.c_str(), n);
}

// 'L',cat,len,bytes — library search. Render the results as a numbered list into the
// browse slot (for the ZX81 to pull + show), reply the count (1 byte). cat 255 = all.
static void handleLibList() {
  int cat = readByteBlocking(2000);
  int len = readByteBlocking(2000);
  if (cat < 0 || len < 0) return;
  String q;
  for (int i = 0; i < len; i++) { int c = readByteBlocking(2000); if (c < 0) return; q += (char)c; }
  int n = libraryRenderSearch(cat == 255 ? -1 : cat, q);
  servePath = BROWSE_FS_PATH;
  S1.write((uint8_t)(n > 255 ? 255 : n));
}

// 'D',num — download the Nth shown result into the program slot; reply 1 status byte.
static void handleLibGet() {
  int num = readByteBlocking(3000);
  if (num < 0) return;
  String r = libraryDownloadByPage(num);
  bool ok = r.indexOf("ready") >= 0;
  if (ok) servePath = PROGRAM_FS_PATH;       // arm the freshly downloaded game for I/T/X
  S1.write((uint8_t)(ok ? 1 : 0));
  Serial.printf("[srv] D %d -> %s\n", num, r.c_str());
}

void serialServerLoop() {
  while (S1.available()) {
    int b = S1.read();
    switch (b) {
      case 'I': handleInfo();        break;
      case 'T': handleBlock();       break;
      case 'U': handleUpdateQuery(); break;
      case 'B': handleBrowse();      break;
      case 'G': handleSetUrl();      break;
      case 'N': handleName();        break;
      case 'L': handleLibList();     break;
      case 'D': handleLibGet();      break;
      case 'X':
        if (serveFile) serveFile.close();
        servePath = PROGRAM_FS_PATH;             // revert to program slot
        Serial.println("[srv] X (done)");
        break;
      default: break;                            // ignore stray bytes / line glitches
    }
  }
}
