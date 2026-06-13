#pragma once
#include <Arduino.h>

// Minimal Lynx-style web browser for the ZX81 terminal. Fetches a URL, renders the
// HTML to ZX81-friendly text (uppercased + filtered to the ZX81 character set, word-
// wrapped to BROWSE_COLS, with numbered "[N]" link markers) and stores it as
// BROWSE_FS_PATH for the ZX81 to pull via the I/T/X protocol. All state — current URL,
// the numbered link table, and the back history — lives here; the ZX81 just sends a
// one-byte command (0 = reload, 1..N = follow link N, 255 = back).

void   browserBegin();                    // set the default home URL (no fetch yet)
void   browserSetUrl(const String& url);  // set + fetch an entry page (from the web UI)
bool   browserGo(int cmd);                // 0=reload, 1..N=follow link N, 255=back
String browserCurrentUrl();
int    browserLinkCount();
