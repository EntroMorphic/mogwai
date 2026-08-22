# mogwai — twin-ternary intent router for ESP32

[![regress](https://github.com/EntroMorphic/mogwai/actions/workflows/regress.yml/badge.svg)](https://github.com/EntroMorphic/mogwai/actions/workflows/regress.yml)

CI runs the 55-check host suite on Linux/GCC and builds the ESP32 firmware from
`sdkconfig.defaults`, asserting the blob and firmware agree on `RD`.

A natural-language interface for controlling ESP32-class hardware, built as an
**integer-only nearest-neighbour router** rather than a language model.

No floats. No learned parameters. No training step. The whole thing is a hashed
character-n-gram encoder, a two-bit-plane vector index, and an integer
similarity — and it runs the full 10,500-entry index on a stock ESP32-D0WD-V3
in **43.5 ms**, bit-identical to the host.

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

    make demo                                    # 60-second tour
    make route TEXT="turn off the kitchen light" # route one utterance, see why
    make repl                                    # interactively

Two host dependencies: a C compiler and `curl`. Build is 0.8 s, a full
evaluation 0.7 s, the entire 48-check regression suite 11 s.
Full path in [doc/QUICKSTART.md](doc/QUICKSTART.md).

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

## Result

**Held-out test set** — 220 IoT commands, 2754 negatives, threshold 136:

| | recall | unbidden (fa) | wrong action (wa) | missed |
|---|---|---|---|---|
| **twin-ternary, d=256** | **84.1% ±2.5** | **8** (0.29%) | **15** | **20** |

Two bits per dimension beats one, and the comparison is **size-matched** — binary
at d=512 spends the same 64 bytes per vector:

| | recall | fa | wa | missed | size |
|---|---|---|---|---|---|
| binary, 1 bit/dim, d=256 | 80.7% ±2.8 | 1 | 13 | 24 | 328 KB |
| binary, 1 bit/dim, d=512 | 79.7% ±2.9 | 0 | 15 | 24 | 656 KB |
| **twin-ternary, 2 bit/dim, d=256** | **85.9% ±2.5** | **1** | **13** | **14** | **656 KB** |

At identical bytes per vector, twin recalls **6.2 points more** and misses **10
fewer** commands. Binary's operating curve is *identical* at d=256 and d=512 — it
saturates, so the gain is structural, not capacity. Paired test on held-out data:
`fixed 25, broke 11, `**`p = 0.0288`**.

**Read `fa`, not recall.** Recall is `correct_IoT / total_IoT` — it cannot see
false actuations at all, so it is maximised by never rejecting. For anything that
drives a relay, `fa` is the number that matters: **8 in 2754 negatives, 0.29%**.
Twin buys recall and pays a little precision; binary is the more conservative
router. Which you want is a deployment choice, and the threshold is the knob —
the full operating curve is in [doc/EXPERIMENTS.md](doc/EXPERIMENTS.md).

## On hardware

    ESP32-D0WD-V3, 240 MHz, QIO flash @ 80 MHz, stock ESP-IDF v5.5

    index scan, 10500 vectors / 656 KB    43.5 ms  (1 core)   26.7 ms  (2 cores)
    parity vs host                        64/64 class and score, bit-exact

Optimisation history: 200.4 → 102.1 (clocks) → 78.8 (precomputed activity
counts) → 43.5 ms (popcount table). Every step held bit-exact parity. The
second core is real but **core 1 belongs to WiFi on a production device**, so
43.5 ms is the number that survives deployment.

Cost model, fitted on three dimensions: **59.8 ns/byte + 326 ns/vector**. Bytes
are 92% of the cost, so index size predicts latency directly.

## Quickstart

    make fetch          # curl the corpora (MASSIVE + NLU-Evaluation-Data), records SHA256
    make compare        # dev/validation evaluation — safe to run as often as you like
    make testset        # HELD-OUT TEST. Burns one budget unit. Deliberately not `make test`.
    make tools          # build every tool and test — run after any signature change
    make regress        # full host regression (55 checks) — run after any structural change

Build and flash the device:

    make c/bin/mkblob
    ./c/bin/mkblob data/train.json data/validation.json data/test.json \
                   data/nlu_home.csv esp32_router/main/router.bin
    cd esp32_router && idf.py -DTPOPCNT=1 -DRD=256 build flash monitor

The firmware self-checks against 64 host-computed reference queries embedded in
the blob and prints `PARITY EXACT` only if class **and** score match on all 64.
The layout and what parity does *not* cover: [doc/BLOB_FORMAT.md](doc/BLOB_FORMAT.md).

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

## Repo map

    README.md          you are here
    Makefile           every entry point: fetch, demo, route, ship, regress, testset
    c/src/             the entire shipping system — 8 tools + 7 shared units
    c/test/            an exhaustive 2^32 popcount proof, a blob-format validator
    esp32_router/      VALIDATION firmware (see its README); sources are SYMLINKS into c/src
    doc/               QUICKSTART, EXPERIMENTS, METHOD, TOOLS, BLOB_FORMAT, FRAME, ARCHIVE, TODO
    journal/           Lincoln Manifold Method artifacts, 7 cycles
    scripts/           fetch.sh (curl only), regress.sh (55 checks)
    results/           every run appends a stamped row; TEST_BUDGET is the audit log
    provenance/        the only off-disk copy of three never-pushed upstream commits
    board_backup/      how to restore the board's original ESP-AT firmware
    data/              fetched, never vendored — only SHA256 is tracked
    archive/           superseded work + provenance. LOCAL ONLY (gitignored);
                       inventory in doc/ARCHIVE.md — nothing is ever deleted

## Standing constraints

Small, capable, and governed by three absolute prohibitions. Break them and you
get something much worse — which is the whole reason for the name.

1. **No Python** anywhere in the pipeline. Corpora are fetched with `curl`.
   Not in the product, not on the hot path, not as glue, not as a "quick"
   experiment harness that quietly becomes the deliverable.
2. **No float** on the hot path. `int32_t` is the widest type. No `sqrt`, no
   division that is not integer, no learned parameters anywhere.
3. **Nothing is deleted, only archived.** Superseded work, negative results and
   the autopsies of both are kept — see [doc/ARCHIVE.md](doc/ARCHIVE.md).

Every run is logged and stamped with the git SHA and clean/dirty tree state.
Run `make regress` after any structural change: 55 checks, 11 seconds.

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
