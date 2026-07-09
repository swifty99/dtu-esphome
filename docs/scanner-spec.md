# Spec — active inverter scanner (`inverter_scan`)

Status: **protocol layer + unit tests implemented; radio driver, config surface,
and sensors deferred to a hardware-in-the-loop (HIL) session.** Written 2026-07-09.

This is the transmit-side counterpart to the passive `scan_detection` feature
(README → *Security: detecting scans*). Where `scan_detection` listens and
reports foreign discovery traffic it overhears, the scanner *transmits* the
discovery requests itself and collects the serials that answer — the same thing
AhoyDTU, OpenDTU, and the CCC report's proof-of-concept scanner do. The
mechanism is documented in the CCC report *"Wireless Interface Vulnerabilities
of Hoymiles Microinverters"* (`docs/hoymiles_dtu_vuln.pdf`), section 4.

## Why this exists

Two legitimate uses, both on hardware you own or are authorised to assess:

1. **Onboarding.** Discover the serial and model of your own inverter over the
   air instead of reading the packed-BCD label by eye. Getting that serial wrong
   is this project's original bug (see `hm_radio_id_from_serial` / `parse_serial_bcd`
   and the README addressing note); a scan reports it directly.
2. **Exposure auditing.** Enumerate the Hoymiles inverters within RF range of
   your own site to understand what an attacker with a €5 module would see. This
   is the active complement to `scan_detection`.

Honest framing, stated plainly and up front in the README when this ships: the
scanner transmits the exact requests the CCC report describes as the attack, and
exploits the same unauthenticated-protocol flaw. It is therefore **disabled by
default, only ever runs on an explicit user-initiated action, never on a timer,
and is bounded in duration.** It only *reads* serials — it sends no power-limit,
on/off, or firmware command, so it cannot change an inverter's operating state.
Run it only against inverters you own or are authorised to assess.

## Protocol

Two discovery commands, both broadcast to the HM global Search-ID address
`05 64 64 64 64` (`HM_SEARCH_ID_ADDRESS`). Replies are sent to the DTU address
embedded in the request and are received on the DTU's own listen pipe. Both
requests carry only an app-layer CRC8 trailer — no CRC16, unlike the realtime
poll and DevControl requests.

All four wire formats below are reproduced byte-for-byte from the report's
captures and are covered by golden-vector unit tests (native C++ in
`tests/native/test_protocol.cpp`, Python in `tests/unit/test_protocol.py`). The
CRC8 on every one of them validates against the repo's `hm_crc8`.

### Search-ID (Gongfa), opcode `0x02` — `hm_build_search_id_request`

```
TX:  02 00 00 00 00  <dtu_serial BE>  00  CRC8            (11 bytes)
     └┬ └───┬─────┘  └──────┬──────┘  └┬  └┬
      │     │               │          │   └ app-layer CRC8 over [0..9]
      │     │               │          └ padding
      │     │               └ our DTU address (reply is sent here), big-endian
      │     └ target inverter id = 0 (broadcast; "does not matter" per the report)
      └ opcode
```

An in-range inverter that is **not currently bound to a DTU** replies. The
report notes an HM inverter stops answering Search-ID roughly 15 minutes after a
DTU last polled it; that is why the `0x06` fallback below exists.

Reply — `hm_parse_search_id_response` (report fig. 3, HMS-600):

```
RX:  82 <serial4 BCD> <dtu4> <pid2> <serial4 BCD> CRC8    (16 bytes)
     │  └─────┬─────┘ └─┬──┘ └─┬─┘
     │        │         │      └ model / product ID (PID)
     │        │         └ echoed DTU address (our scan sender id)
     │        └ last 8 printed serial digits, packed BCD
     └ 0x82 = 0x02 | all-frames (response bit set)
```

### Collect RF info, opcode `0x06` — `hm_build_collect_info_request`

The fallback that leaks the serial **even while the inverter is bound to a DTU**.
One wildcard byte longer than Search-ID, so the DTU address sits at `[6..9]`.

```
TX:  06 00 00 00 00 00  <dtu_serial BE>  00  CRC8          (12 bytes)
```

Reply — `hm_parse_collect_info_response` (report section 4.3):

```
RX:  06 00 00 00 00  <serial4 BCD>  00  CRC8               (11 bytes)
     └ observed opcode 0x06 (a firmware quirk; a spec-correct reply would set the
       response bit, 0x86 — the parser accepts both). No PID/payload.
```

### What a scan learns, and what it does not

A reply carries only the **last 8 printed serial digits** (packed BCD). That is
`serial & 0xFFFFFFFF` and equals the low 4 bytes of the radio address, which is
all the radio addressing actually uses (`hm_radio_id_from_serial` ignores the
high digits). The printed **4-digit model prefix is not transmitted.** So:

- To *talk to* a discovered inverter, the 8-digit suffix is sufficient (the
  radio address is fully determined by it).
- To fill in a 12-digit `serial:` for the config, prepend the model prefix from
  the label. The Search-ID PID identifies the model family and narrows the
  prefix; the `0x06` path returns no PID, so it identifies presence and serial
  only.

PIDs for which the report received responses: `1021, 1121, 1141, 1144, 1161,
1164, 2801, 2821`. This component keys inverter behaviour on channel-count
family (1/2/4), not on PID, so the scanner should report the raw PID and leave
model naming to the operator rather than baking in a partial PID→model table.

## Radio behaviour (driver, deferred to HIL)

Reuses the existing async, non-blocking TX/RX state machine (`RequestState`,
`loop()`-driven — nothing calls `delay()`), adding an `ExchangeKind::SCAN`
alongside `TELEMETRY` and `DEV_CONTROL`.

