# mogwai - twin-ternary intent routing for ESP32

[![regress](https://github.com/EntroMorphic/mogwai/actions/workflows/regress.yml/badge.svg)](https://github.com/EntroMorphic/mogwai/actions/workflows/regress.yml)
[![release](https://img.shields.io/github/v/release/EntroMorphic/mogwai?label=flashable%20image)](https://github.com/EntroMorphic/mogwai/releases/latest)

mogwai is a small, deterministic natural-language router for ESP32-class
hardware. It maps short utterances to device intents with an integer-only
nearest-neighbour index, then actuates GPIO when the accepted intent has a device
effect.

The shipping configuration runs on a stock ESP32-D0WD-V3 in **4.3 ms/query**
with the **137 KB index fully resident in SRAM**. Host and device arithmetic are
checked by embedded reference queries and have passed `PARITY EXACT` on classic
ESP32 and ESP32-C6 boards.

## What It Is

mogwai is built from three pieces:

1. **Encoder** - character 3/4-grams hashed into `RD=256` integer dimensions.
2. **Twin-ternary index** - each dimension carries presence plus sign, so sparse
   text evidence keeps a native zero state.
3. **Router** - integer Dice-style scoring over a pruned nearest-neighbour index,
   followed by thresholding and intent-specific GPIO effects.

The device firmware and the host tools share the same C implementation through
symlinked source files. The `router.bin` blob is the host/device contract: it
contains the index, labels, threshold, compressed sign exceptions, and reference
queries for device parity.

## What Makes It Special

- **Integer-only hot path**: `int32_t`, bit masks, popcount, and fixed layout.
- **Deterministic decisions**: every accepted command has a score, margin, label,
  and nearest stored utterances on the host.
- **MCU-sized footprint**: shipped `router.bin` is 147,377 B; resident index is
  137 KB; all 3840 vectors fit in ESP32 SRAM.
- **Lossless v2 blob format**: the sign plane is stored as exceptions, preserving
  bit-identical routing while cutting resident bytes.
- **Hardware parity harness**: device firmware re-routes 64 host-computed
  references and prints `PARITY EXACT` only when class and score match exactly.
- **Measured deployment path**: bare ESP32, WiFi-associated ESP32, validation
  firmware, release image, and power probe each have dedicated build paths.
- **C-first workflow**: host tools, serial driver, regression tests, blob tools,
  and diagnostics live in this repo as C programs.

## Quick Start

Host development needs a C compiler and `curl`.

```sh
make demo
make route TEXT="turn off the kitchen light"
make repl
make regress
```

`make demo` fetches pinned corpora, builds the tools, routes example utterances,
and prints the shipped operating-point summary.

Example host route:

```text
"turn off the kitchen light"
  decision               iot_hue_lightoff
  score                  220   (threshold 136, margin +84)
  polarity               off cue present, winner already agrees
  encoding               42 of 256 dims carry evidence
  nearest stored utterances:
     220  iot_hue_lightoff       "turn off the kitchen lights"
     199  iot_hue_lightoff       "turn off the light in the kitchen"
     181  iot_hue_lightoff       "kitchen light off"
```

## Flash A Board

Use the browser flasher for the fastest path:

**[entromorphic.github.io/mogwai](https://entromorphic.github.io/mogwai/)**

Chrome and Edge can flash the published ESP32 image over WebSerial.

Or write the release image with `esptool`:

Current release: **[v0.1.6](https://github.com/EntroMorphic/mogwai/releases/tag/v0.1.6)**.

```sh
curl -LO https://github.com/EntroMorphic/mogwai/releases/latest/download/mogwai-esp32.bin
curl -LO https://github.com/EntroMorphic/mogwai/releases/latest/download/mogwai-esp32.bin.sha256
shasum -a 256 -c mogwai-esp32.bin.sha256
esptool --chip esp32 --port /dev/ttyUSB0 write_flash 0x0 mogwai-esp32.bin
```

The release image is a complete ESP32 firmware image for boards with at least
4 MB of flash. Board restore notes live in
[board_backup/RESTORE.md](board_backup/RESTORE.md).

## Build For Hardware

Install ESP-IDF v5.5 for firmware builds.

Build and flash the product firmware:

```sh
make c/bin/mkblob
./c/bin/mkblob data/train.json data/validation.json data/test.json \
               data/nlu_home.csv esp32_router/main/router.bin
cd esp32_router
idf.py -DPRODUCT=1 -DRD=256 -DTPOPCNT=1 build flash monitor
```

Build and flash the validation firmware:

```sh
cd esp32_router
idf.py -DRD=256 -DTPOPCNT=1 build flash monitor
```

Build validation firmware for ESP32-C6:

```sh
cd esp32_router
idf.py -B build-c6 -DSDKCONFIG=/tmp/mogwai-sdkconfig-c6 -DIDF_TARGET=esp32c6 \
       -DRD=256 -DTPOPCNT=1 build flash monitor
```

The product firmware accepts UART lines and drives these GPIO effects:

| intent | effect |
|---|---|
| `iot_hue_lighton` / `iot_hue_lightoff` | LEDC PWM on GPIO2 full / zero |
| `iot_hue_lightup` / `iot_hue_lightdim` | PWM +/-64 of 255 |
| `iot_hue_lightchange` | PWM to half when dark |
| `iot_wemo_on` / `iot_wemo_off` | GPIO4 level |
| `iot_cleaning` | GPIO16 pulse, 250 ms |
| `iot_coffee` | GPIO17 pulse, 250 ms |

GPIO2 is the onboard LED on many ESP32 devkits, so light intents are visible on
an unmodified board.

## Current Measurements

| area | result |
|---|---:|
| shipped blob | 147,377 B |
| resident index | 137 KB |
| index vectors | 3840 |
| SRAM residency | 3840/3840 vectors |
| ESP32 product latency | 4.3 ms/query |
| ESP32 product latency with WiFi associated | 4.3 ms/query |
| ESP32 validation parity | 64/64 class and score, bit-exact |
| ESP32-C6 validation parity | 64/64 class and score, bit-exact |
| validation suite | 105 checks |
| scan energy, measured on devkit | 0.496 mJ/query |

Latest attached-board validation:

| board | port | target | result |
|---|---|---|---|
| ESP32-D0WD-V3 | `/dev/cu.usbserial-1140` | `esp32` | `PARITY EXACT` |
| ESP32-C6FH4 | `/dev/cu.usbmodem11101` | `esp32c6` | `PARITY EXACT` |
| ESP32-C6FH4 | `/dev/cu.usbmodem11201` | `esp32c6` | `PARITY EXACT` |

## Routing Quality

The shipped operating point uses the twin-ternary representation with threshold
136 and a pruned 3840-vector index.

| split | command recall | non-command set size | per query |
|---|---:|---:|---:|
| dev | 85.9% +/-2.5 | 1330 | 4.3 ms |
| held-out | 84.1% +/-2.5 | 2754 | 4.3 ms |

The representation result also holds on all 60 MASSIVE intents as pure nearest
neighbour classification:

| representation | accuracy | bytes/vector |
|---|---:|---:|
| twin-ternary, d=256 | 72.0% | 64 |
| binary, d=512 | 69.3% | 64 |

Research protocol, operating curves, pre-registrations, falsifiers, and the full
result record live in [doc/FRAME.md](doc/FRAME.md), [doc/METHOD.md](doc/METHOD.md),
and [doc/EXPERIMENTS.md](doc/EXPERIMENTS.md).

## Developer Workflow

```sh
make fetch       # fetch pinned corpora and verify checksums
make compare     # dev/validation evaluation
make ship        # shipped operating point used by this README
make tools       # build every host tool and test helper
make regress     # full host regression suite
make image       # merged flashable ESP32 image in dist/
```

`make testset`, `make testset-ship`, and `make testset-sel` read the held-out
split and append budgeted result rows. The test-set protocol is documented in
[doc/METHOD.md](doc/METHOD.md).

## Repo Map

| path | purpose |
|---|---|
| `c/src/` | router, encoder, pruning, gates, tools, diagnostics |
| `c/test/` | host regression helpers and blob/image guards |
| `esp32_router/` | ESP-IDF component and firmware variants |
| `doc/QUICKSTART.md` | longer onboarding path |
| `doc/BLOB_FORMAT.md` | `router.bin` layout and parser contract |
| `doc/TOOLS.md` | host tool reference |
| `doc/FRAME.md` | scope and interpretation guide |
| `doc/METHOD.md` | measurement governance and release discipline |
| `doc/EXPERIMENTS.md` | full research record |
| `journal/` | Lincoln Manifold Method artifacts |
| `results/` | stamped evaluation logs and release validation rows |
| `flash/` | browser flasher published to GitHub Pages |
| `board_backup/` | ESP-AT restore procedure for the development board |

## Standing Constraints

- Repo-owned code is C.
- The router hot path is integer-only.
- Superseded work is archived with provenance.
- Corpora are fetched at pinned revisions and verified by checksum.
- Structural changes run through `make regress`.
- Firmware changes run through ESP-IDF build and validation parity.

## License

[MIT](LICENSE) (c) 2026 Tripp Josserand-Austin.

The license covers this repository's code and documentation. Fetched corpora and
retained upstream provenance keep their original licenses and attribution paths;
see [doc/ARCHIVE.md](doc/ARCHIVE.md) and [provenance/README.md](provenance/README.md).

- **`provenance/needle-upstream.bundle`** is a git bundle of
  [anjaustin/needle](https://github.com/anjaustin/needle), retained for
  provenance under its own terms. This project shares no code with it.
