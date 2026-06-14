# OSOS-ESP32 — OpenSpand WiFi bridge

An **ESP32-S2 Mini** that sits permanently on the **OpenSpand** serial header and
gives a **Sinclair ZX81** running [OSOS](https://github.com/hbehrensj/OpenSpand-OS)
WiFi. It is a multi-service server the ZX81 talks to over UART; the ZX81 always
initiates (matching its `zxsvr` pull / `FAST`-mode receive).

It provides WiFiManager captive-portal config, a browser UI, and espota + GitHub self-update.

## Features

- 🛜 **WiFi configuration** — captive portal (`OSOS-Setup`), hold BOOT to re-enter.
- 📤 **Send programs over WiFi** — upload a `.p` in the browser, press **S** on the ZX81 to
  pull it. The original filename is preserved (the ZX81 saves e.g. `CHESS.P`, not `INBOX.P`).
- ⬆️ **OSOS auto-update** — the bridge mirrors the latest `menu.p` from the OpenSpand-OS
  GitHub releases; press **U** on the ZX81 to install it and reboot into the new version.
- 🌐 **Web browser** — fetches a URL and stream-renders the HTML to ZX81 text with numbered
  links; press **B** on the ZX81 to browse (follow links by number, or type a URL). Handles
  heavy modern sites (a streaming parser skips huge `<head>`/`<script>` sections).
- 📚 **Online program library** — search the ZX81 TOSEC archive and download games straight
  to the SD card; press **L** on the ZX81 (or use the web UI). The bridge fetches each game's
  zip from archive.org, inflates the `.p`, gives it an 8.3 name, and arms it for the next pull.
- 🔄 **ESP self-update + OTA** — this firmware updates itself from its own GitHub releases,
  and is flashable over WiFi.

Roadmap: NTP→RTC clock sync, and an Anthropic-API chat terminal for the ZX81.

## Hardware

| Component | Detail |
| --------- | ------ |
| Microcontroller | ESP32-S2 Mini (Lolin/WeMos) |
| Link | TTL UART to the OpenSpand serial header, 38400 8N1 |
| Enclosure | 3D-printed case — [`3D print case/osos Wifi.stl`](3D%20print%20case/) |

The board connects to the OpenSpand with **4 wires** (no separate USB needed — it is powered
from the OpenSpand's 5 V rail into the S2's `VBUS`):

| OpenSpand | ESP32-S2 | Purpose |
| --------- | -------- | ------- |
| GND | GND | common ground |
| serial-out | **RX = GPIO 21** | ZX81 → ESP |
| serial-in | **TX = GPIO 34** | ESP → ZX81 |
| 5 V — **pin 6 of the I²C header** | **VBUS** | power |

> Swapping TX/RX is silent (nothing works). The 5 V → `VBUS` line powers the board, so you
> can leave USB-C unplugged in normal use. Pins are in [`src/config.h`](src/config.h).

## Flashing a new device

Download **`osos-esp32-factory.bin`** from the
[latest release](https://github.com/hbehrensj/osos-esp32/releases/latest) and flash a blank
ESP32-S2 Mini. Enter download mode first (hold **0**, tap **RST**, release **0**), then:

```sh
esptool.py --chip esp32s2 write_flash 0x0 osos-esp32-factory.bin
```

…or flash it from the browser with [esptool-js](https://espressif.github.io/esptool-js/) (no
install). On first boot it starts the **`OSOS-Setup`** WiFi captive portal — join it and enter
your network; afterwards the bridge is reachable at **http://osos.local/**. Wire it to the
OpenSpand serial header (see Hardware) and you're ready to press `S`/`U`/`B`/`L` on the ZX81.

## Build from source

```sh
pio run -t upload          # USB
pio run -e ota -t upload   # over WiFi (osos.local, OTA_PASSWORD)
```

For bench bring-up, copy `include/secrets.h.example` to `include/secrets.h` and set your WiFi;
otherwise the captive portal handles it. Push a `vX.Y.Z` tag to publish a GitHub Release
(`firmware.bin` + `osos-esp32-factory.bin` + `version.json`) via the CI workflow.

## How it works

- **`serial_server`** — the `zxsvr` server on the OpenSpand UART. Verbs: `I`/`T`/`X` (file
  pull), `U` (OSOS auto-update query), `B`/`G` (browser command / set URL), `N` (report the
  program's filename). See [docs/serial-protocol.md](docs/serial-protocol.md).
- **`browser`** — fetches a URL and stream-parses the HTML into ZX81 text + numbered links.
- **`github_update`** — mirrors the latest `menu.p` from OpenSpand-OS releases (update slot).
- **`web_ui`** — status, `.p` upload (program slot), browser preview, manual update checks.
- **`net_config`** — WiFiManager portal + reconnect watchdog + mDNS (`osos.local`).
- **`ota` / `selfupdate`** — this firmware's own OTA and GitHub self-update.

## See also

The ZX81 side — the launcher, the `S`/`U` keys, and the `RXSER`/`UPDQRY` machine code that
drive this bridge — lives in **[OpenSpand-OS](https://github.com/hbehrensj/OpenSpand-OS)**.

## License

GPLv3 — same as OSOS and the OpenSpand firmware.