- **Addresses.** TX address and pipe-0 RX (auto-ACK) = `05 64 64 64 64`; pipe-1
  RX = `0x01` followed by the DTU's own 4 address bytes (replies land here), per
  report section 4.3.
- **Channel sweep.** Round-robin the five HM channels `{3, 23, 40, 61, 75}`. On
  each channel transmit the enabled command(s) — `0x02`, then `0x06` — and dwell
  for replies before hopping. Repeat sweeps until the scan duration elapses.
  Channel dwell is a tunable at the top of `hoymiles_dtu.cpp`, like
  `MONITOR_CHANNEL_DWELL_MS`.
- **Dedup + collect.** Key discovered inverters by `serial_suffix`; keep the
  first PID and the command/channel that found each. Publish new discoveries as
  they arrive.
- **Mutual exclusion.** One radio: a scan runs only from `IDLE`, suspends passive
  `scan_detection` monitoring while active, and defers/blocks telemetry polls for
  its (bounded) duration. A scan will trip other detectors in range — including
  another node's `scan_detection` — by design.

## Config surface (deferred to HIL)

Gated behind an **action**, not a boolean option, because it transmits and must
be user-initiated. Mirrors the existing `radio_set_power_limit` action wiring
(`Action` + `Parented`, `RadioSetPowerLimitAction`).

```yaml
# Action — the only way to start a scan. Never runs on a timer.
button:
  - platform: template
    name: "Scan for inverters"
    on_press:
      - hoymiles_dtu.scan_inverters:
          id: dtu
          duration: 20s          # optional, bounded; default 20s
          commands: [search_id, collect_info]   # optional; default both

# Reporting — hub-level sensors, keyed by dtu_id like the other radio diagnostics.
text_sensor:
  - platform: hoymiles_dtu
    dtu_id: dtu
    scan_result: {name: "DTU Scan Result"}     # each newly discovered inverter, one line

sensor:
  - platform: hoymiles_dtu
    dtu_id: dtu
    scan_found_count: {name: "DTU Scan Found"}  # unique inverters found this scan
```

New symbols to add: `CONF_SCAN_RESULT`, `CONF_SCAN_FOUND_COUNT`, an
`hoymiles_dtu.scan_inverters` action (with `CONF_DURATION`, and a `commands`
enum list), and the matching setters/sensors on `HoymilesDtuComponent`.

### Output format

One line per newly discovered inverter, reading like the `scan_detected`
reports:

```
serial ...82806989 | PID 0x1144 | via search-id | ch 40 | #1
serial ...85034022 | PID -      | via collect-info | ch 23 | #2
```

`serial` is the discovered 8-digit suffix; `PID` is `-` when the `0x06` path
answered (no PID); `#` is a running count within the scan.

## Implementation status

**Done in this change (no hardware needed):**

- `hm_build_search_id_request`, `hm_build_collect_info_request`,
  `hm_parse_search_id_response`, `hm_parse_collect_info_response` in
  `protocol.{h,cpp}` — pure, in the repo's unit-tested protocol layer.
- Python mirrors of the two request builders in `protocol.py`.
- Golden-vector tests against the report's real captures, plus a round-trip test
  that a request the scanner builds is exactly what `hm_classify_sniffed_packet`
  recognises (transmit and receive sides agree on the wire format).

**Implemented (2026-07-09):** `ExchangeKind::SCAN` radio path, `scan_inverters`
action, `scan_result` / `scan_found_count` sensors, config schema, README
section. Compiles for ESP32 (CI fixture `test.scan.yaml`) and the S2 rig.

**HIL — transmit path validated on-air, reply capture pending inverter
conditions.** Flashed via OTA to the S2 rig (`hm4_readonly`, DTU serial
`0x83915460`) and run against the live HM-1200 (`116182806989`), 2026-07-09
~21:30 (after dark):

- The scan runs end-to-end and non-blocking: `Scan started` → ~128 probes across
  all five channels alternating `0x02`/`0x06` → `Scan complete`, with normal
  telemetry polling resuming afterward. The on-air bytes are exact
  (`02 00 00 00 00 83 91 54 60 00 24`, `06 00 00 00 00 00 83 91 54 60 00 20`).
- **No discovery reply was captured.** Every probe returned `MAX_RT` (no ACK). In
  the first run the inverter was online and answering telemetry (4 good reads)
  yet did not answer the broadcast while bound to our 15 s polling — consistent
  with the report: Search-ID goes silent once a DTU polls the inverter (and
  plausibly the firmware already disables its global RX address while bound,
  which is the report's own recommended fix). In a later run the inverter had
  powered down for the night (telemetry offline too), so no reply is possible.

**Still to validate (needs a powered, unbound inverter — i.e. daytime, with our
polling paused ~15 min so the inverter answers Search-ID again):** capture a real
`0x82`/`0x06` reply and confirm the scan reports suffix `82806989` and a
4-channel-family PID. The `+3` RX offset and per-probe TX-result logging added
during this session are in place to catch it. Cross-check against a second known
inverter if available.

## Limitations (expected, will go in the README)

- **Only the low 8 serial digits** are returned; complete the printed prefix
  from the label (§ *What a scan learns*).
- **Search-ID goes silent** on an inverter a DTU is actively polling; `0x06` is
  the fallback and is the more reliable path in a live install.
- **Probabilistic and range-bound**, like detection — both sides hop channels;
  absence of a result is not proof of absence.
- **HM (2.4 GHz) only.** HMS/HMT are 868 MHz and out of scope for this nRF24
  component, though the report shows the same commands work there.
- **Not stealthy.** It transmits, and dynamic-payload RX is coupled to the
  nRF24 auto-ACK, so it both broadcasts discovery requests and ACKs replies.
