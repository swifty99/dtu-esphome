# HM/nRF24 Reference Notes

Project scope:

- HM-series inverters only.
- nRF24L01+ radio only.
- HMS/HMT and CMT2300A are intentionally out of scope.

Sources consulted:

- Ahoy `src/hm/NrfRadio.h`, `Radio.h`, `Communication.h`, `hmDefines.h`,
  `hmInverter.h`, and `utils/crc.*` from `https://github.com/lumapu/ahoy`.
- ESPHome 2026.6.2 local package and generated build headers for SPI device,
  GPIO interrupt, loop wake, sensor, and text sensor codegen patterns.

Implementation policy:

- Ahoy code is used as a communication reference only.
- This component keeps a clean ESPHome-native implementation in this repo.
- Do not copy Ahoy source into this component unless licensing is revisited.

Protocol facts used by the first HM 4-channel milestone:

- nRF24: 1 MHz SPI mode 0, 250 kbps RF rate, CRC16, dynamic payloads,
  5-byte addresses, channel list `3, 23, 40, 61, 75`.
- Inverter nRF address is four low serial bytes reversed into bytes 4..1,
  with byte 0 set to `0x01`.
- Read-only polling uses request `0x15` / payload command `0x0B`.
- HM 4-channel realtime payload is expected in seven fragments.
- Frame CRC is CRC8 polynomial `0x01`, init `0x00`.
- Payload control CRC is CRC16/Modbus polynomial `0xA001`, init `0xFFFF`.
- RX pipe/address setup: Ahoy `NrfRadio.h::setup()` opens the reading pipe on
  the DTU's own radio id; `sendPacket()` opens the writing pipe on the target
  inverter's radio id (RF24 pipe 0 handles the TX ACK).
- On TX ACK, Ahoy sets HM `rxOffset = 3`, starts RX on `(txCh+3) % 5`. For the
  first `DURATION_TXFRAME = 85ms` it pendulates RX between `txCh+3` and
  `txCh+2` (channel dwell `DURATION_LISTEN_MIN = 5ms`), then falls back to a
  reverse sweep through all five channels. These map to this component's
  `RX_INITIAL_PENDULUM_MS` / `RX_CHANNEL_DWELL_MS` constants.
- Ahoy's retransmit setup is `setRetries(3, 15)`.

## ESPHome API notes

- `automation.register_action(...)` requires an explicit `synchronous=`
  keyword; action classes that complete all work inside `play()` (including
  ones that just queue state for a state machine to pick up later) should
  register with `synchronous=True`.
- Trivially-copyable `TEMPLATABLE_VALUE(...)` fields need Python codegen to
  pass values through `cg.templatable(value, args, type)` before calling
  setters.
- `Component::enable_loop_soon_any_context()` (declared in
  `esphome/core/component.h`) is `IRAM_ATTR`-safe; it sets the component's
  pending loop-enable flag and calls `wake_loop_any_context()`.
- `HighFrequencyLoopRequester` (declared in `esphome/core/helpers.h`) —
  `start()`/`stop()` from normal loop context only while a radio exchange is
  active.
- `GPIOPin` has no interrupt API; interrupt attachment requires
  `InternalGPIOPin::attach_interrupt(func, arg, gpio::INTERRUPT_FALLING_EDGE)`.
- ISR handlers must not perform SPI, logging, parsing, publishing, or
  `HighFrequencyLoopRequester` state changes. The nRF IRQ handler should only
  set an ISR-safe flag and call `enable_loop_soon_any_context()`.
- `cv.polling_component_schema(default)` extends `COMPONENT_SCHEMA` and adds
  `update_interval:`; `cg.register_component()` calls `set_update_interval()`
  automatically when `CONF_UPDATE_INTERVAL` is present in config, so
  `PollingComponent` subclasses don't need to wire it by hand.

## HM model families and realtime payload layout, 2026-07-01

Verified against Ahoy `src/hm/hmDefines.h` (`hm1chAssignment`, `hm2chAssignment`,
`hm4chAssignment`). Ahoy groups HM inverters by DC-input count; the realtime
record layout depends only on that count, so this component keys the parser on it:

- 1 channel: HM-300 / HM-350 / HM-400 (`HM1CH_PAYLOAD_LEN` 30).
- 2 channels: HM-600 / HM-700 / HM-800 (`HM2CH_PAYLOAD_LEN` 42).
- 4 channels: HM-1000 / HM-1200 / HM-1500 (`HM4CH_PAYLOAD_LEN` 62).

Field byte offsets in the assembled record (`u16` big-endian unless noted; the
common divisors are UDC/10, IDC/100, PDC/10, YD/1, YT(`u32`)/1000, UAC/10,
IAC/100, PAC/10, Q/10, F/100, PF/1000, T(`int16`)/10, EVT/1):

| Field | 1ch | 2ch | 4ch |
| --- | --- | --- | --- |
| CH1 UDC/IDC/PDC | 2 / 4 / 6 | 2 / 4 / 6 | 2 / 4 / 8 |
| CH1 YD / YT | 12 / 8 | 22 / 14 | 20 / 12 |
| CH2 UDC/IDC/PDC | - | 8 / 10 / 12 | =CH1 / 6 / 10 |
| CH2 YD / YT | - | 24 / 18 | 22 / 16 |
| CH3 UDC/IDC/PDC | - | - | 24 / 26 / 30 |
| CH3 YD / YT | - | - | 42 / 34 |
| CH4 UDC/IDC/PDC | - | - | =CH3 / 28 / 32 |
| CH4 YD / YT | - | - | 44 / 38 |
| UAC / F / PAC | 14 / 16 / 18 | 26 / 28 / 30 | 46 / 48 / 50 |
| Q / IAC / PF | 20 / 22 / 24 | 32 / 34 / 36 | 52 / 54 / 56 |
| T / EVT | 26 / 28 | 38 / 40 | 58 / 60 |

`=CHn` means the input shares the paired channel's voltage (Ahoy `CALC_UDC_CH`).
These offsets live in `hm_model_layout()` in `protocol.cpp` and are unit-tested in
`tests/native/test_protocol.cpp`.
