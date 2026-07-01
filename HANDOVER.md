# Handover — ESPHome Hoymiles DTU (HM / nRF24)

_Last updated: 2026-07-01. Native ESPHome component in `components/hoymiles_dtu`._

## Status: working

Standalone HM telemetry works end-to-end and is verified on a live **HM-1200**
(serial 116182806989) — link acquisition → data → assemble → decode → publish,
with **no Ahoy running**. Last on-air check (2026-07-01): 100% `TX_DS`, 4/4
fragments, `AC 236.8V 49.99Hz`, `27.1°C`, `YieldTotal 888.664 kWh`.

What is implemented:

- Link acquisition: Ahoy-style burst retransmit (`poll_tx_` → `transmit_request_`,
  `MAX_TX_ATTEMPTS`), whole-record re-request on missing fragments.
- Realtime decode for **all HM channel families like Ahoy** — 1ch HM-300/350/400,
  2ch HM-600/700/800, 4ch HM-1000/1200/1500 — via the table-driven
  `hm_model_layout()` in `protocol.cpp` (offsets from Ahoy `hm{1,2,4}chAssignment`).
- Active-power-limit DevControl command with ack verification.
- `sensor` / `text_sensor` platforms; optional diagnostic radio actions.

## The two root causes (both fixed, for history)

1. **Serial is packed BCD**, not decimal — see memory `hoymiles-serial-bcd`.
2. **Request byte order** had to match Ahoy's wire format exactly (the reply is
   routed by `[5..8]`) — see memory `hm-request-timestamp-big-endian`. Both are
   pinned by unit tests.

## Testing

```sh
.venv/bin/pytest tests/unit -q          # Python + native C++ protocol tests + config validation
esphome compile tests/components/hoymiles_dtu/test.multi-model.yaml
```

- Native decoder tests: `tests/native/test_protocol.cpp` (compiled+run by
  `tests/unit/test_native_protocol.py`; skips if no C++ compiler). Includes a real
  captured HM-1200 record as a golden fixture.
- `esphome` is system-wide (`/opt/homebrew/bin`, 2026.6.x). macOS has no
  `timeout`; capture logs with a backgrounded `esphome logs …`.

## Live hardware

- Board: ESP32-S2 `hm-link-probe`, OTA at `hm-link-probe.local`
  (`192.168.178.74`). Flash/log with
  `esphome {compile,upload,logs} tests/manual/hm4_readonly/hm-link-probe.yaml --device hm-link-probe.local`.
- Native-USB flashing details (only if OTA breaks): memory
  `esp32s2-native-usb-flashing`.

## Next / not done

- Live HIL for a real 1-channel and 2-channel inverter (decode is unit-tested; no
  such hardware on hand).
- Optional: verify the record's trailing CRC16 (per-fragment CRC8 is already
  checked); per-fragment (not whole-record) retransmit like Ahoy.
- Command entities beyond power limit (enable/disable, restart).
