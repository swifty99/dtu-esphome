# HM4 Read-Only HIL Checklist

Target hardware:

- ESP32-S2 Saola test board
- nRF24 wiring verified by `tests/manual/nrf24_probe`
- First inverter target: HM 4-channel, normally HM-1500 or HM-1200

Before flashing:

- Add `hoymiles_hm4_serial` to the ESPHome secrets file as a quoted
  12-digit decimal serial.
- Confirm wiring: CE GPIO12, CS GPIO9, SCK GPIO5, MOSI GPIO11, MISO GPIO16,
  IRQ GPIO7.

Run:

```sh
esphome config tests/manual/hm4_readonly/ahoy-dtu-hm4-readonly.yaml
esphome run tests/manual/hm4_readonly/ahoy-dtu-hm4-readonly.yaml --device /dev/cu.usbmodem1101
esphome logs tests/manual/hm4_readonly/ahoy-dtu-hm4-readonly.yaml --device ahoy-dtu-hm4-readonly.local
```

Acceptance:

- Boot log reports `nRF24 radio ready`.
- First live values publish within 60 seconds while the inverter is producing.
- AC/DC values match Ahoy reference within +/-2%.
- Removing production makes `status` become `offline` after timeout.
- 100 reboot loop has no SPI deadlock.
- 12-hour soak keeps free heap stable in logs.
