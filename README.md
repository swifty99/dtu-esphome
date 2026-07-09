# ESPHome Hoymiles DTU

External ESPHome component that talks directly to Hoymiles HM-series
microinverters over an nRF24L01+ radio. It reads live AC/DC telemetry and can
send an active-power-limit command.

## Status

Alpha-stage, HM-only. Standalone telemetry (link acquisition, fragment
reassembly, per-model decode) and the active-power-limit command are verified
on hardware against a live HM-1200. On/off, restart, and multi-inverter
scheduling beyond "one at a time" are not implemented yet.

### Supported models

Same HM microinverter families Ahoy handles over nRF24, grouped by DC-input
(channel) count — the realtime payload layout only depends on this count:

| Channels | Models (`model:` value) |
| --- | --- |
| 1 | HM-300 (`hm_300`), HM-350 (`hm_350`), HM-400 (`hm_400`) |
| 2 | HM-600 (`hm_600`), HM-700 (`hm_700`), HM-800 (`hm_800`) |
| 4 | HM-1000 (`hm_1000`), HM-1200 (`hm_1200`), HM-1500 (`hm_1500`) |

HMS/HMT inverters and CMT2300A radios are out of scope for this repo.

## Hardware

Any nRF24L01+ module wired to your ESP's SPI bus, plus two dedicated GPIOs:

- **SCK / MOSI / MISO** — shared SPI bus, declared once under the standard
  ESPHome `spi:` component.
- **CS** — chip-select, declared under `spi_id:`/`cs_pin:` like any other SPI
  device.
- **CE** (`ce_pin:`) — radio chip-enable, required.
- **IRQ** (`irq_pin:`) — optional. If wired to an internal GPIO, RX/TX
  completion is interrupt-driven; otherwise the component falls back to
  high-frequency polling during an exchange. Either way, the ESPHome loop is
  never blocked waiting on the radio.

> [!WARNING]
> **Add a capacitor across the nRF24L01+ module's VCC/GND, right at the
> module's pins.** The cheap breakout boards sold everywhere have little to no
> onboard decoupling, and the radio draws current spikes on TX that a long or
> thin 3.3V supply line can't keep up with. Without it you'll typically still
> see the module respond to SPI register reads/writes, but over-the-air comms
> will be flaky or dead — intermittent ACKs, garbled or missing packets, or no
> link at all. A 10–100 µF capacitor (electrolytic or tantalum work; add a
> 100 nF ceramic in parallel if you have one) directly across VCC/GND at the
> module fixes this in almost all cases. Do not rely on decoupling elsewhere
> on the board — it needs to be at the module itself.

## Installation

```yaml
external_components:
  - source: github://swifty99/dtu-esphome
    components: [hoymiles_dtu]
```

## Minimal configuration

```yaml
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

sensor:
  - platform: hoymiles_dtu
    inverter_id: hm4
    ac_power:
      name: "HM4 AC Power"
```

## Configuration reference

### `hoymiles_dtu:` (hub)

The hub owns the nRF24 radio and polls each configured inverter in turn; it
is a standard ESPHome `PollingComponent`, so it accepts the usual
`update_interval:` alongside its own options.

- **`ce_pin`** (**Required**, pin schema): radio chip-enable GPIO.
- `irq_pin` (*Optional*, pin schema): radio IRQ GPIO. Must be an internal
  GPIO to get interrupt-driven wakeup; any other pin (or omitting it) falls
  back to polling.
- `pa_level` (*Optional*, one of `min` / `low` / `high` / `max`, default
  `low`): nRF24 PA/LNA transmit power. Standalone (no Ahoy) links have been
  validated on hardware at `max` for a marginal link; raise it if telemetry
  polls are failing to acquire.
- `update_interval` (*Optional*, default `15s`): how often each inverter is
  polled for telemetry.
