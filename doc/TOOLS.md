# C tools in `c/src/`

    make tools      # builds every tool AND both tests — run this after any
                    # signature change; it is what catches bit-rot

Individually: `make c/bin/<name>`. `CORE`, linked into all of them, is
`router.c ternary.c cascade.c invariants.c prior.c prune.c`.

## Shipping path

| file | role |
|---|---|
| `router.h` | all constants; `RD`, `RSHIP_TH`, blob magic. **Single source of truth.** |
| `router.c` | n-gram hashing, integer encode, polarity cues (`on`/`off`/`up`/`dim`) |
| `ternary.c` | twin-ternary encode, `t_dot`, Dice score, popcount table |
| `prune.c` | index pruning — shared by the harness and the exporter, deliberately |
| `invariants.c` | leak assertions that **abort** |
| `gate.c` | the word prior compacted for the device: 2.13 MB -> 74 KB, bit-exact. Built but NOT shipped — the selector it serves failed held-out |
| `cue.c` | index-derived hard word cues. Built, swept, measured harmful at every lift threshold |

## Harness

| file | role |
|---|---|
| `compare.c` | the main experiment. Every flag is listed by `compare --help`, grouped by whether it still helps — that is the single source of truth, not this table |
| `compare --route=".."` | route ONE utterance and show the nearest stored matches. The fastest way to see what the router actually does |
| `compare --repl` | the same, interactively |
| `compare --help` | every flag, grouped by whether it still helps |
| `mkblob.c` | emits `router.bin`. Refuses to guess a threshold when pruning |
| `eval.c` | **verifies a built blob** before flashing: reference-query parity + index integrity. `c/bin/eval router.bin data/validation.json`. Cannot touch test by construction |

## Device

| file | role |
|---|---|
| `devtalk.c` | talk to the flashed board over serial from a script. `c/bin/devtalk /dev/cu.usbserial-0001 -r -w 6000 -s "turn the lights on" -w 1500` resets, drains the boot log, sends an utterance, prints the reply. Every device number in [EXPERIMENTS.md](EXPERIMENTS.md) is reproducible with it, and no Python is needed to do it |

## Diagnostics (kept, not on the shipping path)

| file | role |
|---|---|
| `prior.c` / `prdiag.c` | word-derived lift prior. **Measured inert; cut from the router.** Reachable via `--curve` |
| `cascade.c` | signature cascade. Measured identical on every axis; cut |
| `leakchk.c` | is the dev split optimistically biased? `... train.json validation.json test.json` — answer: no, 36.3% vs 37.3% |
| `leaktest.c` | reproduces the 75.6% leak on demand — a regression test for a fixed bug. `... train.json nlu_home.csv` |
| `build.c` | older blob builder, superseded by `mkblob.c` |
| `cuemine.c` | mines discriminative words from the INDEX ONLY, replicating compare.c's DEV carve so dev cannot contribute. `cuemine train.json test.json <class> [min_count]` |

`probe.c` was archived to `archive/superseded_tools/` — it compared `t_score`
against a candidate `dice()`, and Dice was then adopted *as* `t_score`, so it
compared a function to itself. It had also bit-rotted behind a changed
signature, which is why `make tools` now exists.

## Tests in `c/test/`

| file | what it proves |
|---|---|
| `t_popcnt.c` | the popcount table equals `__builtin_popcount` on **all 2³² words**, exhaustively — not sampled. Plus the `t_dot` algebraic rewrite over 2M random vector pairs |
| `blobfmt.c` | `router.bin` parses **exactly** per [BLOB_FORMAT.md](BLOB_FORMAT.md), last record ending precisely at EOF. Run `c/bin/blobfmt esp32_router/main/router.bin` — doc drift shows up as unaccounted bytes |
| `blobguard.c` | `r_parse2()` — the parser the **firmware** runs — REFUSES a corrupt blob rather than misreading it. Eleven cases: bad magic, wrong dim, zero and oversized `n_index`, truncation at every scale, non-ascending offsets, unsorted and duplicated exception slices, references past the end. Two of these were live gaps when it was written |

## Compile-time switches

| flag | default | meaning |
|---|---|---|
| `-DRD=N` | 256 | router dimension. Blob and firmware **must** agree |
| `-DTPOPCNT=N` | 1 | 0 = builtin SWAR, 1 = 8-bit table (DRAM), 2 = 16-bit table (64 KB) |
| `-DTSMOOTH=N` | 8 | Dice denominator smoothing. Flat over 2–16 at d=256 |

ESP-IDF takes `RD` and `TPOPCNT` the same way: `idf.py -DRD=256 -DTPOPCNT=1 build`.
**Pass them on every invocation** — `idf.py` caches `-D` variables, and a rebuild
without them silently reuses the previous config.
