# mogwai — twin-ternary intent router for ESP32

[![regress](https://github.com/EntroMorphic/mogwai/actions/workflows/regress.yml/badge.svg)](https://github.com/EntroMorphic/mogwai/actions/workflows/regress.yml)
[![release](https://img.shields.io/github/v/release/EntroMorphic/mogwai?label=flashable%20image)](https://github.com/EntroMorphic/mogwai/releases/latest)

A natural-language interface for controlling ESP32-class hardware, built as an
**integer-only nearest-neighbour router** rather than a language model.

No floats. No learned parameters. No training step. A hashed character-n-gram
encoder, a sparse ternary vector index, and an integer similarity — and it routes
an utterance on a stock ESP32-D0WD-V3 in **4.3 ms**, with the whole index
resident in SRAM, flash untouched on every query, bit-identical to the host.

The premise being tested is that **compressing an LLM onto an MCU is not
necessary for this task**. Controlling nine IoT intents does not need 45M
parameters; it needs a good representation and an honest threshold.

> **Status: exploratory research.** There is no product and no real user, and
> the task framing is inherited from a public dataset rather than specified by
> anyone. Read [FRAME.md](doc/FRAME.md) before quoting any number from this repo.

## Provenance

This began as an audit of [anjaustin/needle](https://github.com/anjaustin/needle)
— the Cactus Needle 2 Python package — and asks whether that approach is needed
for this task. It **shares no code with it**. The original clone is archived
locally with its full history; see [doc/ARCHIVE.md](doc/ARCHIVE.md).

## Try it

**On hardware** — utterance in over UART, GPIO out:

    cd esp32_router && idf.py -DPRODUCT=1 -DRD=256 -DTPOPCNT=1 build flash monitor

    ===== mogwai =====
    index 3840 vectors, threshold 136, 10 classes
    pins: light PWM=GPIO2  wemo=GPIO4  cleaning=GPIO16  coffee=GPIO17
    index 3840/3840 vectors in SRAM (100%), 15 chunks of 256
      3840 in DRAM, 0 in the IRAM-only pool (malloc cannot reach it)
      addressing verified over all 3840 vectors: 0 MISMATCHED

    > turn the lights on
      iot_hue_lighton score 227  (margin +91)
      ACTUATED     light -> ON  (duty 255/255)
      4437 us

    > what time does the train leave
      REJECTED     nearest match is not a command — no output changed
      4290 us

Nothing actuates unless the router accepts, and "the nearest thing I know is not
a command" is reported as a *different* rejection from "nothing scored high
enough". See [esp32_router/README.md](esp32_router/README.md).

### Just flash it

**[entromorphic.github.io/mogwai](https://entromorphic.github.io/mogwai/)** — plug
in an ESP32 over USB and click. Nothing to install: it flashes over WebSerial
from Chrome or Edge.

Or one file at one offset, no toolchain and no checkout. The current release is
**[v0.1.1](https://github.com/EntroMorphic/mogwai/releases/tag/v0.1.1)**; every
tag carries the same two assets, and `make image` builds the identical file
locally:

    curl -LO https://github.com/EntroMorphic/mogwai/releases/latest/download/mogwai-esp32.bin
    esptool --chip esp32 --port /dev/ttyUSB0 write_flash 0x0 mogwai-esp32.bin
    # (older ESP-IDF ships it as `esptool.py`)

A `mogwai-esp32.bin.sha256` is published beside it. What each release changed,
and what it still does not do: [CHANGELOG.md](CHANGELOG.md).

Needs an ESP32 with **4 MB of flash or more** — the partition table puts a 2 MB
app at `0x10000`, so the image needs `0x210000` bytes. Most devkits are 4 MB.

Verified end to end rather than assumed: the artifact published by CI was
downloaded, written to an **erased** chip at `0x0`, and the board booted and
routed with nothing else on the flash — `3840/3840 vectors in SRAM`, score 227,
4436 µs.

> **It erases the board.** ESP32 devkits usually ship with ESP-AT; if you want
> it back, save it first — [board_backup/RESTORE.md](board_backup/RESTORE.md).


### What the published image does and does not guarantee

The `.bin` is **not signed**, and the firmware does not enable secure boot or
flash encryption. The `mogwai-esp32.bin.sha256` published beside it is served
from the same origin as the binary, so it detects a corrupted or truncated
download — not a compromised release. Treat it as you would any unsigned
hobbyist firmware.

What you *can* do is rebuild it and diff:

    make image     # then compare against the release asset

The build is reproducible except for four things, and we know exactly which
bytes they are because v0.1.0 and v0.1.1 were compared this way: the version
string at app offset `0x35`, the build timestamp at `0x71`–`0x77`, the
`app_elf_sha256` at `0xb1`–`0xcf`, and esptool's appended image digest — plus a
timestamp and digest in the bootloader. **107 bytes of 428,016.** Anything else
differing means the artifact is not what this source produces.

The browser flasher loads `esp-web-tools` pinned to an exact version from a
public CDN. That is a third party in the trust path, which is the price of
flashing without installing anything.

**On the host** — two dependencies, a C compiler and `curl`:

    make demo                                    # 60-second tour
    make route TEXT="turn off the kitchen light" # route one utterance, see why
    make repl                                    # interactively

Build is about a second, a full evaluation about a second, and the whole
75-check regression suite runs in 25 s - most of which is an exhaustive 2^32
popcount proof. Full path in [doc/QUICKSTART.md](doc/QUICKSTART.md).

```
$ make route TEXT="turn off the kitchen light"

  "turn off the kitchen light"
    decision               iot_hue_lightoff
    score                  220   (threshold 136, margin +84)
    polarity               negative cue present, winner already agrees
    encoding               42 of 256 dims carry evidence
    nearest stored utterances:
       220  iot_hue_lightoff       "turn off the kitchen lights"
       199  iot_hue_lightoff       "turn off the light in the kitchen"
       181  iot_hue_lightoff       "kitchen light off"
       181  iot_hue_lightoff       "please turn off kitchen light"
       180  iot_hue_lightoff       "turn off the lights in the kitchenn"
```

## Results

Two numbers matter and they are measured on **different splits**. Keeping them
apart is the point of this section.

### The representation claim — held-out

Two bits per dimension beats one, and the comparison is **size-matched**: binary
at d=512 spends the same 64 bytes per vector. All rows unpruned, so the
byte-matched comparison stays honest.

That comparison is between *encodings*, and it is unchanged. What has changed is
what those encodings cost to **store**: twin-ternary's sign plane is 99.84%
constant and compresses losslessly to 34.4 B/vector, where binary has no sign
plane to compress. So twin now wins on bytes as well as on the curve — but that
is a storage result, not a re-run of this experiment.

| | recall | fa | wa | missed | vector payload |
|---|---|---|---|---|---|
| binary, 1 bit/dim, d=256 | 80.7% ±2.8 | 1 | 13 | 24 | 328 KB |
| binary, 1 bit/dim, d=512 | 79.7% ±2.9 | 0 | 15 | 24 | 656 KB |
| **twin-ternary, 2 bit/dim, d=256** | **85.9% ±2.5** | **1** | **13** | **14** | **656 KB** |

At identical bytes per vector, twin recalls **6.2 points more** and misses **10
fewer** commands. Binary's operating curve is *identical* at d=256 and d=512 — it
saturates, so the gain is structural, not capacity. Paired test on the held-out
set: `fixed 25, broke 11,` **`p = 0.0288`**. Re-run later at the configuration
that actually ships — both variants pruned — it was `fixed 29, broke 12,`
**`p = 0.0115`**.

On held-out data at threshold 136 — 220 IoT commands, 2754 negatives:

    twin-ternary, d=256, unpruned    recall 84.1% ±2.5   fa 8 (0.29%)   wa 15   missed 20

### What actually ships

The shipped blob is the twin-ternary row with the index **pruned from 656 KB to
3840 vectors**, then stored in the v2 exception format — **137 KB resident** —
so it fits entirely in SRAM. That is a memory decision, not an accuracy one:
chunks are 8 KB, the heap reserve is 40 KB, and 15 chunks of 256 masks now hold
the whole index in DRAM without touching the IRAM-only pool. It is
`RSHIP_NEGTOP` in [`c/src/router.h`](c/src/router.h), beside `RSHIP_TH`.

The v2 format stores the sign plane as the 1,539 exceptions it actually is
rather than 122,880 bit-plane bytes, which is **lossless**: routing is
bit-identical to v1, proved on the device by `PARITY EXACT`. See
[doc/BLOB_FORMAT.md](doc/BLOB_FORMAT.md).

| index | split | recall | fa | wa | missed | per query |
|---|---|---|---|---|---|---|
| unpruned, 656 KB | dev | 85.9% ±2.5 | **1** (0.07%) | 13 | 14 | 34.3 ms † |
| **SHIPPED, 137 KB** | dev | 85.9% ±2.5 | **6** (0.45%) | 13 | 14 | **4.3 ms** |
| unpruned, 656 KB | **held-out** | 84.1% ±2.5 | **8** (0.29%) | 15 | 20 | 34.3 ms † |
| **SHIPPED, 137 KB** | **held-out** | **84.1% ±2.5** | **12** (0.44%) | **15** | **20** | **4.3 ms** |

† measured on the v1 layout and not re-run under v2; the unpruned index has
never been the shipped one. Every **accuracy** figure above is unaffected by the
format change, because the arithmetic is bit-identical.

**The cost is false actuations, and nothing else.** Recall, `wa` and `missed` are
*identical* to the unpruned index on **both** splits — on held-out data they are
bit-for-bit 84.1% / 15 / 20 — because at threshold 136 a negative is never an IoT
utterance's nearest neighbour, so pruning negatives changes only what gets
*rejected*. The hardware confirms it too: IoT scores are byte-identical to the
656 KB build.

That invariance was [pre-registered with
falsifiers](doc/EXPERIMENTS.md#test-evaluation-6--pre-registered-does-the-pruning-cost-transfer)
and then [tested on the held-out
set](doc/EXPERIMENTS.md#test-evaluation-6--result-the-invariance-transferred-and-the-cost-is-half-what-i-predicted).
All four predictions held; none of the falsifiers fired. The price of a 2.7×
smaller footprint and a 5.4× faster scan — the figures **as the trade was made**,
both under the v1 format — is **four extra false actuations in 2754 held-out
non-commands**, 0.44% against 0.29%. The v2 format later took those same 3840
vectors to 129 KB and 4.3 ms losslessly and at no accuracy cost; that is a
separate change and does not move this trade.
It is still a regression on the property this project weighs above recall, and
it was taken deliberately. If your application cannot spend it, build the
unpruned blob instead:

    ./c/bin/mkblob <data> out.bin --prune-negtop=0 --threshold=136

The firmware lifts whatever fits and is correct either way.

The held-out set has now been read across **evaluations #2 through #7**. Every
read was pre-registered with falsifiers and logged against a budget
(`results/TEST_BUDGET`), which is the discipline that makes repeated reads
defensible — but independence erodes with each one, and 84.1% is not as clean a
number as a single-shot measurement would be. This is stated here rather than
only in [METHOD.md](doc/METHOD.md), because it qualifies every figure above.

### Read `fa`, not recall

Recall is `correct_IoT / total_IoT`. It cannot see false actuations at all, so it
is maximised by never rejecting anything. For a router that drives a relay, `fa`
is the number that matters, and for what ships it is **12 in 2754 held-out
non-commands — 0.44%**.

Held-out, at the shipped threshold, the two representations trade off like this:

| | recall | fa | wa | missed | vector payload |
|---|---|---|---|---|---|
| binary, 1 bit/dim | 75.5% ±2.9 | **10** | 14 | 40 | 120 KB |
| **twin-ternary, 2 bit/dim** | **84.1% ±2.5** | 12 | 15 | **20** | 129 KB |

Twin buys recall and pays a little precision: it misses **half** as many commands
(20 against 40) for two more false actuations. Binary is the more conservative
router and the cheaper one. Which you want is a deployment choice, and the
threshold is the knob — the full operating curve is in
[doc/EXPERIMENTS.md](doc/EXPERIMENTS.md).

## On hardware

    ESP32-D0WD-V3, 240 MHz, QIO flash @ 80 MHz, stock ESP-IDF v5.5

    SHIPPED, 3840 vectors / 137 KB     4.3 ms   100% resident, all in DRAM
    the same, with the WiFi stack up   4.3 ms   100% resident ‡
    unpruned, 10500 vectors / 656 KB  43.5 ms   (1 core)   26.7 ms  (2 cores) †
    parity vs host                    64/64 class and score, bit-exact

† v1 layout, not re-run under v2.
‡ measured associated (DHCP lease held) and unchanged by it: 4.33–4.52 ms
  associated against 4.29–4.51 ms merely initialised. A live TLS fetch over the
  same link returned HTTP 200 and dipped free heap to 49,184 B — a 37,912 B peak
  draw against the 61,440 B reserve — with the whole index still resident, and
  routing afterwards was score-identical (206, 233). Under v1 the same build ran
  9.3 ms at 70% residency; the gap closed because the index now fits alongside
  the network stack instead of spilling to flash.

Optimisation history: 200.4 → 102.1 (clocks) → 78.8 (precomputed activity
counts) → 43.5 ms (popcount table) → 34.3 ms (chunked SRAM residency) → 6.3 ms
(index pruned to the largest fully-resident size) → **4.3 ms** (v2 exception
format: half the bytes per vector, half the popcounts). Every step held bit-exact
parity. The second core is real, but **core 1 belongs to WiFi on a production
device**, so the single-core number is the one that survives deployment.

Cost model, fitted across three dimensions: **59.8 ns/byte + 326 ns/vector**.
Bytes are 92% of the cost, so index size predicts latency directly — which is
why shrinking the index, not speeding up the arithmetic, was the lever that paid.

That fit is flash-mapped. Resident in SRAM the slope is far shallower — v1 ran
1683 ns/vector at 64 B, implying **21.2 ns/byte + 326** — and v2 is an
out-of-sample test of the *form*, because it nearly halved the bytes without
touching the arithmetic. Predicted 1055 ns/vector at 34.4 B; **measured 1096, a
3.7% error**. The model was not refitted.

Getting the index into SRAM looked impossible for a while. Free heap is **295 KB
in total but only 164 KB in the largest region**, so the single `malloc` that
would lift it can never succeed — and it fails *silently*, falling back to flash
while reporting success. The scan is sequential, so the index does not need one
allocation. Lifting it in 8 KB chunks uses nearly all the free heap; any chunk
that will not fit stays flash-mapped and scores identically. Details:
[Chunked SRAM residency](doc/EXPERIMENTS.md#chunked-sram-residency-the-index-does-not-need-one-allocation).

Two consequences worth knowing before deploying this. **3840 vectors is a
ceiling, not a target** — the index cannot grow indefinitely without chunks
falling back to flash, and a flash-resident chunk costs real milliseconds on
every query forever. And the free-heap figures depend on whether the network
stack is up: WiFi takes ~72 KB of liftable heap and TLS wants a 61 KB reserve
on top. Under the v1 64-byte format that was decisive — the index dropped to
70% resident and 9.3 ms with WiFi running. **Under v2 it is not**: the index is
137 KB instead of 240 KB, so it stays 100% resident either way and routes in
4.3 ms with the radio associated, verified through a live TLS handshake. See
[the sign plane write-up](doc/return-to-me/the-sign-plane-is-an-exception-set.md).

## Power

Inline USB power meter on the devkit, holding each state 30 s with the serial
line silent and the actuator pins parked (`MOGWAI_POWER=1`).

**Measured:**

| state | current | power |
|---|---:|---:|
| idle | 48 mA | 245.8 mW |
| scanning, 100% duty | 71 mA | 363.5 mW |
| **the scan itself** | **23 mA** | **117.8 mW** |

At 5.12 V and a 4.21 ms scan that is **0.496 mJ per query**, and the scan
saturates — runs continuously — only at **238 queries/second**.

### The devkit number is the misleading one

Read literally, that says the router is irrelevant to power: 0.5 mW against a
246 mW baseline at 1 query/s. But the meter reads the **whole devkit**, and the
ESP32 datasheet puts idle at 240 MHz around 30 mA — leaving ~18 mA, **38% of the
baseline, as USB bridge and regulator**. A product has neither.

Estimated at 3.3 V with light sleep between queries (light, not deep: deep sleep
loses SRAM, and a 137 KB resident index is the whole design):

| design | avg | power | router's share | 2000 mAh |
|---|---:|---:|---:|---:|
| always awake, 240 MHz | 30.1 mA | 99.3 mW | 0.3% | 2.6 days |
| **light sleep, 1 query/s** | **1.02 mA** | **3.37 mW** | **9.5%** | **78 days** |
| light sleep, 10 queries/s | 3.00 mA | 9.89 mW | 32.3% | 27 days |

So the router is **~10% of a sleeping product's budget at 1 Hz** and a third of
it at 10 Hz. What is irrelevant is its contribution *on a devkit*. Under the same
model the v2 format was worth **10.3%** of total power at 1 Hz and **28.2%** at
10 Hz — where on the devkit the identical change is worth 0.2 mW of 246, which
is invisible. Both are correct; only one is about a product.

> **Not validated off USB.** Only the first table is measured. Everything below
> it is arithmetic on a datasheet, taken on a devkit powered over USB, and no
> part of it has been checked on a board running from a battery or a bare 3.3 V
> supply. It rests on three assumptions, any of which would move the numbers:
> the 30 mA idle figure; a **linear LDO**, where input current ≈ output current
> (true for the AMS1117 most devkits carry, false for a buck converter); and
> light sleep actually reaching 0.8 mA — which a **live network connection would
> break outright**, and none of this models a device holding a WiFi association.
> Treat the product rows as a sizing estimate, not a specification.

Full working, including what would falsify it:
[EXPERIMENTS.md](doc/EXPERIMENTS.md#power-what-a-scan-costs-and-why-the-devkit-hides-it).

## How it works

1. **Encode** — FNV-1a hash character 3/4-grams into `RD=256` dimensions. Each
   dimension gets two bits: `m` (is there evidence here) and `s` (which way,
   versus an integer per-dimension centre). Ternary lets "no evidence" be its
   own state; binary must force it to −1, which is why binary saturates.
2. **Score** — `dot = popcount(ma&mb&~(sa^sb)) − popcount(ma&mb&(sa^sb))`,
   normalised by Dice `(2·dot·256)/(aa+ab+8)`. Symmetric and length-invariant,
   which matters because IoT commands are short and negatives are long.
3. **Route** — argmax over the index; if the best score ≤ threshold, answer
   "none"; otherwise apply polarity cues (on/off/up/dim) to the winner's label.

## Build and flash

    make fetch          # curl the corpora (MASSIVE + NLU-Evaluation-Data), records SHA256
    make compare        # dev/validation evaluation — safe to run as often as you like
    make testset        # HELD-OUT TEST. Burns one budget unit. Deliberately not `make test`.
    make tools          # build every tool and test — run after any signature change
    make regress        # full host regression (75 checks) — run after any structural change

Then the device:

    make c/bin/mkblob
    ./c/bin/mkblob data/train.json data/validation.json data/test.json \
                   data/nlu_home.csv esp32_router/main/router.bin
    cd esp32_router && idf.py -DPRODUCT=1 -DRD=256 -DTPOPCNT=1 build flash monitor

`mkblob` with no prune flags emits **exactly** the shipped configuration, which
is what lets the regression suite prove `router.bin` is reproducible. Any other
prune setting requires an explicit `--threshold=`, because pruning changes the
tuned threshold and shipping the wrong one still passes parity — parity proves
host equals device, not that either is right.

The firmware self-checks against 64 host-computed reference queries embedded in
the blob and prints `PARITY EXACT` only if class **and** score match on all 64.
The layout, and what parity does *not* cover:
[doc/BLOB_FORMAT.md](doc/BLOB_FORMAT.md).

## Repo map

    README.md          you are here
    CHANGELOG.md       what shipped, and what it does not do
    Makefile           every entry point: fetch, demo, route, ship, regress, testset
    c/src/             the entire shipping system — 9 tools + 8 shared units
    c/test/            an exhaustive 2^32 popcount proof, a blob-format validator,
                       a firmware-parser guard, and a release-image integrity check
    esp32_router/      five firmwares from one component: PRODUCT (UART in, GPIO
                       out), VALIDATION (host-parity harness), +WiFi, and two
                       measurement probes (WiFi heap cost, power)
                       Sources are SYMLINKS into c/src
    doc/               QUICKSTART, EXPERIMENTS, METHOD, TOOLS, BLOB_FORMAT, FRAME, ARCHIVE, TODO
    journal/           Lincoln Manifold Method artifacts, 8 cycles
    flash/             the browser flasher, published to Pages by CI on every tag
    scripts/           fetch.sh (curl only), regress.sh (75 checks), mutate.sh
    results/           every run appends a stamped row; TEST_BUDGET is the audit log
    provenance/        the only off-disk copy of three never-pushed upstream commits
    board_backup/      how to restore the board's original ESP-AT firmware
    data/              fetched, never vendored — only SHA256 is tracked
    archive/           superseded work + provenance. LOCAL ONLY (gitignored);
                       inventory in doc/ARCHIVE.md — nothing is ever deleted

## Standing constraints

Small, capable, and governed by three absolute prohibitions. Break them and you
get something much worse — which is the whole reason for the name.

1. **No Python** in anything this repo owns — no product code, no hot path, no
   glue, and no "quick" experiment harness that quietly becomes the deliverable.
   Corpora are fetched with `curl` and the board is driven by `c/bin/devtalk`,
   both written for the purpose. The exception is the **vendor toolchain**:
   ESP-IDF's `idf.py` and `esptool` are Python, and replacing them is not a
   project. That is why `make image` exists — so a *user* needs neither.
2. **No float** on the hot path. `int32_t` is the widest type. No `sqrt`, no
   division that is not integer, no learned parameters anywhere.
3. **Nothing is deleted, only archived.** Superseded work, negative results and
   the autopsies of both are kept — see [doc/ARCHIVE.md](doc/ARCHIVE.md).

The held-out split is a budgeted resource: every read is logged in
`results/TEST_BUDGET`, and configurations are pre-registered with falsifiers
before it is touched. Every run is stamped with the git SHA and the clean/dirty
state of the tree. Run `make regress` after any structural change: 75 checks,
25 seconds.

CI runs the same 75 checks on Linux/GCC and builds the VALIDATION, PRODUCT and
networked firmwares from `sdkconfig.defaults`, asserting the blob and the
firmware agree on `RD`. That job exists because the code had never left
macOS/clang, and three portability bugs were found the first time it did.

How the guardrails were learned, each named for the incident that taught it:
[doc/METHOD.md](doc/METHOD.md).

## License

[MIT](LICENSE) © 2026 Tripp Josserand-Austin.

The MIT grant covers **this repository's code and documentation**. It does not
and cannot relicense material this project does not own:

- **The corpora are not vendored and not covered.** `make fetch` downloads
  [MASSIVE](https://huggingface.co/datasets/mteb/amazon_massive_intent) and
  [NLU-Evaluation-Data](https://github.com/xliuhw/NLU-Evaluation-Data) at build
  time; each carries its own upstream licence. Only `data/SHA256` is tracked.

  Both are pinned to **immutable revisions**, and `fetch.sh` verifies what it
  downloaded against the tracked checksums rather than regenerating them — so a
  changed upstream is a loud failure, not a silent rebaseline. That is the
  mitigation for not holding a copy: the exact bytes this repo was built on are
  identified, even though they are not redistributed here. Both datasets are
  CC-BY-4.0 and could be archived with attribution if the upstreams ever
  disappear; that decision has not been taken.
  [anjaustin/needle](https://github.com/anjaustin/needle), retained for
  provenance under its own terms. This project shares no code with it.
- **`archive/`** is local-only and holds superseded work plus a second
  repository; see [doc/ARCHIVE.md](doc/ARCHIVE.md).
