# Changelog

## v0.1.0 — 2026-08-24

First tagged release. **Exploratory research, not a product** — the task framing
is inherited from a public dataset rather than specified by anyone. Read
[doc/FRAME.md](doc/FRAME.md) before quoting a number.

### What it is

An integer-only nearest-neighbour intent router for ESP32-class hardware. No
floats, no learned parameters, no training step: a hashed character-n-gram
encoder, a sparse ternary vector index, and an integer similarity.

### Measured

On held-out data — 220 IoT commands, 2754 negatives, threshold 136:

| | recall | fa | wa | missed |
|---|---|---|---|---|
| shipped index | 84.1% ±2.5 | **12** (0.44%) | 15 | 20 |

`fa` — false actuations — is the number that matters for something driving a
relay; recall cannot see it. Two bits per dimension beats one at **matched
bytes** (`p = 0.0115` paired, at the shipped configuration).

On a stock ESP32-D0WD-V3, 240 MHz, ESP-IDF v5.5:

- **4.3 ms** per utterance, index **100% resident in SRAM**, flash untouched
- **PARITY EXACT** — 64/64 class and score, bit-identical to the host
- **4.3 ms at 100% residency with the WiFi stack up**, verified through a live
  TLS handshake with the radio associated
- **23 mA / 0.496 mJ** per query, measured with an inline USB meter

### Blob format v2 — the sign plane as an exception set

The index is 78.51% zeros, 21.34% `+1`, and **0.16% `-1`**. The sign plane was
122,880 bytes carrying about 1.6 KB of information. v2 makes `+1` implicit and
stores only the exceptions:

    blob            261,036 -> 147,377 B
    vector payload  245,760 -> 132,101 B   (64 -> 34.4 B/vector)
    latency            6.46 -> 4.3 ms

**Lossless.** Routing is bit-identical, proved on hardware by `PARITY EXACT`, so
every number recorded on the held-out set remains exactly valid. This is
deliberately *not* the lossy "drop the sign plane" variant — `--condcentre`
showed that plane is worth twenty points under a different perturbation.

### Getting it onto a board

- **[entromorphic.github.io/mogwai](https://entromorphic.github.io/mogwai/)** —
  flashes over WebSerial from Chrome or Edge, nothing to install
- A single 428 KB image at offset `0x0`, attached to this release. `make image`
  builds it locally.

Both were verified by downloading the published artifact, erasing the chip, and
booting it with nothing else on the flash.

### Known limitations

- **Nine IoT intents** from a public dataset. Not a general NLU.
- **0.44% false actuations** on held-out negatives. Real, deliberate, and the
  price of pruning the index to fit SRAM.
- **One board.** ESP32-D0WD-V3 only; the popcount table, DRAM placement and QIO
  result are specific to LX6 with no SIMD.
- **GPIO16/17 are the PSRAM lines on WROVER modules.** Override the pins at
  build time if you have one.
- **Power figures beyond the measured 23 mA are estimates** from a datasheet, on
  a USB-powered devkit, unvalidated off USB.
- The IRAM-only lift path is **dormant** — v2 fits entirely in DRAM, so nothing
  exercises it.

### Verification

72-check host regression, mutation coverage 70 of 72, an exhaustive 2³² popcount
proof, and a bit-identity guard over 5,863,680 host comparisons plus 200,000
randomised adversarial pairs. CI builds four firmware variants on Linux/GCC.
