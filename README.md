# Twin-ternary intent router for ESP32

A natural-language interface for controlling ESP32-class hardware, built as an
**integer-only nearest-neighbour router** rather than a language model.

No floats. No learned parameters. No training step. The whole thing is a hashed
character-n-gram encoder, a two-bit-plane vector index, and an integer
similarity — and it runs the full 10,500-entry index on a stock ESP32-D0WD-V3
in **43.5 ms**, bit-identical to the host.

The premise being tested is that **compressing an LLM onto an MCU is not
necessary for this task**. Controlling nine IoT intents does not need 45M
parameters; it needs a good representation and an honest threshold.

## Result

| | recall | unbidden (fa) | wrong action (wa) | missed | size |
|---|---|---|---|---|---|
| binary, 1 bit/dim, d=256 | 80.7% ±2.8 | 0 | 13 | 24 | 328 KB |
| binary, 1 bit/dim, d=512 | 79.7% ±2.9 | — | — | — | 656 KB |
| **twin-ternary, 2 bit/dim, d=256** | **88.0% ±2.3** | **3** | **15** | **8** | **656 KB** |

Dev split, 192 IoT commands and 1335 negatives, at the shipped threshold 126.
The d=512 binary row is the **size-matched** control: at identical bytes per
vector, one bit per dimension is 8.3 points worse, and binary's curve is
*identical* at d=256 and d=512 — it saturates. The gain is structural, not
capacity. See [EXPERIMENTS.md](EXPERIMENTS.md).

**Held-out test: 84.1% ±2.5** — but measured at threshold 136, **not** the
shipped 126. It does not describe the current config. Test budget is deliberately
scarce; see [doc/METHOD.md](doc/METHOD.md).

### Read the error columns, not the recall column

`recall` is `correct_IoT / total_IoT`. **It cannot see false actuations at all** —
firing on a non-command costs it nothing, so it is maximised by never rejecting.
For a system that actuates physical devices, `fa` (fired on something that was
not a command) is the number that matters. It is 3 in 1335 negatives, 0.22%.

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

    c/src/          the entire shipping system (see doc/TOOLS.md for each tool)
    esp32_router/   ESP-IDF firmware; router.c/ternary.c are SYMLINKS into c/src
    doc/            blob format, method/guardrails, tool reference
    EXPERIMENTS.md  the full experimental record, including invalidated results
    FRAME.md        what these numbers do and do not mean — read before quoting any
    journal/        Lincoln Manifold Method artifacts, 5 cycles
    archive/        nothing is deleted; superseded work lives here with autopsies
    results/        every run appends a stamped row; TEST_BUDGET is the audit log
    board_backup/   original ESP-AT image + verified restore procedure

## Standing constraints

- **No Python** anywhere in the pipeline. Corpora are fetched with `curl`.
- **No float** on the hot path; `int32_t` is the widest type.
- **Nothing is deleted**, only archived.
- Every run is logged and stamped with the git SHA and clean/dirty tree state.
