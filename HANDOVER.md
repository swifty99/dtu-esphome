# Handover — HM-1200 (serial 116182806989) over ESPHome Hoymiles DTU

_Last updated: 2026-06-28 (evening). Context: porting Ahoy-DTU to a native ESPHome component
(`components/hoymiles_dtu`). Goal: get the HM-1200 (4-channel, nRF24) publishing live
telemetry. It responds to Ahoy today. Working tree is uncommitted WIP on `main`._

## TL;DR status

1. **Root cause #1 — wrong nRF address (serial parsed as decimal, not BCD). FIXED + PROVEN.**
2. **Root cause #2 — RX listener swept all channels instead of parking. FIX FLASHED + VERIFIED
   RUNNING on hardware — but it is NOT the blocker.** The RX park/pendulum/sweep algo executes
   exactly as designed.
3. **Real blocker (diagnosed this session): we cannot ESTABLISH THE LINK standalone.** With
   Ahoy off, every request is `MAX_RT` (no ACK) on all channels, at any cadence. The inverter
   only ACKs us while Ahoy is actively holding the link. We lack Ahoy's TX channel/timing sync.
4. **Next step = port Ahoy's time/sequence-based TX channel selection + retry state machine,
   then re-test standalone.** See "THE PLAN" below. This is a real implementation chunk.

## Current device / working-tree state (read before resuming)

- **Board is alive on WiFi + OTA**, no USB needed: `hm-link-probe.local` = `192.168.178.74`.
  Flash/log with plain `esphome … --device hm-link-probe.local` (see commands below).
- **Currently flashed = a temporary DIAGNOSTIC build** of `hm-link-probe.yaml`:
  - `poll_interval: 1s` (normal is 15s) — was a cadence experiment.
  - extra `interval:` firing `hoymiles_dtu.radio_dump` every 15s (register dump).
- **Diagnostic scaffolding added to the component this session** (decide keep-vs-remove before
  shipping; harmless but noisy):
  - `hoymiles_dtu.cpp`: `NRF_REG_RPD = 0x09`; `poll_rx_` samples RPD each iteration; exchange
    summary logs `rpd=<hits> rpdmask=0x<perchannel-bitmask>`; `read_available_packets_` logs
    `RX payload ch=<RF_CH> …`; `begin_rx_window_` resets the RPD counters.
  - `hoymiles_dtu.h`: members `rpd_hits_`, `rpd_channel_mask_`.
  - (The temporary `RX_INITIAL_PENDULUM_MS`/`RX_CHANNEL_DWELL_MS` tweaks were reverted to
    85/5.)
- **To restore a clean state:** set `hm-link-probe.yaml` `poll_interval` back to `15s`, drop
  the `radio_dump` interval, and (optionally) strip the RPD/`RX payload ch=` logging + the two
  `rpd_*` members. The clang "file not found: sensor.h" diagnostics in the editor are an IDE
  include-path artifact only — `esphome compile` is the source of truth and passes.

### Commands (copy/paste)

```sh
cd /Users/jan/dev/repos/dtu-esphome
esphome compile tests/manual/hm4_readonly/hm-link-probe.yaml          # ~10-17s incremental
esphome upload  tests/manual/hm4_readonly/hm-link-probe.yaml --device hm-link-probe.local
esphome logs    tests/manual/hm4_readonly/hm-link-probe.yaml --device hm-link-probe.local
.venv/bin/pytest tests/unit -q                                        # 10 passing
```
`esphome` is system-wide (`/opt/homebrew/bin/esphome`, 2026.6.2). macOS has **no `timeout`** —
capture logs with a backgrounded `esphome logs … & sleep N; kill %1` pattern.

## THE PLAN (resume here): give us Ahoy's link discipline

The inverter listens for DTU requests on a channel that advances on a time/sequence schedule;
a DTU must transmit on the channel the inverter is on *at that moment*. Ahoy tracks this and
keeps the inverter synced — which is why our stray requests only ACK while Ahoy runs. Our
component just does blind `(idx+1)%5` rotation every poll, so standalone it almost never
coincides with the inverter's listen window.

