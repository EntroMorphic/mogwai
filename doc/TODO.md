# Open items

Ranked by consequence, not effort. Each names what would close it.

## P1 — limits what can be deployed

### P1-2 · No power measurement
For an always-on listener, mW dominates ms. The cost of a 6.3 ms scan, and the
duty cycle it implies, are both unmeasured. The index is now resident in SRAM
rather than read from flash on every query, which should cut scan energy by more
than the 5.4x latency figure suggests - SPI flash reads dominate. Unmeasured,
and stated as a prediction so it can be wrong.

### P1-3 · One board
ESP32-D0WD-V3 only. The popcount table, the DRAM placement and the QIO result
are specific to LX6 with no SIMD; S3/C6 would likely reorder them.

## P2 — worth doing, nothing blocked on it

### P2-1 · The held-out cost of the 240 KB index is unmeasured
The shipped index was pruned 656 -> 240 KB to fit SRAM entirely. On dev the cost
is `fa` 1 -> 6 and nothing else. Whether that transfers is
[pre-registered](EXPERIMENTS.md#test-evaluation-6--pre-registered-does-the-pruning-cost-transfer)
and **not yet run** - it needs an explicit decision to spend test budget.

**Closes when:** test evaluation #6 runs and its result is recorded beside the
prediction, or the 240 KB index is reverted.

### P2-2 · `.git` is 123 MB for a 16 MB tree
Python-era `.npy`/`.npz` blobs and seven copies of a 5.8 MB
`compile_commands.json` remain in history. Gitignoring stopped the growth; only
a history rewrite removes the weight, which is destructive. This is not
theoretical — it made the first push to GitHub time out.

### P2-3 · No pre-test signal
Dev and index cross-validation both failed to predict a held-out result, and the
near-duplicate explanation was tested and refuted. Every change therefore costs
a budget unit to evaluate. No action known — recorded so it is not rediscovered.

## Closed

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
