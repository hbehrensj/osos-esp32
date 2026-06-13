#include "serial_server.h"
#include "config.h"
#include "github_update.h"
#include "browser.h"
#include <LittleFS.h>

static HardwareSerial S1(OSOS_UART_NUM);
static File   serveFile;                       // open across an I..T..X session
static String servePath = PROGRAM_FS_PATH;     // armed slot (reverts on 'X')

void serialServerBegin() {
  S1.begin(OSOS_BAUD, SERIAL_8N1, OSOS_RX_PIN, OSOS_TX_PIN);
  Serial.printf("[srv] UART%d @ %d 8N1 (RX=%d TX=%d)\n",
                OSOS_UART_NUM, OSOS_BAUD, OSOS_RX_PIN, OSOS_TX_PIN);
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

void serialServerLoop() {
  while (S1.available()) {
    int b = S1.read();
    switch (b) {
      case 'I': handleInfo();        break;
      case 'T': handleBlock();       break;
      case 'U': handleUpdateQuery(); break;
      case 'B': handleBrowse();      break;
      case 'X':
        if (serveFile) serveFile.close();
        servePath = PROGRAM_FS_PATH;             // revert to program slot
        Serial.println("[srv] X (done)");
        break;
      default: break;                            // ignore stray bytes / line glitches
    }
  }
}
