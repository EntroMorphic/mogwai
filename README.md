# mogwai — twin-ternary intent router for ESP32

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
> anyone. Read [FRAME.md](FRAME.md) before quoting any number from this repo.

## What is unusual here

Most small-model repositories publish what worked. This one publishes the
operating curve, the size-matched control, the paired significance test, and
every architecture that was built, measured and cut — including one that
dominated the dev curve and still failed on held-out data.

- The first held-out evaluation is marked **VOID** and the budget stayed spent,
  because the config it measured had been chosen on a leaked split.
- The shipped threshold was moved to 126 on dev evidence and then **reverted by
  its own pre-registered falsifier** when held-out data disagreed.
- The headline representation claim carries its p-value **in the README**
  (p=0.0931 on dev, not significant) rather than in a footnote.
- `scripts/regress.sh` is mutation-tested: every check has been verified to fail
  when the thing it guards is deliberately broken. Two rounds of that found four
  checks that could never fail.

[EXPERIMENTS.md](EXPERIMENTS.md) is a lab notebook, not a highlight reel.
[doc/METHOD.md](doc/METHOD.md) is fifteen guardrails, each named for the
incident that produced it.

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
evaluation 0.7 s, the entire regression suite 11 s.
Full path in [QUICKSTART.md](QUICKSTART.md).

## Result

**Held-out test set, at the shipped threshold** — 220 IoT commands, 2754 negatives:

| | recall | unbidden (fa) | wrong action (wa) | missed |
|---|---|---|---|---|
| **twin-ternary, d=256, th=136** | **84.1% ±2.5** | **8** (0.29%) | **15** | **20** |

Dev split (192 IoT, 1335 negatives), same threshold, with the size-matched control:

| | recall | fa | wa | missed | size |
|---|---|---|---|---|---|
| binary, 1 bit/dim, d=256 | 80.7% ±2.8 | 1 | 13 | 24 | 328 KB |
| binary, 1 bit/dim, d=512 | 79.7% ±2.9 | 0 | 15 | 24 | 656 KB |
| **twin-ternary, 2 bit/dim, d=256** | **85.9% ±2.5** | **1** | **13** | **14** | **656 KB** |

The d=512 binary row is the **size-matched** control — the honest comparison,
since twin-ternary spends two bits per dimension. At identical bytes per vector
twin recalls 6.2 points more and misses 10 fewer commands, and binary's operating
curve is *identical* at d=256 and d=512: it saturates, so the gain is structural
rather than capacity.

**But binary is the more conservative router** and the table is not a clean
sweep: at matched bytes it fires on **zero** non-commands where twin fires on
one. Twin buys recall and pays in unbidden actuations. Which side you want is a
deployment decision — see [EXPERIMENTS.md](EXPERIMENTS.md).

**Where the claim rests, stated precisely.** On dev the paired test is
`fixed 16, broke 7, p=0.0931` — **not significant at n=192**. The claim rests on
the held-out gap (84.1% vs 75.5%, 19 net items on 220) and on binary saturating
— its curve is identical at d=256 and d=512 — not on dev significance, which it
does not have.

### Why 136 and not 126

126 was chosen on dev, where it looked clearly better (85.9 → 88.0% recall,
missed 14 → 8). A held-out measurement then showed the gain did not transfer:
**+3 commands recognised for +5 unbidden actuations**, and the recall comparison
is paired and one-directional, so b=3 / c=0, **exact p=0.25 — not significant**.
An unbidden rate that nearly doubles (0.29% → 0.47%) to buy a recall gain
indistinguishable from zero is the wrong trade for hardware that actuates.
Reverted per a falsifier pre-registered before the run.

### Read the error columns, not the recall column

`recall` is `correct_IoT / total_IoT`. **It cannot see false actuations at all** —
firing on a non-command costs it nothing, so it is maximised by never rejecting.
For a system that actuates physical devices, `fa` (fired on something that was
not a command) is the number that matters. On the held-out set it is **8 in 2754
negatives, 0.29%**.

That distinction is not academic here. Moving the threshold from 136 to 126 gains
2.1 points of dev recall and nearly doubles the unbidden rate — and the recall
gain does not survive a held-out check. Recall alone would have endorsed it.

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
    make regress        # full host regression (43 checks) — run after any structural change

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
    c/test/         exhaustive popcount proof, blob-format validator
    esp32_router/   VALIDATION firmware (see its README) — parity + benchmarks,
                    not a product build; sources are SYMLINKS into c/src
    QUICKSTART.md   60 seconds to a routed sentence
    TODO.md         open items, ranked by consequence
    doc/            blob format, method/guardrails, tool reference, archive inventory
    EXPERIMENTS.md  the full experimental record, including invalidated results
    FRAME.md        what these numbers do and do not mean — read before quoting any
    journal/        Lincoln Manifold Method artifacts, 5 cycles
    archive/        superseded work + provenance. LOCAL ONLY (gitignored);
                    inventory in doc/ARCHIVE.md — nothing is ever deleted
    results/        every run appends a stamped row; TEST_BUDGET is the audit log
    board_backup/   original ESP-AT image + verified restore procedure

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
Run `make regress` after any structural change: 43 checks, including that the
negative results still reproduce.
