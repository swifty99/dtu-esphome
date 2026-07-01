# ESPHome Hoymiles DTU

External ESPHome component for Hoymiles HM-series inverters using an
nRF24L01+ radio.

## Status

This project is alpha-stage and HM-only. It reads live telemetry over nRF24 and
supports an active-power-limit command. On/off, restart, and hardened
multi-inverter scheduling are planned later.

### Supported models

Same HM microinverter families Ahoy handles over nRF24, grouped by DC-input
(channel) count:

| Channels | Models (`model:` value) |
| --- | --- |
| 1 | HM-300 (`hm_300`), HM-350 (`hm_350`), HM-400 (`hm_400`) |
| 2 | HM-600 (`hm_600`), HM-700 (`hm_700`), HM-800 (`hm_800`) |
| 4 | HM-1000 (`hm_1000`), HM-1200 (`hm_1200`), HM-1500 (`hm_1500`) |

Standalone telemetry is verified on hardware against an HM-1200. HMS/HMT
inverters and CMT2300A radios are out of scope for this repo.

## Minimal Configuration

```yaml
external_components:
  - source: github://swifty99/dtu-esphome
    components: [hoymiles_dtu]

spi:
  id: dtu_spi
  clk_pin: GPIO5
  mosi_pin: GPIO11
  miso_pin: GPIO16

hoymiles_dtu:
  id: dtu
  spi_id: dtu_spi
  cs_pin: GPIO9
  ce_pin: GPIO12
  irq_pin: GPIO7
  inverters:
    - id: hm4
      serial: "114180000000"
      model: hm_1500
```

## Development

Run local checks with:

```sh
pre-commit run --all-files
```

Compile the minimal fixture with:

```sh
esphome compile tests/components/hoymiles_dtu/test.minimal.yaml
```

## License

GPL-3.0-only. See [LICENSE](LICENSE).