Port from Ahoy (reference only — keep our clean ESPHome-native impl, do not copy source):

1. **TX channel selection** — Ahoy `src/hm/NrfRadio.h` / `Radio.h`: how `mTxChIdx` is advanced
   per `sendPacket`, the `getTxNxtChannel()` rule, and the per-inverter channel **heuristic**
   (`txRfChId` / channel quality) that biases toward the channel that last worked. Seed it with
   the channels we already saw ACK: **61 / 75 / 40** (histogram below).
2. **Retry / timeout state machine** — Ahoy `src/hm/Communication.h`: the send→wait→retry loop
   and timing constants (it resends quickly on no-ACK rather than waiting a full poll interval).
   Our `request_state_` machine in `hoymiles_dtu.cpp` (`start_request_`/`poll_tx_`/`poll_rx_`)
   is where this goes; on `MAX_RT`, retry on the next/best channel immediately instead of
   ending the exchange and waiting for the next `poll_interval`.
3. **Test STANDALONE (Ahoy OFF).** Success ladder: standalone `TX_DS` → `RX payload ch=…` →
   `Accepted HM frame id=N count=n/7` → `Exchange … complete` → telemetry. Only once we hold
   `TX_DS` without Ahoy does the (already-correct) RX park/sweep algo get its real test; re-check
   `rpd`/`RX_DR` then. Keep Ahoy OFF during testing — when it runs, it owns the session and the
   inverter streams data to Ahoy's address, never ours.

## Diagnosis evidence (so you don't re-run it)

Hardware verification 2026-06-28 evening, HM-1200 confirmed awake/producing. Each row observed
on-device:

| Observation | Evidence |
|---|---|
| New RX park/pendulum/sweep algo runs correctly | TX byte-identical to Ahoy, parks on `(txChIdx+3)%5` (tx61→rx23, tx75→rx40, tx40→rx3) |
| Inverter awake & reachable | Ahoy ON + live power → **41 `TX_DS` / 38 `MAX_RT`** at 1s cadence |
| Inverter's current preferred listen channels | `TX_DS` histogram: ch **40/61/75 = 11/14/14**, ch 3/23 ≈ 0 |
| **Standalone we get NOTHING** | Ahoy OFF, 1s cadence → **89 exchanges, 100% `MAX_RT`, 0 `TX_DS`** |
| Not nightfall / inverter-asleep | ACKs return the instant Ahoy re-engages (caveat closed) |
| Even when we ACK (Ahoy on), 0 data frames | data streams to Ahoy's DTU address, not ours |

