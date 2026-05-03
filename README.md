# ESPHome Hoymiles DTU

External ESPHome component for Hoymiles HMS/HMT inverters using the CMT2300A
radio.

## Status

This project is alpha-stage scaffolding. Phase 0 has no working radio,
Hoymiles protocol implementation, inverter polling, sensors, limit control, or
hardware support. The current component only compiles and logs:

```text
Hello from Hoymiles DTU
```

## Minimal Configuration

```yaml
external_components:
  - source: github://swifty99/dtu-esphome
    components: [hoymiles_dtu]

hoymiles_dtu:
  id: dtu
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
