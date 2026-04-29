# wM-Buster

A passive Wireless M-Bus listener for the Flipper Zero. Reads utility
meters (heat, water, gas, electricity, heat-cost allocators) on the EU
868 MHz band using only the built-in CC1101 radio.

> [!CAUTION]
> **Only use this app on meters you own.** Decrypting someone else's
> meter is illegal. The author does not condone unlawful use and is
> not responsible for what you do with this tool. This app is
> receive-only and never transmits.

<p align="center">
  <img src="docs/demoscan.png"    alt="Live meter scan"   width="45%"/>
  &nbsp;&nbsp;
  <img src="docs/demodetails.png" alt="Per-meter detail"  width="45%"/>
</p>

## Features

- **Modes T1, C1, T+C combined, S1** on the CC1101.
  T+C is the default and runs both T-mode (3-of-6) and C-mode (NRZ
  Format A & B) on a single chip configuration via post-sync byte
  detection (`0xCD` / `0x3D` / fall-through).
- **EN 13757-3 application layer**: DIF/VIF walker covering Energy,
  Volume, Mass, Power, Volume flow, Mass flow, Flow / Return / External
  temperature, Temperature difference, Pressure, On-time, Operating
  time, Date, DateTime, HCA units, Fabrication number, Bus address.
- **Manufacturer drivers**: Techem (HCA / heat / water / smoke),
  Kamstrup, Diehl / Hydrometer / Sappel, Qundis, ista, Brunata.
  Unknown manufacturers fall through to the generic walker; payloads
  with proprietary CI bytes are clean-hex-dumped instead of mis-decoded.
- **AES-128-CBC mode 5 auto-decrypt** using the STM32WB hardware crypto
  block. Per-meter keys live in a CSV on the SD card and are loaded at
  startup. Three-state badge (`ENC` / `BAD` / `DEC`) tells you whether
  a key is missing, wrong, or working.
- **Live meter list** with signal-bar RSSI, value preview, and
  filter / sort (Top-N, by signal / recent / id / packets).
- **Detail view** with the decoded reading, model name, packet count,
  encryption state, and a scrollable label/value list.
- **SD card logging** of raw frame + parsed text (optional).

## Compatibility status

The protocol layer is built from public standards (EN 13757-3/4, OMS
Vol. 2) and matches `wmbusmeters` / `rtl-wmbus` byte-for-byte on every
test vector. Field-validation has so far been limited to a single
residential RF environment dominated by Techem hardware.

| Component                                  | Field-validated  |
|--------------------------------------------|------------------|
| T+C reception, `0x543D` sync, 3-of-6, CRC  | yes              |
| Techem CI=0xA2 HCA driver (FHKV-3 / v0x52) | yes              |
| C-mode Format A NRZ                        | yes              |
| Mode 5 enc-mode extraction                 | yes              |
| C-mode Format B NRZ                        | not yet          |
| S1 Manchester @ 868.30 MHz                 | not yet          |
| Kamstrup / Diehl / Qundis / ista / Brunata | not yet          |
| OMS DIF/VIF walker on decrypted payloads   | not yet          |