- `scan_detection` (*Optional*, boolean, default `false`): enable passive,
  receive-only detection of nearby scanning/probing of Hoymiles inverters.
  See [Security: detecting scans](#security-detecting-scans).
- **`inverters`** (**Required**, list, at least one): the inverters this hub
  talks to. See below.
- Also accepts the standard SPI device options (`spi_id`, `cs_pin`,
  `data_rate`, `spi_mode`, ...).

### `inverters:` (list, under `hoymiles_dtu:`)

- **`id`** (**Required**, ID): used to attach `sensor:`/`text_sensor:`
  entities to this inverter.
- **`serial`** (**Required**, string): the inverter's printed serial number,
  as **12 decimal digits**. It is interpreted as **packed BCD**, exactly like
  Ahoy — the printed digits map straight onto the radio address bytes. This
  is *not* the same as parsing the serial as a plain decimal number; doing
  that would compute the wrong nRF address and the inverter would never
  respond. Just copy the digits off the inverter's label.
- **`model`** (**Required**, one of the `model:` values in the table above).

### `sensor:` platform

Keyed by `inverter_id`. All keys are optional — only declare the sensors you
want:

```yaml
sensor:
  - platform: hoymiles_dtu
    inverter_id: hm4
    ac_power: {name: "HM4 AC Power"}
    ac_voltage: {name: "HM4 AC Voltage"}
    ac_current: {name: "HM4 AC Current"}
    ac_frequency: {name: "HM4 AC Frequency"}
    reactive_power: {name: "HM4 Reactive Power"}
    power_factor: {name: "HM4 Power Factor"}
    temperature: {name: "HM4 Temperature"}
    yield_today: {name: "HM4 Yield Today"}
    yield_total: {name: "HM4 Yield Total"}
    channels:
      - number: 1
        dc_voltage: {name: "HM4 CH1 DC Voltage"}
        dc_current: {name: "HM4 CH1 DC Current"}
        dc_power: {name: "HM4 CH1 DC Power"}
        yield_today: {name: "HM4 CH1 Yield Today"}
```

`channels` is a list of per-DC-input entries; `number` (1-4, **Required**)
should match the inverter's model channel count (1/2/4) — there is no
separate validation error for a mismatch, but any `number` beyond what the
model actually reports publishes `NaN` instead of a real reading.

### `text_sensor:` platform

Two independent groups, selected by which ID you key the block on:

```yaml
text_sensor:
  - platform: hoymiles_dtu
    inverter_id: hm4
    status: {name: "HM4 Status"}       # "offline" / "online" / "producing"
    last_seen: {name: "HM4 Last Seen"} # seconds since the last good telemetry

  - platform: hoymiles_dtu
    dtu_id: dtu
    last_rx_payload: {name: "DTU Last RX Payload"}   # hex dump, diagnostic
    last_radio_error: {name: "DTU Last Radio Error"} # diagnostic
    scan_detected: {name: "DTU Scan Detected"}       # last detected scan/probe
```

`status`/`last_seen` require `inverter_id`; `last_rx_payload`/
`last_radio_error`/`scan_detected` are hub-level radio diagnostics and require
`dtu_id`. At least one of `inverter_id`/`dtu_id` is required per block.
`scan_detected` only publishes when `scan_detection` is enabled on the hub —
see [Security: detecting scans](#security-detecting-scans).

### Actions

**`hoymiles_dtu.radio_set_power_limit`** — sets the inverter's active-power
limit via the DevControl protocol path. Non-blocking: it queues onto the same
request state machine telemetry polling uses, so calling it never stalls the
ESPHome loop, and it's automatically deferred if a telemetry exchange is
already in flight.

- `id` (**Required**): the `hoymiles_dtu:` hub to target.
- `percent` (*Optional*, 0-100, default `100`, templatable): relative power
  limit.
- `persistent` (*Optional*, boolean, default `true`, templatable): `true`
  survives an inverter restart/grid dropout; `false` reverts on the next
  power cycle.

```yaml
button:
  - platform: template
    name: "Limit to 80%"
    on_press:
      - hoymiles_dtu.radio_set_power_limit:
          id: dtu
          percent: 80
          persistent: false
```

Only the first configured inverter can currently be targeted by this action
(single-inverter limitation, matching how the DevControl exchange is wired
today).

## Security: detecting scans

The Hoymiles DTU protocol has no encryption, integrity, or authentication. As
documented in the CCC report [*"Wireless Interface Vulnerabilities of Hoymiles
Microinverters"*](https://www.ccc.de/system/uploads/382/original/hoymiles_dtu_vuln.pdf)
(plain-language [writeup](https://www.ccc.de/updates/2026/blinkenlights-hoymiles)),
an attacker with a cheap 2.4 GHz
module can broadcast a **Search-ID** request to enumerate the serial numbers of
every inverter in radio range, then send valid commands to any of them. **This
flaw lives in the inverter firmware — no DTU-side software (this component,
OpenDTU, or AhoyDTU) can close it; only a Hoymiles firmware fix can.**

What this component *can* do is **tell you when it is happening near you**.
Setting `scan_detection: true` makes the hub, while it is otherwise idle between
telemetry polls, listen on the HM global Search-ID address and on your
inverter's own address, hopping the HM channels. Anything it overhears that is a
foreign DTU **request** is reported to the `scan_detected` text sensor:

- **`Search-ID scan`** — a broadcast serial-enumeration scan (opcode `0x02`).
  Severity **MEDIUM**.
- **`info probe`** — an RF info request that also leaks the serial (`0x06`).
  Severity **MEDIUM**.
- **`foreign poll`** — a telemetry request aimed at *your* inverter by another
  DTU (`0x15`). Severity **HIGH**.
- **`FOREIGN DevControl`** — a control command (power limit / on-off) aimed at
  *your* inverter by another DTU (`0x51`). Severity **CRITICAL** — this is the
  serious one.

Each report reads like `CRITICAL | FOREIGN DevControl | DTU 0x80187264 | ch 40 | #7`,
where the leading word is the severity (below), the `DTU` value is the attacker's
own DTU serial (embedded in every request), and `#` is a running count.

```yaml
text_sensor:
  - platform: hoymiles_dtu
    dtu_id: dtu
    scan_detected: {name: "DTU Scan Detected", id: dtu_scan}
```

### Severity

Every detection carries a severity, so an automation can react to *how bad* the
traffic is instead of parsing the text. It rises with how far an attacker has
progressed against your specific inverter. The underlying unauthenticated-protocol
flaw these map onto is described in the CCC report [*"Wireless Interface
Vulnerabilities of Hoymiles Microinverters"*](https://www.ccc.de/system/uploads/382/original/hoymiles_dtu_vuln.pdf).

| Severity | Value | Kinds | Meaning |
|----------|-------|-------|---------|
| MEDIUM   | `2`   | `Search-ID scan`, `info probe` | reconnaissance — enumerating or probing serials nearby |
| HIGH     | `3`   | `foreign poll` | a foreign DTU is actively **reading** your inverter |
| CRITICAL | `4`   | `FOREIGN DevControl` | a foreign DTU is **commanding** your inverter (power limit / on-off) |

The severity prefixes the `scan_detected` text, and is also published as a number
by the optional hub-level **`scan_severity`** sensor — the value to threshold on in
automations. It updates on every classified packet (not throttled), so an
escalation is never hidden behind the text-report rate limit, and a `CRITICAL` also
logs at `ERROR`.

```yaml
sensor:
  - platform: hoymiles_dtu
    dtu_id: dtu
    scan_severity: {name: "DTU Scan Severity", id: dtu_scan_severity}

# automation (Home Assistant): alert when severity reaches HIGH (3) or above,
# e.g. trigger on numeric_state above 2 of sensor.dtu_scan_severity
```

This is **detection only — there are no countermeasures.** It never transmits a
request and cannot control the inverter. Honest limitations:

- **Probabilistic.** The monitor and the attacker both hop channels, so it will
  not catch *every* packet — but scanners transmit repeatedly, so a real scanning
  run is very likely to be seen over a few seconds. Absence of a report is not
  proof of absence of scanning.
- **Blind during its own polls.** One radio cannot listen while it is actively
  polling telemetry. A longer `update_interval` leaves more time to monitor.
- **Range-bound.** It only hears transmitters within RF range.
- **Not perfectly stealthy.** HM dynamic-payload reception is coupled to the
  nRF24's hardware auto-ACK, so the monitor does ACK what it overhears. This
  leaks nothing beyond what the (already unauthenticated) protocol exposes, but
  it is not a silent sniffer.
- **Targeted-request detection covers the first configured inverter;**
  Search-ID broadcast detection is inverter-independent.
- **Validated on hardware, but RF-dependent.** Confirmed on-air against a real
  AhoyDTU — both `foreign poll` and `FOREIGN DevControl` were detected. It is
  receive-only and cannot cause harm, but whether it catches a *given* scanner in
  *your* RF environment depends on range and timing, so confirm on your own bench.

## How the HM RF protocol works

This section is for anyone extending the component or debugging a link that
won't acquire. All of it is implemented in `components/hoymiles_dtu/protocol.{h,cpp}`
(pure protocol/codec, unit-tested, no ESPHome or radio-hardware dependency)
and `hoymiles_dtu.cpp` (the radio driver + async state machine).

### The physical link

HM inverters use a Nordic nRF24L01+ compatible radio at 250 kbps, CRC16,
5-byte addresses, and dynamic (auto-sized) payloads — the same config Ahoy
uses. Instead of a single fixed channel, the link hops across five 2.4 GHz
channels — `{3, 23, 40, 61, 75}` — so both sides need to agree on *where* to
be listening at any given moment, not just how to encode a byte.

### Addressing

Both the DTU and every inverter have a 5-byte nRF radio address derived from
a serial number: byte 0 is always `0x01`, and bytes 1-4 are the serial's low
32 bits, byte-reversed (`hm_radio_id_from_serial` in `protocol.cpp`). The
important, easy-to-get-wrong part: the *printed* inverter serial is **packed
BCD**, not a decimal integer — e.g. printed serial `116182806989` produces
radio address `01 82 80 69 89`, but naively parsing it as decimal-116182806989
computes a completely different (wrong) address that the inverter will never
answer to. This bit the project early on; `validate_serial` in `__init__.py`
and `parse_serial_bcd` in `protocol.py` exist specifically to keep it correct.

### The telemetry exchange

1. The DTU picks a channel and transmits a realtime-data request: command
   byte `0x15`, with a request-type byte `0x0B` further into the payload,
   both the inverter's and the DTU's radio-address bytes embedded (so the
   inverter can verify it's being addressed and knows where to reply), a
   timestamp, and a CRC16 + CRC8 trailer (`hm_build_realtime_request`).
2. The inverter's radio auto-ACKs at the hardware level (`TX_DS`) — this
   confirms delivery but not that the inverter accepted the request.
3. The inverter then transmits its reply as a run of fragments, one per
   packet, each stamped `command = 0x95` (`0x15 | 0x80`, "all frames") and a
   1-based fragment ID in the low 7 bits of a header byte; the *last*
   fragment additionally sets that byte's top bit so the DTU knows the
   record is complete without needing a fixed fragment count (`hm_parse_frame`).
   These land on a channel offset **+3** from the request channel, so after
   sending, the DTU jumps there to listen.
4. The DTU reassembles fragments by ID (`hm_assemble_payload`) and, once it
   has every fragment up to the one with the "last" bit set, decodes the
   payload with a table keyed by the inverter's channel-count family
   (1/2/4-channel HM models each have a different byte layout for AC/DC
   fields — see `hm_model_layout` in `protocol.cpp`).

If the inverter never ACKs, the DTU retransmits on the next channel in the
hop sequence rather than waiting out a full poll interval — an acquisition
burst, up to 30 attempts, mirroring Ahoy's own retransmit cadence. If the ACK
lands but fragments are incomplete when the RX window closes, the DTU
re-requests the whole record (up to 4 times) rather than giving up — every
frame it already has is kept, only the missing ones need to reappear.

### Never blocks the ESPHome loop

All of the above — acquisition burst with channel rotation, the RX pendulum/
sweep search for the reply, fragment reassembly, whole-record re-request —
runs as an explicit state machine (`RequestState::{IDLE,WAIT_TX_IRQ,RX_ACTIVE}`)
driven a step at a time from `loop()`, with `update()` (the `PollingComponent`
tick) only responsible for starting the next telemetry exchange when idle and
due. Nothing in the radio path calls `delay()`.

### The power-limit path

`radio_set_power_limit` builds a DevControl request instead (command `0x51`,
a single-frame flag rather than "all frames", the `ActivePowerContr` sub-code,
the requested percentage ×10, and a persistent/non-persistent flag —
`hm_build_power_limit_request`). It reuses the exact same TX burst and
channel-rotation machinery as telemetry (`ExchangeKind::DEV_CONTROL` instead
of `ExchangeKind::TELEMETRY`), but the RX side is deliberately simpler: the
inverter replies with exactly one confirmation frame (`0xD1`), so the DTU
parks on the `+3`-offset channel and waits — no pendulum, no sweep, because
there's only one frame and one chance to be on the right channel. Acceptance
is decided the same way Ahoy's `parseDevCtrl` does (specific bytes in the
confirmation frame both zero).

Because that confirmation is a single unhopped frame, it's structurally less
redundant than the multi-fragment telemetry exchange. In hardware testing,
`TX_DS` (radio-level ACK — the inverter definitely received the command) has
been 100% reliable, while the RX confirmation occasionally doesn't arrive
within the listen window even though the command almost certainly landed;
you may see a log line like `... -> no confirmation frame` on an otherwise
healthy link. That's read as an inherent limitation of a single unhopped
confirmation frame, not a bug — nothing about the change itself is rolled
back by a missed confirmation.

## Development

Run local checks with:

```sh
pre-commit run --all-files
```

Run the unit tests (native C++ decoder tests, protocol tests, and — with
`esphome` installed — config validation) with:

```sh
pytest tests/unit
```

Compile the minimal fixture with:

```sh
esphome compile tests/components/hoymiles_dtu/test.minimal.yaml
```

## License

GPL-3.0-only. See [LICENSE](LICENSE).
