# Changelog

## v0.1.1 — 2026-08-24

**No firmware change.** Every source file on the shipped path is byte-for-byte
identical to v0.1.0; `git diff v0.1.0..v0.1.1 -- c/src esp32_router/main` is
empty. This release exists because the *pipeline that produced v0.1.0* was
weaker than the artifact it produced, and shipping a build system nobody has
attacked is how a good binary becomes a bad one later.

Red-teaming the deployment found five defects:

- **No test gate.** The release workflow was entirely independent of the test
  suite, so tagging a tree that fails `regress` would have shipped a broken
  image to strangers. A `verify` job now runs all 72 checks first.
- **Hardcoded flash offsets** in two files. `0x1000/0x8000/0x10000` is correct
  only until `partitions.csv` moves — and a moved app produces a plausible image
  of exactly the right size that does not boot. Now `idf.py merge-bin`, which
  reads the offsets out of the build that just happened.
- **Nothing verified the image's contents.** A stale build directory or the
  wrong app passes every other step. Adds `c/test/imgcheck.c`, which finds the
  blob inside the image byte for byte — not a checksum of the image, which says
  nothing about what is in it. It fails on a blob altered by one byte.
- **An unpinned CDN** in the browser flasher: `esp-web-tools@10` is a range that
  silently follows new releases, for a script whose job is flashing firmware.
  Pinned to `10.4.0`.
- **The manifest version stamp** was a `sed` that failed silently if the
  template were ever committed already-stamped. CI now asserts it first.

Two deployment misconfigurations, both invisible in the repository:

- The `github-pages` environment refused tag deployments, so the release
  published its assets but never its flasher.
- Pages was set to the **legacy branch builder**, so every push to `main` served
  the repo root — which has no `index.html` — and overwrote the flasher. It
  looked like a deployment that succeeded and then died for no reason.

Also: `actions/checkout` bumped off deprecated Node 20, mutation coverage
recorded to `results/mutation-summary.txt` rather than stdout alone, and the
README audited end to end — a transcript that spliced a v2 banner onto v1
timings, a size comparison in incomparable units, and a "No Python anywhere"
claim that the ESP-IDF toolchain contradicts.

Verification unchanged and re-run: **72/72** host checks, mutation coverage
**70 of 72**, and the published artifact downloaded, written to an erased chip
at `0x0`, and booted with nothing else on the flash.


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
