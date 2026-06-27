# HM/nRF24 Reference Notes

Last checked: 2026-06-27.

Project scope:

- HM-series inverters only.
- nRF24L01+ radio only.
- HMS/HMT and CMT2300A are intentionally out of scope.

Sources consulted:

- Ahoy `src/hm/NrfRadio.h`, `Radio.h`, `Communication.h`, `hmDefines.h`,
  `hmInverter.h`, and `utils/crc.*` from `https://github.com/lumapu/ahoy`.
- ESPHome 2026.6.2 local package for SPI device, sensor, and text sensor
  codegen patterns.

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