**Ruled out on-device (don't rabbit-hole these again):**
- **Channel coverage** of the *reply* — a build sweeping all 5 channels from t=0 for the full
  520 ms window still got 0 frames.
- **RX address / pipe / data rate** — live `radio_dump`:
  `EN_AA=0x3F EN_RXADDR=0x03 SETUP_AW=0x03 SETUP_RETR=0x3F RF_SETUP=0x26 (250 kbps, max PA)
  DYNPD=0x3F FEATURE=0x04 RX_P0=01 82 80 69 89 (inv) RX_P1=01 83 91 54 60 (DTU) TX=01 82 80 69 89`.
  P1 is the correct DTU return address, pipe 1 enabled, auto-ack + dynamic payload on.
- **RX pipe-1 address byte order** — self-consistent with the proven TX convention: inverter
  addr `01 82 80 69 89` = `0x01 + reverse(`request bytes `89 69 80 82`)` (confirmed by ACK);
  same rule on our embedded DTU bytes `60 54 91 83` ⇒ `01 83 91 54 60` = our P1.
- **Request bytes / CRC** — `hm_build_realtime_request` matches Ahoy: `0x15` + inv-id (BE @1) +
  dtu-id (LE @5) + `80 0B 00` + timestamp **LE @12** (matches Ahoy `CP_U32_LittleEndian`, NOT a
  bug) + CRC16/Modbus + CRC8. Unit test `test_realtime_request_uses_ahoy_byte_order` pins it.
  (One residual nit vs Ahoy: Ahoy's `initPacket` encodes BOTH ids with `CP_U32_BigEndian`; we
  use LE for the dtu-id @5. Harmless for our self-consistent reception, but the only remaining
  byte-level difference if the inverter ever turns out to be dtu-id-sensitive.)
- **Cadence** — 1s polling didn't help (above). Faster-alone is not the fix.

## Background: the two prior root causes

### Root cause #1 — serial must be packed BCD (DONE, verified on HW)
Hoymiles serials are packed BCD; Ahoy parses the printed digits as base-16. nRF address =
`0x01` + low 4 serial bytes, reversed:
- Decimal `116182806989` → `0x1B0D08F1CD` → `01 0D 08 F1 CD` ❌ (old config path)
- BCD `0x116182806989` → `01 82 80 69 89` ✅ (Ahoy-correct, inverter ACKs)
Bug was the config path (`__init__.py validate_serial → parse_serial`, decimal); now uses
`parse_serial_bcd`. See memory `hoymiles-serial-bcd`.

### Root cause #2 — RX park/pendulum/sweep (IMPLEMENTED + VERIFIED RUNNING)
Channels `{3,23,40,61,75}`; HM `rxOffset = 3`. `poll_rx_`/`begin_rx_window_` now: park on
`(txChIdx+3)%5`, pendulate to ≤1 neighbor for the first `RX_INITIAL_PENDULUM_MS=85`, then
reverse-sweep all five every `RX_CHANNEL_DWELL_MS=5` until `RX_TIMEOUT_MS=520`. Correct and
confirmed on-device — just not the bottleneck (the bottleneck is link establishment, above).

## Changes in the tree (uncommitted)

Prior session (the real fixes):
- `components/hoymiles_dtu/__init__.py` — `validate_serial` uses `parse_serial_bcd`;
  `radio_send_request` default `serial_format` → `bcd`.
- `components/hoymiles_dtu/hoymiles_dtu.cpp` — new `poll_rx_` park/pendulum/sweep; serial logs
  `%012llX`.
- `tests/unit/test_protocol.py` — `test_inverter_config_serial_is_bcd_packed` (+ others; 10 pass).
- `tests/manual/hm4_readonly/hm-link-probe.yaml` — WiFi+OTA probe (model hm_1200, serial,
  pa_level max). Includes `packages: device_base: !include ../nrf24_probe/packages/device_base_debug.yaml`
  and a 300s `radio_send_request` probe interval.
- `tests/manual/hm4_readonly/secrets.yaml` — symlink → `../nrf24_probe/secrets.yaml`.
- `tests/manual/hm4_readonly/ahoy-dtu-hm4-readonly.yaml` — model hm_1200 + `time: sntp`.

This session (diagnostic only — see "Current state"): RPD logging, `RX payload ch=`,
`radio_dump` interval, `poll_interval: 1s`.

## Flashing / OTA notes

- Board: ESP32-S2, **native USB** (logger `hardware_uart: USB_CDC`). esptool can't auto-enter
  download mode. **Manual download mode** (only if OTA ever breaks): hold **BOOT** → tap
  **RESET** → release **BOOT** → port `/dev/cu.usbmodem01` appears. App port re-enumerates
  (last seen `/dev/cu.usbmodem207NTUW3X9582`). USB `esphome upload` prints scary errors after a
  *successful* write (chip resets, port vanishes) — look for `Hash of data verified` +
  `Hard resetting`.
- **Normal path is OTA**: `--device hm-link-probe.local`. ~5s upload, then it reboots and
  rejoins WiFi in ~15s.

## When telemetry finally flows (later)

- Cross-check AC/DC vs Ahoy within ±2% (README acceptance).
- Remove diagnostic scaffolding (RPD, `RX payload ch=`, `radio_dump` interval) and the probe
  `interval:`; restore `poll_interval: 15s`.
- Production config is `ahoy-dtu-hm4-readonly.yaml` (WiFi+API+OTA + all sensors + radio
  scripts). It uses `!secret hoymiles_hm4_serial`, which is NOT in the symlinked secrets — add
  that key or hardcode.
- Decide whether to keep the `radio_send_request` / `radio_dump` debug actions in the shipped
  component or gate them behind a flag.
