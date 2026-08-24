# Open items

Ranked by consequence, not effort. Each names what would close it.

## P1 — limits what can be deployed

### P1-2 · No power measurement
For an always-on listener, mW dominates ms. The cost of a 4.3 ms scan, and the
duty cycle it implies, are both unmeasured. The index is now resident in SRAM
rather than read from flash on every query, which should cut scan energy by more
than the 5.4x latency figure suggests - SPI flash reads dominate. Unmeasured,
and stated as a prediction so it can be wrong.

A harness now exists: `MOGWAI_POWER=1` holds the board in four defined states
(idle, 100% duty, 1 Hz, 10 Hz) for 30 s each with the serial line silent and the
actuator pins parked, so an inline USB meter can be read against each one. See
[../esp32_router/README.md](../esp32_router/README.md). What is still missing is
the readings, which need a meter on the USB line - the ESP32 has no current
sensor and no amount of firmware will produce one.
### P1-3 · One board
ESP32-D0WD-V3 only. The popcount table, the DRAM placement and the QIO result
are specific to LX6 with no SIMD; S3/C6 would likely reorder them.

## P2 — worth doing, nothing blocked on it

### P2-2 · `.git` is large, and will stay that way — WON'T FIX
Python-era `.npy`/`.npz` blobs and seven copies of a 5.8 MB
`compile_commands.json` remain in history. Gitignoring stopped the growth; only
a history rewrite removes the weight. This is not theoretical — it made the
first push to GitHub time out.

**Closed as won't-fix, for a reason stronger than "rewriting is destructive".**
`results/RESULTS.tsv` stamps every run with the git SHA and the clean/dirty
state of the tree — 19 rows so far — and `journal/` and `doc/EXPERIMENTS.md`
cite commits by SHA. A history rewrite changes every SHA in the repository, so
it would **orphan the entire experimental record**: every stamped row would
point at a commit that no longer exists. The provenance discipline that makes
the results checkable is exactly what makes the history unrewritable. That is a
trade this project already made, knowingly, and it is the right side of it.

What *was* done instead: `git gc --prune=now` packed 29.3 MB of loose objects
and took `.git` from **126 MB to 85 MB** — a third of it, non-destructively,
with every SHA intact. And the one stale tracked artefact (`c/router.bin`, an 830 KB `dim=512` blob from the first
C commit that nothing referenced) is archived and untracked. Neither shrinks
history, and neither is claimed to.

### P2-3 · No pre-test signal
Dev and index cross-validation both failed to predict a held-out result, and the
near-duplicate explanation was tested and refuted. Every change therefore costs
a budget unit to evaluate. No action known — recorded so it is not rediscovered.

## Closed
- **P2-1 · The held-out cost of the 240 KB index.** Test evaluation #6, budget
  entry 8: `recall 84.1% ±2.5, fa 12, wa 15, missed 20` against the unpruned
  baseline's `84.1%, fa 8, wa 15, missed 20`. Four of four pre-registered
  predictions held, no falsifier fired, `missed` and `wa` bit-identical. The
  240 KB index stands.

- **P0-1 · Three commits existed only on one disk.** The upstream `.git` and its
  own bundle were co-located on a disk at 95% capacity, and three commits were
  never pushed. Now tracked at `provenance/needle-upstream.bundle`, so this
  repo's remote carries them off-disk; verified by cloning from the tracked copy.
- **P0-2 · The core claim had no held-out significance test.** Test evaluation
  #5: `fixed 25, broke 11, p=0.0288` — significant. Stated in the README
  alongside the dev non-significance (p=0.0639), because the effect is real but
  not large.
- Curve dominance without significance — `doc/METHOD.md` #11
- Tests that cannot fail — mutation testing, #13 and #15
- Docs quoting stale numbers — self-consistency check, #14
- **P2-1 (old) · Compress the index.** Packing the sign plane is measured:
  bit-exact over 5,376,000 comparisons and 1.61x smaller, but **4.8x slower**
  against a break-even of 1.62x - 6x over budget, and worse on an in-order LX6
  than on the host that measured it. The footprint cut was taken by *pruning*
  instead: 656 -> 240 KB, which is what made the index fully SRAM-resident.
- **P1-1 · No input and no output path.** `esp32_router/main/product.c` takes an
  utterance over UART and drives GPIO/LEDC. Nothing actuates unless the router
  accepts, and a non-command match is refused separately from a below-threshold
  score.
- Portability: the code only ever built on macOS/clang until CI ran GCC
