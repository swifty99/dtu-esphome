# Handover — scan detection, next testing session

Written 2026-07-08. Context for validating the passive `scan_detection` feature
on hardware, using a real AhoyDTU as the intruder.

## What was built (and merged in this branch)

A **receive-only** scan/intrusion detector. `scan_detection: true` on the
`hoymiles_dtu:` hub makes the radio, while idle between telemetry polls, listen
on the HM global Search-ID address (`05 64 64 64 64`) and on the inverter's own
address, hopping the HM channels. Overheard foreign DTU **requests** are
classified and published to a hub-level `scan_detected` text sensor.

There are **no countermeasures** — it never transmits a request and cannot
command the inverter. The flaw itself is in the inverter firmware; this only
gives visibility. Full background: the CCC report in `docs/hoymiles_dtu_vuln.pdf`.

Key code:
- `components/hoymiles_dtu/protocol.cpp` → `hm_classify_sniffed_packet()`
- `components/hoymiles_dtu/hoymiles_dtu.cpp` → `enter_monitor_` / `poll_monitor_`
  / `read_monitor_packets_` / `report_scan_`
- Tunables (top of `hoymiles_dtu.cpp`): `MONITOR_CHANNEL_DWELL_MS` (20 ms),
  `SCAN_REPORT_THROTTLE_MS` (2000 ms).

Verified so far: native classifier tests (incl. the report's real `0x02`/`0x06`
captures), `esphome config` on all test YAMLs, and a full ESP32 firmware compile
with `scan_detection` on. **Not yet verified on-air** — that's tomorrow.

## What AhoyDTU can and cannot exercise

AhoyDTU is a legitimate DTU, not the report's scanner. So it exercises the
**targeted** detection paths, not the broadcast one:

| Detector verdict        | Opcode | Triggered by AhoyDTU?                       |
|-------------------------|--------|--------------------------------------------|
| `foreign poll`          | `0x15` | **Yes** — AhoyDTU polling our inverter      |
| `FOREIGN DevControl`    | `0x51` | **Yes** — AhoyDTU setting a power limit      |
| `info probe`            | `0x06` | Maybe, depending on AhoyDTU's probing        |
| `Search-ID scan`        | `0x02` | **No** — AhoyDTU does not broadcast Search-ID |

To test `Search-ID scan` specifically you would need the report's scanner
firmware, which we deliberately did **not** build. AhoyDTU validates the
foreign-poll / foreign-control paths, which reuse the same monitor + classifier,
so a clean pass there is strong evidence the whole path works.

## The one setting that must be right: distinct DTU serials

The classifier flags a request as an intruder when the **sender DTU serial**
embedded in it (`[5..8]`) differs from *our* DTU serial. If AhoyDTU and the
esphome device share a DTU serial, AhoyDTU traffic is treated as "ours" and
ignored.

- The esphome device auto-generates its DTU serial from its MAC unless you set
  one. It is logged at boot: `nRF24 radio ready, DTU serial 0x........`.
- AhoyDTU has its own DTU serial (Settings → Inverter/DTU). Note it.
- Confirm the two differ before testing. The `scan_detected` report echoes the
  attacker serial — it should match AhoyDTU's.

## Test procedure

1. Build/flash the esphome device from this branch with `scan_detection: true`
   and a `scan_detected` text sensor (see `tests/components/hoymiles_dtu/test.scan-detect.yaml`).
   Record its DTU serial from the boot log.
2. Point AhoyDTU at the **same inverter serial** so it polls the same address on
   the same channels.
3. Watch the esphome logs and the `scan_detected` entity. Expect `foreign poll`
   reports within seconds-to-minutes; the reported DTU serial should be
   AhoyDTU's.
4. From AhoyDTU, set an active power limit → expect a `FOREIGN DevControl`
   report.

## If nothing is detected — debug order

1. `dump_config` shows `Scan detection: enabled` — confirm the option took.
2. Set logger to `DEBUG`. Even when reports are throttled, each classified
   packet logs `Scan packet: ... opcode=0x.. dtu=0x........ ch=..`. Raw hex of
   everything received shows on `last_rx_payload`.
3. Confirm both devices are on the same inverter and the two DTU serials differ.
4. Detection is **probabilistic** — both sides channel-hop, so give it time and
   let AhoyDTU poll repeatedly. Absence of a report is not proof of no scan.
5. The monitor only runs while the esphome device is **idle**. With both devices
   polling the same inverter there is RF contention; to get long, clean monitor
   windows, raise the esphome `update_interval` (e.g. 60–120 s) or temporarily
   point the esphome device at a dummy/second inverter so its own polling backs
   off. An `irq_pin` gives prompt wakeups and better catch rate.

## Known limitations to keep in mind (expected, not bugs)

- Not perfectly stealthy: HM dynamic-payload RX is coupled to the nRF24's
  hardware auto-ACK, so the monitor ACKs what it hears. AhoyDTU may see an extra
  ACK; harmless.
- Blind during the esphome device's own polls; range-bound to RF proximity.
- Targeted-request detection covers the **first** configured inverter; Search-ID
  detection is inverter-independent.

## Possible next steps (after detection is validated)

- Tune `MONITOR_CHANNEL_DWELL_MS` if catch rate is low.
- Revisit countermeasures (enforcement / keep-alive) only once detection is
  proven and only with the grid-safety caveats from the earlier discussion.
- Decide whether `docs/hoymiles_dtu_vuln.pdf` should be committed (the README
  links to it; currently untracked).
