# nRF24 Manual Probe Notes

Last tested on ESP32-S2 `/dev/cu.usbmodem1101` with ESPHome 2026.6.2.

Working pinout:

- CE: GPIO12
- CSN: GPIO9
- SCK: GPIO5
- MOSI: GPIO11
- MISO: GPIO16
- IRQ: GPIO7

Probe result: `PASS`.

Observed register values:

- `STATUS=0x0E`
- `CONFIG=0x08`
- `EN_AA=0x3F`
- `RF_CH=0x02`
- `RF_SETUP=0x0F`
- `SETUP_AW` write/read test passed with `0x03`

Diagnostics that passed:

- ESP-IDF SPI, 1 MHz, mode 0, hardware CS
- ESP-IDF SPI, 250 kHz, mode 0, hardware CS
- GPIO bit-banged SPI mode 0

Earlier failure with MISO on GPIO17 produced all-zero SPI reads. The working
MISO pin for this wiring is GPIO16.

## WiFi Debug Profile

`ahoy-dtu-controller.yaml` is the Zoo-style WiFi/API/OTA debug profile for
moving the probe onto the Ahoy DTU controller.

It follows the recent ESPHomeZoo pattern:

- `substitutions` for `plug_name` and `device_name`
- `packages/device_base_debug.yaml` for WiFi, API, OTA, DHCP, and fallback AP
- `packages/default_diag.yaml` for uptime, WiFi RSSI, WiFi info, and restart button
- `logger.level: DEBUG` for nRF24 bring-up logs over WiFi
- `captive_portal:` for fallback AP recovery
- DHCP addressing, with mDNS hostname `ahoy-dtu-nrf24-probe.local`

Make sure the ESPHome config directory has these secrets available:

- `wifi_pw_home`
- `wifi_pw_4thing`
- `web_server_password`

For local validation from this repository, an ignored `secrets.yaml` symlink can
point at `/Users/jan/dev/repos/EsphomeZoo/secrets.yaml`.

Useful commands:

```sh
esphome config tests/manual/nrf24_probe/ahoy-dtu-controller.yaml
esphome compile tests/manual/nrf24_probe/ahoy-dtu-controller.yaml
esphome run tests/manual/nrf24_probe/ahoy-dtu-controller.yaml --device /dev/cu.usbmodem1101
esphome logs tests/manual/nrf24_probe/ahoy-dtu-controller.yaml --device ahoy-dtu-nrf24-probe.local
esphome logs tests/manual/nrf24_probe/ahoy-dtu-controller.yaml --device 192.168.178.74
```

Use USB for the first flash, then WiFi logs/API/OTA after the device is on the
network.

Validation status:

- `esphome config tests/manual/nrf24_probe/ahoy-dtu-controller.yaml` passed.
- `esphome compile tests/manual/nrf24_probe/ahoy-dtu-controller.yaml` passed
  with ESPHome 2026.6.2.
- USB flash to ESP32-S2 `/dev/cu.usbmodem1101` passed.
- DHCP assigned `192.168.178.74` on SSID `Bit_Pumpe`.
- mDNS resolved `ahoy-dtu-nrf24-probe.local` to `192.168.178.74`.
- `ping -c 3 192.168.178.74` and `ping -c 2 ahoy-dtu-nrf24-probe.local` passed.
- `esphome logs ... --device 192.168.178.74` connected over API and streamed
  nRF24 `PASS` logs.

Current WiFi/API details from the verified run:

- IP address: `192.168.178.74`
- Hostname: `ahoy-dtu-nrf24-probe`
- API: `ahoy-dtu-nrf24-probe.local:6053`
- OTA: `ahoy-dtu-nrf24-probe.local:3232`
- SSID: `Bit_Pumpe`
- BSSID: `9C:05:D6:13:02:E9`
- RSSI: about `-49 dB`
- MAC: `84:F7:03:F1:BA:60`

Verified WiFi log command:

```sh
esphome logs tests/manual/nrf24_probe/ahoy-dtu-controller.yaml --device 192.168.178.74
```
