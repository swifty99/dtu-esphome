# ESPHome Hoymiles DTU - HM-Only Implementation Plan

This project targets Hoymiles **HM-series microinverters only** using an
nRF24L01+ radio. HMS/HMT and CMT2300A support are intentionally out of scope.

The communication reference is Ahoy's HM/nRF24 implementation. Keep this repo
ESPHome-native: use ESPHome SPI, component lifecycle, logging, sensors, and
config schemas rather than porting upstream application structure.

## Current State

Implemented as of 2026-06-27:

- `hoymiles_dtu` hub with ESPHome SPI, `cs_pin`, `ce_pin`, optional `irq_pin`,
  `poll_interval`, `pa_level`, and one or more inverter definitions.
- nRF24 probe/init using 1 MHz SPI mode 0, 250 kbps RF, CRC16, dynamic payloads,
  5-byte addresses, and the HM channel set.
- HM-1200/HM-1500 4-channel read-only realtime telemetry request and parser.
- `sensor` platform for AC values, DC channel values, and yields.
- `text_sensor` platform for status and last seen.
- Python protocol helpers and unit/config tests.
- Compile fixtures for ESP32 and ESP32-S3 on Arduino and ESP-IDF.
- Manual HIL config/checklist in `tests/manual/hm4_readonly/`.

## Scope

In scope:

- HM-300, HM-350, HM-400, HM-600, HM-700, HM-800, HM-1000, HM-1200, HM-1500.
- nRF24L01+ radio only.
- Read-only telemetry first.
- Later: power limit, inverter enable/disable, restart, and multi-inverter
  scheduling, if HM protocol testing confirms reliable command handling.

Out of scope:

- HMS/HMT inverters.
- CMT2300A radios.
- Zero-export control loops beyond exposing safe ESPHome entities/actions.
- Vendoring incompatible upstream source into this GPL-only repo without a
  separate licensing decision.

## Phase 1 - HM4 Read-Only HIL

Goal: prove the current HM-1200/HM-1500 4-channel implementation against the
ready ESP32-S2+nRF24 rig and a live inverter.

Tasks:

1. Flash `tests/manual/hm4_readonly/ahoy-dtu-hm4-readonly.yaml`.
2. Confirm boot logs report `nRF24 radio ready`.
3. Confirm all configured sensors publish within 60 seconds while producing.
4. Compare AC/DC values against Ahoy within +/-2%.
5. Force no production and confirm `status` becomes `offline` after timeout.
6. Run 100 reboot cycles without SPI deadlock.
7. Run a 12-hour soak and check heap/log stability.

Acceptance:

- HM4 live telemetry works on real hardware.
- Values are close enough to Ahoy for the same inverter/radio conditions.
- Failure modes are visible in logs and do not wedge the ESP.

## Phase 2 - Parser Coverage For HM Models

Goal: support the common HM 1-, 2-, and 4-channel model families with shared
protocol code and model-specific payload layouts.

Tasks:

1. Add model metadata for channel count and payload offsets.
2. Add parsers for 1-channel, 2-channel, and 4-channel HM realtime payloads.
3. Add captured-packet fixtures under `tests/fixtures/packets/`.
4. Add unit tests for every supported model layout.
5. Update README with the supported model list and per-model caveats.

Acceptance:

- Adding a model is mostly metadata and tests.
- Unit tests cover real captured payloads for each supported layout.

## Phase 3 - Robust Radio Scheduling

Goal: make polling reliable enough for daily use.

Tasks:

1. Tune channel hop timing and response timeout from HIL logs.
2. Add retry behavior for incomplete frame sets.
3. Add round-robin multi-inverter scheduling.
4. Improve `dump_config()` diagnostics for radio, inverter, and last error.
5. Add manual HIL cases for two inverters when hardware is available.

Acceptance:

- Single inverter polling is stable over 24 hours.
- Multiple inverters do not starve each other.
- Incomplete responses recover without rebooting the ESP.

## Phase 4 - HM Command Entities

Goal: expose commands only after read-only telemetry is stable.

Tasks:

1. Implement active power limit as a `number` entity.
2. Implement enable/disable as a `switch`.
3. Implement restart as a `button`.
4. Add ESPHome action support for automations.
5. Require ack/verification before publishing command state.
6. Document command risks and non-optimistic behavior clearly.

Acceptance:

- Command state in Home Assistant reflects actual inverter acknowledgements.
- Failed commands time out, retry, and log a useful warning.

## Phase 5 - Release Hardening

Goal: make the HM-only component easy to install, test, and maintain.

Tasks:

1. Add complete README examples for basic HM setups.
2. Add troubleshooting docs for no radio, no response, incomplete response,
   wrong serial, and unstable values.
3. Keep CI compiling every public config shape.
4. Add a release checklist that includes HIL date, ESPHome version, board,
   framework, inverter model, and radio module used.
5. Retest against current ESPHome monthly or before any release.

Acceptance:

- New users can wire an HM+nRF24 setup and get telemetry from the docs.
- Release notes say exactly which HM models and ESPHome versions were tested.

## Testing Rules

- Unit tests cover serial validation, radio ID generation, CRCs, request
  framing, frame assembly, and payload decoding.
- ESPHome config tests cover schema and public YAML examples.
- ESPHome compile tests cover ESP32/ESP32-S3 and Arduino/ESP-IDF.
- HIL tests are required before tagging releases because radio timing cannot
  be proven by compile tests.

## Reference Policy
t
- Ahoy is the communication reference for HM/nRF24 behavior.
- Store findings in `components/hoymiles_dtu/REFERENCE_NOTES.md`.
- Do not copy Ahoy source into this repo unless licensing is revisited.
- When ESPHome APIs are uncertain, verify against current ESPHome docs/source
  before changing code.
