# Open items

Ranked by consequence, not effort. Each names what would close it.

## P1 — limits what can be deployed

### P1-1 · No input and no output path
Zero GPIO, UART, or microphone calls in the firmware; all `route()` call sites
are fed from embedded reference queries. It is a router with nothing to hear and
nothing to actuate. `esp32_router/README.md` says so, but the gap between
"validated result" and "controls ESP32-class hardware" is exactly this.

**Closes when:** a product firmware takes an utterance from somewhere real and
drives a pin.

### P1-2 · No power measurement
For an always-on listener, mW dominates ms. The cost of a 43.5 ms scan, and the
duty cycle it implies, are both unmeasured.

### P1-3 · One board
ESP32-D0WD-V3 only. The popcount table, the DRAM placement and the QIO result
are specific to LX6 with no SIMD; S3/C6 would likely reorder them.

## P2 — worth doing, nothing blocked on it

### P2-1 · Compress the index — the best-supported idea left
The hardware audit closed every offload path at the same 24 MB/s flash wall, so
the only remaining lever is moving fewer bytes. Measured: vectors are 22.6%
dense (57.9 of 256 active). Naive sparse encoding is *larger* (65 B vs 64 B),
but the sign plane stores 256 bits of which only ~58 carry meaning — ~25 B per
vector of provable waste, against an entropy bound near 32 B. **A 2x footprint
cut is theoretically available**, at the cost of the branchless popcount. At 92%
byte-bound that trade may still win. Untested.

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
- Portability: the code only ever built on macOS/clang until CI ran GCC
