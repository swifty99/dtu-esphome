# HM/nRF24 Reference Notes

Last checked: 2026-06-28.

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

## ESPHome action API check, 2026-06-28

Verified against local ESPHome `2026.6.2` and upstream ESPHome source:

- `automation.register_action(...)` requires an explicit `synchronous=` keyword.
- Action classes that complete all work inside `play()` should register with
  `synchronous=True`.
- Trivially-copyable `TEMPLATABLE_VALUE(...)` fields need Python codegen to pass
  values through `cg.templatable(value, args, type)` before calling setters.

## ESPHome radio-loop API check, 2026-06-28

Verified against local generated ESPHome `2026.6.2` headers:

- `Component::enable_loop_soon_any_context()` is declared in
  `esphome/core/component.h`, defined with `IRAM_ATTR`, sets the component's
  pending loop-enable flag, and calls `wake_loop_any_context()`.
- `HighFrequencyLoopRequester` is declared in `esphome/core/helpers.h`; use
  `start()`/`stop()` from normal loop context only while a radio exchange is
  active.
- `GPIOPin` has no interrupt API. Interrupt attachment requires
  `InternalGPIOPin::attach_interrupt(func, arg, gpio::INTERRUPT_FALLING_EDGE)`.
- ISR handlers must not perform SPI, logging, parsing, publishing, or
  `HighFrequencyLoopRequester` state changes. The nRF IRQ handler should only
  set an ISR-safe flag and call `enable_loop_soon_any_context()`.

## Ahoy nRF24 RX behavior check, 2026-06-28

Verified against upstream Ahoy `main`:

- `src/hm/NrfRadio.h::setup()` calls `openReadingPipe(1, mDtuRadioId)`.
- `src/hm/NrfRadio.h::sendPacket()` calls `openWritingPipe(iv->radioId)`;
  RF24 uses pipe 0 for TX ACK handling.
- On TX IRQ, Ahoy sets HM `rxOffset = 3`, starts RX on `(txCh+3) % 5`, and
  calls `startListening()`.
- While waiting, Ahoy uses `DURATION_LISTEN_MIN = 5ms`. For the first
  `DURATION_TXFRAME = 85ms`, HM RX pendulates between `txCh+3` and `txCh+2`;
  after that it falls back to a reverse sweep through all five channels.
- Ahoy setup uses 250 kbps RF, CRC16, 5-byte addresses, dynamic payloads, and
  default auto-ack enabled. Retries are `setRetries(3, 15)`.
