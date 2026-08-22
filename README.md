# mogwai — twin-ternary intent router for ESP32

[![regress](https://github.com/EntroMorphic/mogwai/actions/workflows/regress.yml/badge.svg)](https://github.com/EntroMorphic/mogwai/actions/workflows/regress.yml)

A natural-language interface for controlling ESP32-class hardware, built as an
**integer-only nearest-neighbour router** rather than a language model.

No floats. No learned parameters. No training step. A hashed character-n-gram
encoder, a two-bit-plane vector index, and an integer similarity — and it routes
an utterance on a stock ESP32-D0WD-V3 in **6.3 ms**, with the whole index
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
    index 3840/3840 vectors in SRAM (100%), 30 chunks of 128
      addressing verified over all 3840 vectors: 0 MISMATCHED

    > turn the lights on
      iot_hue_lighton score 227  (margin +91)
      ACTUATED     light -> ON  (duty 255/255)
      6455 us

    > what time does the train leave
      REJECTED     nearest match is not a command — no output changed
      6304 us

Nothing actuates unless the router accepts, and "the nearest thing I know is not
a command" is reported as a *different* rejection from "nothing scored high
enough". See [esp32_router/README.md](esp32_router/README.md).

**On the host** — two dependencies, a C compiler and `curl`:

    make demo                                    # 60-second tour
    make route TEXT="turn off the kitchen light" # route one utterance, see why
    make repl                                    # interactively

Build is about a second, a full evaluation about a second, and the whole
62-check regression suite runs in 19 s - most of which is an exhaustive 2^32
popcount proof. Full path in [doc/QUICKSTART.md](doc/QUICKSTART.md).

```
$ make route TEXT="turn off the kitchen light"

  "turn off the kitchen light"
    decision               iot_hue_lightoff
    score                  220   (threshold 136, margin +84)
    polarity               negative cue present, winner already agrees
    encoding               42 of 256 dims carry evidence
    nearest stored utterances:
       220  iot_hue_lightoff   "turn off the kitchen lights"
       199  iot_hue_lightoff   "turn off the light in the kitchen"
```

## Results

Two numbers matter and they are measured on **different splits**. Keeping them
apart is the point of this section.

### The representation claim — held-out

Two bits per dimension beats one, and the comparison is **size-matched**: binary
at d=512 spends the same 64 bytes per vector. All rows unpruned, so the
byte-matched comparison stays honest.

| | recall | fa | wa | missed | size |
|---|---|---|---|---|---|
| binary, 1 bit/dim, d=256 | 80.7% ±2.8 | 1 | 13 | 24 | 328 KB |
| binary, 1 bit/dim, d=512 | 79.7% ±2.9 | 0 | 15 | 24 | 656 KB |
| **twin-ternary, 2 bit/dim, d=256** | **85.9% ±2.5** | **1** | **13** | **14** | **656 KB** |

At identical bytes per vector, twin recalls **6.2 points more** and misses **10
fewer** commands. Binary's operating curve is *identical* at d=256 and d=512 — it
saturates, so the gain is structural, not capacity. Paired test on the held-out
set: `fixed 25, broke 11,` **`p = 0.0288`**.

On held-out data at threshold 136 — 220 IoT commands, 2754 negatives:

    twin-ternary, d=256, unpruned    recall 84.1% ±2.5   fa 8 (0.29%)   wa 15   missed 20

### What actually ships

The shipped blob is the twin-ternary row with the index **pruned from 656 KB to
240 KB** so it fits entirely in SRAM. That is a memory decision, not an accuracy
one: chunks are 8 KB, the heap reserve is 40 KB, and 30 chunks is the most that
fits — so 3840 vectors is the largest fully-resident index. It is
`RSHIP_NEGTOP` in [`c/src/router.h`](c/src/router.h), beside `RSHIP_TH`.

| index | split | recall | fa | wa | missed | per query |
|---|---|---|---|---|---|---|
| unpruned, 656 KB | dev | 85.9% ±2.5 | **1** (0.07%) | 13 | 14 | 34.3 ms |
| **SHIPPED, 240 KB** | dev | 85.9% ±2.5 | **6** (0.45%) | 13 | 14 | **6.3 ms** |
| **SHIPPED, 240 KB** | **held-out** | [pre-registered, not yet run](doc/EXPERIMENTS.md#test-evaluation-6--pre-registered-does-the-pruning-cost-transfer) | | | | |

**The cost is false actuations, and nothing else.** Recall, `wa` and `missed` are
identical to the unpruned index, because at threshold 136 a negative is never an
IoT utterance's nearest neighbour — so pruning negatives changes only what gets
*rejected*. The hardware confirms it: IoT scores are byte-identical to the
656 KB build.

Six events against one is a real regression on the property this project weighs
above recall, taken deliberately to buy a 2.7× smaller footprint and a 5.4×
faster scan. It is also the same order as the ±2.5 dev standard error, so dev
cannot resolve it finely, and **the held-out cost is not yet measured** — the
prediction and its falsifiers are written down in advance.

If your application cannot spend it, build the unpruned blob instead:

    ./c/bin/mkblob <data> out.bin --prune-negtop=0 --threshold=136

The firmware lifts whatever fits and is correct either way.

### Read `fa`, not recall

Recall is `correct_IoT / total_IoT`. It cannot see false actuations at all, so it
is maximised by never rejecting anything. For a router that drives a relay, `fa`
is the number that matters. Twin buys recall and pays a little precision; binary
is the more conservative router. Which you want is a deployment choice and the
threshold is the knob — the full operating curve is in
[doc/EXPERIMENTS.md](doc/EXPERIMENTS.md).

## On hardware

    ESP32-D0WD-V3, 240 MHz, QIO flash @ 80 MHz, stock ESP-IDF v5.5

    SHIPPED, 3840 vectors / 240 KB     6.3 ms   index 100% resident in SRAM
    the same, with the WiFi stack up   9.3 ms   70% resident; WiFi takes ~72 KB
    unpruned, 10500 vectors / 656 KB  43.5 ms   (1 core)   26.7 ms  (2 cores)
    parity vs host                    64/64 class and score, bit-exact

Optimisation history: 200.4 → 102.1 (clocks) → 78.8 (precomputed activity
counts) → 43.5 ms (popcount table) → 34.3 ms (chunked SRAM residency) → 6.3 ms
(index pruned to the largest fully-resident size). Every step held bit-exact
parity. The second core is real, but **core 1 belongs to WiFi on a production
device**, so the single-core number is the one that survives deployment.

Cost model, fitted across three dimensions: **59.8 ns/byte + 326 ns/vector**.
Bytes are 92% of the cost, so index size predicts latency directly — which is
why shrinking the index, not speeding up the arithmetic, was the lever that paid.

Getting the index into SRAM looked impossible for a while. Free heap is **295 KB
in total but only 164 KB in the largest region**, so the single `malloc` that
would lift it can never succeed — and it fails *silently*, falling back to flash
while reporting success. The scan is sequential, so the index does not need one
allocation. Lifting it in 8 KB chunks uses nearly all the free heap; any chunk
that will not fit stays flash-mapped and scores identically. Details:
[Chunked SRAM residency](doc/EXPERIMENTS.md#chunked-sram-residency-the-index-does-not-need-one-allocation).

Two consequences worth knowing before deploying this. **3840 vectors is a
ceiling, not a target** — chunk 31 does not cost a little, it costs 0.33 ms on
every query forever, so the index cannot grow without falling off. And the
295 KB free-heap figure is measured with **the WiFi stack never started**; bring
WiFi up and it is 70% resident and 9.3 ms, because WiFi takes ~72 KB of liftable
heap. That is the chunked design degrading rather than failing — a flat lift
would have dropped to 43.9 ms — but 6.3 ms is a no-WiFi number and is labelled
as one above.

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
    make regress        # full host regression (62 checks) — run after any structural change

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
    Makefile           every entry point: fetch, demo, route, ship, regress, testset
    c/src/             the entire shipping system — 9 tools + 8 shared units
    c/test/            an exhaustive 2^32 popcount proof, a blob-format validator
    esp32_router/      two firmwares: PRODUCT (UART in, GPIO out) and VALIDATION
                       (host-parity harness). Sources are SYMLINKS into c/src
    doc/               QUICKSTART, EXPERIMENTS, METHOD, TOOLS, BLOB_FORMAT, FRAME, ARCHIVE, TODO
    journal/           Lincoln Manifold Method artifacts, 8 cycles
    scripts/           fetch.sh (curl only), regress.sh (62 checks), mutate.sh
    results/           every run appends a stamped row; TEST_BUDGET is the audit log
    provenance/        the only off-disk copy of three never-pushed upstream commits
    board_backup/      how to restore the board's original ESP-AT firmware
    data/              fetched, never vendored — only SHA256 is tracked
    archive/           superseded work + provenance. LOCAL ONLY (gitignored);
                       inventory in doc/ARCHIVE.md — nothing is ever deleted

## Standing constraints

Small, capable, and governed by three absolute prohibitions. Break them and you
get something much worse — which is the whole reason for the name.

1. **No Python** anywhere in the pipeline. Corpora are fetched with `curl`, and
   the board is driven by `c/bin/devtalk`. Not in the product, not on the hot
   path, not as glue, not as a "quick" experiment harness that quietly becomes
   the deliverable.
2. **No float** on the hot path. `int32_t` is the widest type. No `sqrt`, no
   division that is not integer, no learned parameters anywhere.
3. **Nothing is deleted, only archived.** Superseded work, negative results and
   the autopsies of both are kept — see [doc/ARCHIVE.md](doc/ARCHIVE.md).

The held-out split is a budgeted resource: every read is logged in
`results/TEST_BUDGET`, and configurations are pre-registered with falsifiers
before it is touched. Every run is stamped with the git SHA and the clean/dirty
state of the tree. Run `make regress` after any structural change: 62 checks,
19 seconds.

CI runs the same 62 checks on Linux/GCC and builds both ESP32 firmwares from
`sdkconfig.defaults`, asserting the blob and the firmware agree on `RD`. That
job exists because the code had never left macOS/clang, and three portability
bugs were found the first time it did.

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
- **`provenance/needle-upstream.bundle`** is a git bundle of
  [anjaustin/needle](https://github.com/anjaustin/needle), retained for
  provenance under its own terms. This project shares no code with it.
- **`archive/`** is local-only and holds superseded work plus a second
  repository; see [doc/ARCHIVE.md](doc/ARCHIVE.md).
