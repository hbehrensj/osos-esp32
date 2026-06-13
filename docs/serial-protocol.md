# OSOS ↔ ESP32 serial protocol

The bridge sits on the OpenSpand serial header and speaks the **charlierobson
`zxsvr`** protocol (ZXpand-Vitamins/serial-server). The **ZX81 (OSOS) is the
client and drives every exchange**; the ESP only answers verbs. This matches the
ZX81's pull/`FAST`-mode receive (no per-byte flow control needed — the per-block
round-trip is the pacing).

## Link settings

| Setting | Value |
| ------- | ----- |
| Baud | 38400 |
| Frame | 8N1 |
| Pins (ESP) | TX = GPIO 34 → OpenSpand serial-in, RX = GPIO 21 ← OpenSpand serial-out |
| Level | TTL (verify 3.3 V; level-shift the ESP RX if the header is 5 V) |

## Verbs (ZX81 → ESP)

| Verb | Args (ZX81 → ESP) | Reply (ESP → ZX81) | Meaning |
| ---- | ----------------- | ------------------ | ------- |
| `'I'` (0x49) | — | `len_lo, len_hi` | Length of the armed file (2 bytes, little-endian). |
| `'T'` (0x54) | `blockNum, blockLen` | `blockLen data bytes`, then `sum_lo, sum_hi` | One block from offset `blockNum*256`. `blockLen` 0 means 256. Checksum = 16-bit sum of the block's bytes. |
| `'X'` (0x58) | — | — | End of transfer; the ESP reverts to the program slot. |
| `'U'` (0x55) | `verLo, verHi` | `status` (1 byte) | OSOS auto-update query. The ZX81 sends its running version; the ESP replies `1` if a newer `menu.p` is mirrored (and **arms the update slot** for the next `I`/`T`/`X`), else `0`. |

## Slots

The ESP serves one of two files in LittleFS:

- **program slot** (`/program.p`, default) — the last `.p` uploaded via the web UI.
  The ZX81's `S` key pulls it and saves it as `INBOX.P`.
- **update slot** (`/menu.p`) — the latest OSOS image mirrored from the `OpenSpand-OS`
  GitHub releases. Armed by `'U'`; the ZX81's `U` key pulls it, saves it as `MENU.P`,
  and `LOAD`s it. Reverts to the program slot after the next `'X'`.

A bare `zxsvr.exe` / `zxserver.sh` that only sends `I`/`T`/`X` (never `'U'`) transfers
the program slot unchanged — useful for bench transfers without the ESP's web UI.

## ZX81 side

Implemented in [`OpenSpand-OS`](https://github.com/hbehrensj/OpenSpand-OS) `build_menu.py`:
`RXSER` (the `I`/`T`/`X` pull, run in `FAST` mode) and `UPDQRY` (the `'U'` query).
