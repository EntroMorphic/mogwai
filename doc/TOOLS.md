# C tools in `c/src/`

Everything builds with the Makefile pattern rule: `make c/bin/<name>`.
`CORE` (linked into all of them) is `router.c ternary.c cascade.c invariants.c
prior.c prune.c`.

## Shipping path

| file | role |
|---|---|
| `router.h` | all constants; `RD`, `RSHIP_TH`, blob magic. **Single source of truth.** |
| `router.c` | n-gram hashing, integer encode, polarity cues (`on`/`off`/`up`/`dim`) |
| `ternary.c` | twin-ternary encode, `t_dot`, Dice score, popcount table |
| `prune.c` | index pruning — shared by the harness and the exporter, deliberately |
| `invariants.c` | leak assertions that **abort** |

## Harness

| file | role |
|---|---|
| `compare.c` | the main experiment. `--test` `--curve` `--ship` `--errs` `--fixth=N` `--prune-*` |
| `mkblob.c` | emits `router.bin` for the ESP32. Refuses to guess a threshold when pruning |
| `eval.c` | loads a blob and tunes on held-out validation |

## Diagnostics (kept, not on the shipping path)

| file | role |
|---|---|
| `prior.c` / `prdiag.c` | word-derived lift prior. **Measured inert; cut from the router.** Reachable via `--curve` |
| `cascade.c` | signature cascade. Measured identical on every axis; cut |
| `leakchk.c` | is the dev split optimistically biased? |
| `leaktest.c` | do dev utterances leak back via NLU-Eval? (the 75.6% incident) |
| `probe.c` | diagnoses the 10-point gap |
| `build.c` | older blob builder, superseded by `mkblob.c` |

## Tests

`c/test/t_popcnt.c` — verifies the popcount table against `__builtin_popcount`
**exhaustively, on all 2³² words**, plus the `t_dot` algebraic rewrite over 2M
random vector pairs. Build:

    cc -std=c11 -O2 -DTPOPCNT=1 -o /tmp/tpc c/test/t_popcnt.c c/test/probe.c c/src/router.c

## Compile-time switches

| flag | default | meaning |
|---|---|---|
| `-DRD=N` | 256 | router dimension. Blob and firmware **must** agree |
| `-DTPOPCNT=N` | 1 | 0 = builtin SWAR, 1 = 8-bit table (DRAM), 2 = 16-bit table (64 KB) |
| `-DTSMOOTH=N` | 8 | Dice denominator smoothing. Flat over 2–16 at d=256 |
