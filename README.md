# OSOS-ESP32 — OpenSpand WiFi bridge

An **ESP32-S2 Mini** that sits permanently on the **OpenSpand** serial header and
gives a **Sinclair ZX81** running [OSOS](https://github.com/hbehrensj/OpenSpand-OS)
WiFi. It is a multi-service server the ZX81 talks to over UART; the ZX81 always
initiates (matching its `zxsvr` pull / `FAST`-mode receive).

Built on the same foundation as
[TDAI-2170_EPS32_Interface](https://github.com/hbehrensj/TDAI-2170_EPS32_Interface):
WiFiManager captive-portal config, a browser UI, espota + GitHub self-update.

## Features (Phase 1)

- 🛜 **WiFi configuration** — captive portal (`OSOS-Setup`), hold BOOT to re-enter.
- 📤 **Send programs over WiFi** — upload a `.p` in the browser, press **S** on the
  ZX81 to pull it (saved as `INBOX.P`). Replaces the bench `zxserver.sh` script.
- ⬆️ **OSOS auto-update** — the bridge mirrors the latest `menu.p` from the
  `OpenSpand-OS` GitHub releases; press **U** on the ZX81 to install it (saved as
  `MENU.P` and booted).
- 🔄 **ESP self-update + OTA** — this firmware updates itself from its own GitHub
  releases, and is flashable over WiFi (`pio run -e ota -t upload`).

Roadmap (later phases): NTP→RTC sync, and a ZX81 text terminal (Anthropic API
chat + HTTP-to-text browsing).

## Hardware

| Component | Detail |
| --------- | ------ |
| Microcontroller | ESP32-S2 Mini (Lolin/WeMos) |
| Link | TTL UART to the OpenSpand serial header, 38400 8N1 |
| Wiring | ESP **TX = GPIO 34** → OpenSpand serial-in, ESP **RX = GPIO 21** ← OpenSpand serial-out, GND ↔ GND |
| Power | USB-C (permanent) |

> Swapping TX/RX is silent (nothing works). Pins are in
> [`src/config.h`](src/config.h).

## Build & flash

```sh
pio run -t upload          # USB
pio run -e ota -t upload   # over WiFi (osos.local, OTA_PASSWORD)
```

For bench bring-up, copy `include/secrets.h.example` to `include/secrets.h` and set
your WiFi; otherwise the captive portal handles it.

## How it works

- **`serial_server`** — the `zxsvr` server on the OpenSpand UART (`I`/`T`/`X`/`U`
  verbs). See [docs/serial-protocol.md](docs/serial-protocol.md).
- **`github_update`** — mirrors the latest `menu.p` from `OpenSpand-OS` releases to
  LittleFS (the update slot).
- **`web_ui`** — status, `.p` upload (program slot), manual update checks.
- **`net_config`** — WiFiManager portal + reconnect watchdog + mDNS (`osos.local`).
- **`ota` / `selfupdate`** — this firmware's own OTA and GitHub self-update.

## See also

The ZX81 side — the launcher, the `S`/`U` keys, and the `RXSER`/`UPDQRY` machine code that
drive this bridge — lives in **[OpenSpand-OS](https://github.com/hbehrensj/OpenSpand-OS)**.

## License

GPLv3 — same as OSOS and the OpenSpand firmware.
