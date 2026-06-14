#pragma once
// Central hardware / network configuration for the OSOS ESP32 bridge.

// ---- UART to the OpenSpand serial header (-> ZX81 / OSOS) ----------------
// OpenSpand serial is TTL (the bench link used a TTL USB-serial adapter), 38400 8N1.
// UART1 is used here; UART0 is the USB-CDC debug console on the S2.
// NOTE: getting TX/RX swapped is silent. ESP TX -> OpenSpand serial-in,
//       ESP RX <- OpenSpand serial-out. Verify the header voltage is 3.3 V
//       (direct) or level-shift the ESP RX if it is 5 V.
#define OSOS_TX_PIN      34        // ESP GPIO 34 -> OpenSpand serial RX
#define OSOS_RX_PIN      21        // ESP GPIO 21 <- OpenSpand serial TX
#define OSOS_BAUD        38400     // matches build_menu.py SERBAUD (38400 8N1)
#define OSOS_UART_NUM    1

// ---- Buttons / LED -------------------------------------------------------
#define CONFIG_BUTTON_PIN   0      // BOOT button on the LOLIN S2 Mini (active LOW)
#define CONFIG_HOLD_MS      3000   // hold BOOT this long to (re)enter WiFi setup
#define STATUS_LED_PIN      15     // verify for your board revision
#define STATUS_LED_ACTIVE_HIGH 1

// ---- Network services ----------------------------------------------------
#define WEB_PORT        80
#define WIFI_AP_NAME    "OSOS-Setup"
#define MDNS_HOSTNAME   "osos"     // reachable as http://osos.local/

// ---- OTA (this firmware, over WiFi) --------------------------------------
#ifndef OTA_PASSWORD
#define OTA_PASSWORD    "osos-ota"
#endif

// ---- ESP firmware version + self-update from THIS repo's releases --------
// Release CI injects the tag with  -D FIRMWARE_VERSION=0.2.0  (no quotes).
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION_STR "9.9.9"   // dev sentinel: higher than any release, so a local
                                       // dev OTA is never clobbered by GitHub self-update
#else
#define FW_STR2(x) #x
#define FW_STR(x)  FW_STR2(x)
#define FIRMWARE_VERSION_STR FW_STR(FIRMWARE_VERSION)
#endif
#define UPDATE_VERSION_URL \
  "https://github.com/hbehrensj/osos-esp32/releases/latest/download/version.json"
#define UPDATE_FIRMWARE_URL \
  "https://github.com/hbehrensj/osos-esp32/releases/latest/download/firmware.bin"
#define UPDATE_CHECK_INTERVAL_MS  (24UL * 60 * 60 * 1000)   // daily

// ---- ZX81 OSOS menu.p mirror (OpenSpand-OS releases) ---------------------
// The bridge mirrors the latest menu.p so the ZX81 can pull it via the 'U' verb.
#define ZXMENU_VERSION_URL \
  "https://github.com/hbehrensj/OpenSpand-OS/releases/latest/download/version.json"
#define ZXMENU_FILE_URL \
  "https://github.com/hbehrensj/OpenSpand-OS/releases/latest/download/menu.p"
#define ZXMENU_CHECK_INTERVAL_MS  (6UL * 60 * 60 * 1000)    // every 6 h
#define MENU_FS_PATH      "/menu.p"      // mirrored OSOS image (update slot)
#define PROGRAM_FS_PATH   "/program.p"   // last web-uploaded .p (program slot)

// ---- Phase 3: ZX81 text web browser --------------------------------------
// The ESP fetches a URL, renders the HTML to ZX81-friendly text (uppercased,
// 32-col word-wrapped, numbered [N] link markers) and stores it as the browse
// slot for the ZX81 to pull via I/T/X. State (URL, links, history) lives on the ESP.
#define BROWSE_FS_PATH    "/browse.txt"
#define BROWSE_COLS       32             // ZX81 screen width (word wrap)
#define BROWSE_MAX_BYTES  16384          // cap on rendered page text
#define BROWSE_MAX_LINKS  120            // numbered links per page
#define BROWSE_HOME_URL   "http://example.com"   // default entry page (set live in web UI)
