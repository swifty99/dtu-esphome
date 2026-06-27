# ESPHome Hoymiles DTU

External ESPHome component for Hoymiles HM-series inverters using an
nRF24L01+ radio.

## Status

This project is alpha-stage and HM-only. The current implementation targets
read-only HM-1200/HM-1500 telemetry over nRF24. Power limit, on/off, restart,
and robust multi-inverter scheduling are planned later.

HMS/HMT inverters and CMT2300A radios are out of scope for this repo.

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