If you have meters from any of the "not yet" rows, please open a
[compatibility report](https://github.com/i12bp8/wmbuster/issues/new?template=compat.yml)
with manufacturer code, version, medium, and an example raw frame
captured by `rtl_433`. That's the fastest way to make this list grow.

## Build & install

Requires the Flipper [`ufbt`](https://github.com/flipperdevices/flipperzero-ufbt)
toolchain.

```sh
ufbt              # builds dist/wmbuster.fap
ufbt launch       # builds + uploads + starts on a connected Flipper
```

## Testing

Host-side regression tests for the protocol code (no Flipper required):

```sh
make -C tests check
```

The tests parse real `rtl_433`-captured frames (with IDs redacted) and
assert that the manufacturer / version / medium / CI / encryption-mode
extraction matches the reference. Add new vectors to
`tests/test_link.c` whenever you encounter a meter that exposes a new
code path. CI on GitHub Actions runs the suite on every push.

## Usage

1. Open **wM-Buster** under *Apps → Sub-GHz*.
2. Pick **Scan**. The default mode is T+C (covers most EU residential
   buildings). Switch in *Settings* if you need pure T1, C1 or S1.
3. Frames appear within seconds. Each row shows manufacturer code,
   8-digit ID, RSSI bars, and either the latest reading or a status
   badge.
4. Press **OK** on a row to open the detail view.
5. Press **Back** to return to the meter list (radio keeps running) or
   to the root menu (radio keeps running, LED off).

## Encryption keys

Newer EU meters (Techem v0x6A, Kamstrup MULTICAL etc.) ship with
AES-128-CBC mode 5 enabled. To decrypt, drop a CSV onto the SD card
at:

```
/ext/apps_data/wmbuster/keys.csv
```

One line per meter, `#`-comments allowed:

```
# Manufacturer (3 chars), 8-digit hex ID, 32-hex AES-128 key
TCH,27404216,0123456789ABCDEF0123456789ABCDEF
KAM,12345678,FEDCBA9876543210FEDCBA9876543210
```

The keys are typically printed on the meter's installation document
or accessible through the property-management portal that operates
the meter (Techem, ista, etc.).

Restart the app to reload the file. Open **Keys** from the root menu
to see the loaded entries (only a fingerprint of each key is shown).

When a matching key is on file the meter row flips from `ENC` to
`DEC` and the decoded reading appears.

## Mode selection

| Mode | Frequency  | Bit rate          | Encoding              | Notes                       |
|------|------------|-------------------|-----------------------|-----------------------------|
| T1   | 868.95 MHz | 100 kchip/s       | 3-of-6                | Older Techem firmware       |
| C1   | 868.95 MHz | 100 kbit/s        | NRZ, Format A/B auto  | Newer Techem, most Kamstrup |
| T+C  | 868.95 MHz | 100 kbit/s        | T1 or C1, auto-detect | **Default** — broadest      |
| S1   | 868.30 MHz | 32.768 kbit/s     | Manchester            | Legacy water / gas          |

Mode N (169 MHz narrowband) is not supported: the CC1101 in the
Flipper Zero is hardware-locked to the 300/430/868 MHz subbands.

## Project layout

```
wmbus_inspector/
├── application.fam           # ufbt manifest
├── wmbus_app.c               # entry point + scene wiring
├── wmbus_app_i.h             # shared types (WmbusApp, settings, meter row)
├── meters_db.[ch]            # in-RAM bounded meter table
├── key_store.[ch]            # AES-key CSV reader
├── logger.[ch]               # raw + parsed SD logging
├── protocol/
│   ├── wmbus_3of6.[ch]       # EN 13757-4 §6.2.1.1 chip decoder
│   ├── wmbus_manchester.[ch] # Manchester decoder (S1)
│   ├── wmbus_crc.[ch]        # CRC-16/EN-13757 + Format A/B verify
│   ├── wmbus_link.[ch]       # L/C/M/A/CI parsing + enc-mode extraction
│   ├── wmbus_app_layer.[ch]  # DIF/VIF walker + renderer
│   ├── wmbus_aes.[ch]        # AES-CBC over furi_hal_crypto
│   ├── wmbus_manuf.[ch]      # 16-bit manuf code ↔ 3-letter ASCII
│   ├── wmbus_medium.[ch]     # device-type code → human string
│   └── wmbus_models.[ch]     # friendly meter-model names
├── subghz/
│   ├── wmbus_worker.[ch]     # CC1101 RX worker, slicer, CRC
│   └── wmbus_hal_rx.c        # FIFO drain via SubGHz SPI
├── drivers/
│   ├── driver_registry.[ch]  # manufacturer match-and-dispatch
│   ├── driver_generic.c      # OMS standard fall-through
│   ├── driver_techem.c       # FHKV-3/4 HCA, heat, water, smoke
│   ├── driver_kamstrup.c
│   ├── driver_diehl.c
│   └── driver_eu_hca.c       # Qundis / ista / Brunata HCAs
└── views/
    ├── view_meters.c         # meter list scene
    ├── view_detail.c         # single-meter detail scene
    ├── view_settings.c       # mode / filter / sort / logging
    ├── view_keys.c           # key store browser
    ├── scan_canvas.[ch]      # custom canvas widget for the list
    └── detail_canvas.[ch]    # custom canvas widget for the detail
```

## Adding a manufacturer driver

1. Copy `drivers/driver_generic.c` to `drivers/driver_<name>.c`.
2. Implement `match(manuf, version, medium)` and `decode(...)`.
3. Add the file to `application.fam` and an `extern` + entry in
   `drivers/driver_registry.c`.

Drivers receive the (decrypted) APDU starting at the byte after the
CI byte; they may use the generic walker (`wmbus_app_render`) or parse
proprietary fields directly.

## Limitations

- **Sensitivity** is bounded by the small internal antenna; expect
  10–20 dB worse than a USB SDR. A few metres of in-building range
  is realistic.
- **No transmit**: the app is RX-only by design. T2/C2 bidirectional
  exchanges and meter wake-up requests are out of scope.
- **No N-mode** (169 MHz) — hardware limitation of the Flipper.
- The OMS DIF/VIF walker covers the practical subset (energy / volume
  / mass / power / temperature / pressure / dates / HCA). Less common
  quantities are surfaced as Unknown and filtered from the rendered
  list.

## Testing against rtl_433

For ground-truth validation, capture the same RF environment with a
USB SDR and compare:

```sh
rtl_433 -f 868.95M -s 1.6M -Y minmax -R 104 -R 105 -F json > capture.jsonl
```

`-R 104` is the rtl_433 wM-Bus T+C decoder, `-R 105` is S-mode.

## Legal

This is dual-use research software. Receiving on the unlicensed 868 MHz
SRD band is permitted under ETSI EN 300 220 (and equivalent regional
rules) and the app never transmits. Distribution of analysis tools is
generally protected as open-source / academic expression.

**Operating the app** is your responsibility:

- **Your own meter** — typically legal under EU Directive 2018/2002
  Art. 9-11, which gives consumers a statutory right of access to
  their own consumption data; operators must provide the AES-128 key
  on request. Equivalent rules exist in non-EU jurisdictions.
- **Other meters** — can constitute unlawful interception of private
  communications and unlawful processing of personal data under GDPR
  (or equivalents). Check your national statutes before pointing this
  app at a meter that isn't yours.

The maintainers ship no example keys, no decrypted captures and no
identifying telegrams.

## Standards

- **EN 13757-4** — Wireless M-Bus PHY / Link layer
- **EN 13757-3** — Application layer (DIF/VIF)
- **OMS Specification Vol. 2** — Profile + encryption
- **TI SWRA522** — CC1101 wM-Bus implementation note

## Acknowledgements

The protocol implementation was cross-checked against:

- [wmbusmeters](https://github.com/wmbusmeters/wmbusmeters) — the most
  thorough open-source wM-Bus parser
- [rtl-wmbus](https://github.com/xaelsouth/rtl-wmbus) — SDR receiver
  used as the reference comparator during development
- [rtl_433](https://github.com/merbanan/rtl_433) — multi-protocol SDR
  decoder

## License

MIT. See `LICENSE`.
