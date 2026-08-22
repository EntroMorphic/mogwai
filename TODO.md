# Open items

Ranked by consequence, not effort. Each names what would close it.
Source: the concerns audit in `EXPERIMENTS.md` and `doc/METHOD.md`.

## P0 — unrecoverable, or makes a published claim wrong

_Both P0 items closed 2026-08-22. Remaining work is P1 and below._

### P0-1 · Three commits exist only on one disk
`archive/needle_upstream/` holds both the original `.git` **and** the verified
bundle of the same history — co-located, on a disk at 95% capacity. Three
commits were never pushed (`origin` only ever carried `main`):

    3b8cefd base-3 ternary packing   990978b --kv-window   281baec CLI repair

Co-located redundancy is not redundancy. **This repo's own remote does not fix
it** — `archive/` is gitignored, so pushing mogwai does not carry the bundle.

**CLOSED.** The bundle is now tracked at `provenance/needle-upstream.bundle`
(1.8 MB), so this repo's remote carries it off-disk. It could not be tracked in
place — `archive/needle_upstream/` holds a nested `.git` and git will not add
through one, which is also why a backup should not live inside its own subject. Pushing the branch to `anjaustin/needle` remains the
tidier home for it, but that is a different repository and the owner's call;
the unrecoverable-loss risk is closed either way.

### P0-2 · The core claim has no held-out significance test — CLOSED
Twin-ternary vs binary is **p=0.0931 on dev — not significant at n=192**. The
claim currently rests on the held-out *gap* (19 net items on 220) and on binary
saturating at d=256 vs d=512. Both are sound arguments; neither is the test.
This is now stated in a public README.

**CLOSED** by test evaluation #5: `fixed 25, broke 11, p=0.0288` — significant
on held-out data. README updated to state it alongside the dev non-significance
(p=0.0639), because the effect is real but not large. Budget now 5.

One deviation recorded: the pre-registration said both variants at th=136, but
`make testset` auto-tunes and binary ran at 138. Not re-run — re-running on test
to obtain a preferred framing is what the budget exists to prevent.

## P1 — limits what can be deployed

### P1-1 · No input and no output path
Zero GPIO, UART, or microphone calls in the firmware; all six `route()` call
sites are fed from embedded reference queries. It is a router with nothing to
hear and nothing to actuate. `esp32_router/README.md` says so, but the gap
between "validated result" and "controls hardware" is exactly this.

**Closes when:** a product firmware exists that takes an utterance from
somewhere real and drives a pin.

### P1-2 · No power measurement
For an always-on listener, mW dominates ms. The cost of a 43.5 ms scan, and the
duty cycle it implies, are both unmeasured.

### P1-3 · One board
ESP32-D0WD-V3 only. The popcount table, the DRAM placement and the QIO result
are all specific to LX6 with no SIMD; S3/C6 would likely reorder them.

## P2 — hygiene

### P2-1 · `.git` is 123 MB for a 16 MB tree
Python-era `.npy`/`.npz` blobs remain in history. Gitignoring stopped the
growth; only a history rewrite removes the weight, which is destructive.

### P2-2 · No pre-test signal
Dev and index cross-validation both failed to predict a held-out result, and the
near-duplicate explanation was refuted. Every change therefore costs a budget
unit to evaluate. No action known — recorded so it is not rediscovered.

## Closed

- Curve dominance without significance — `doc/METHOD.md` #11
- Tests that cannot fail — mutation testing, #13 and #15
- Docs quoting stale numbers — self-consistency check, #14
- Archive gitignored but undocumented — `doc/ARCHIVE.md`
