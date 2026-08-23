# The sign plane is nearly dead

*Recorded 2026-08-23. Prompted by a suggestion to use Benford's Law to halve
`router.bin` by "removing the 32k table generation". Both halves of that
suggestion were wrong, and chasing them turned up something real.*

## What was suggested, and why it does not work

**Benford's Law.** Benford describes the leading decimal digit of quantities
spanning orders of magnitude under a multiplicative process. The index stores
no numbers — every value is one of three symbols at 2 bits/dim. There is no
leading digit. The skew in natural language that the suggestion was reaching
for is Zipf, not Benford, and exploiting it needs the measured distribution,
not a named law.

**The 32k table.** Two candidates, neither in the blob:

- `prior_t` (`PR_HASH 32768`, 32768x16 int32, 2.13 MB) — the word prior. Cut on
  2026-08-19 in `45e9dd6`. Referenced only by `main.c`, never `product.c`, and
  it has no section in the blob layout.
- `PC16[65536]` — the 16-bit popcount table, only under `TPOPCNT==2`. We ship
  `TPOPCNT=1`, a 256-byte table. It is rodata in flash, not in `router.bin`,
  and removing it is a known latency regression (`scripts/mutate.sh` covers it).

Removing "the 32k table" saves **zero bytes** of the 261,036.

## What is actually in router.bin

| section | bytes | % |
|---|---:|---:|
| header | 20 | 0.01 |
| class names | 512 | 0.20 |
| centre | 1,024 | 0.39 |
| labels | 3,840 | 1.47 |
| `act[]` | 7,680 | 2.94 |
| **index vectors** | **245,760** | **94.15** |
| refs | 2,200 | 0.84 |

Only the vectors matter. Anything that does not shrink them is noise.

## The census

Over all 983,040 dims (3840 vectors x 256), verified by recomputing
`t_active()` from the vector plane and matching the stored `act[]` — 0
mismatches, and no sign bits set outside the mask:

| symbol | count | share |
|---|---:|---:|
| `0` (m=0) | 771,755 | 78.51% |
| `+1` (m=1,s=1) | 209,746 | 21.34% |
| `-1` (m=1,s=0) | **1,539** | **0.16%** |

**32 of every 64 vector bytes encode a bit that is `+1` for 99.27% of active
dims.** Conditional entropy of sign given active is ~0.062 bits: the whole
sign plane carries roughly 1.6 KB of information stored in 120 KB.

This is structural, not a bug. Mask means "differs from centre", sign means
"above centre", and features are sparse non-negative hashed n-gram counts, so
an active dim is nearly always a bucket that got hit. `-1` needs a dim whose
centre is >=1 and an utterance below it. **Twin-ternary is running as de-facto
binary.**

Where the sign-downs live:

- **69 of 256 dims** carry any; the top 10 hold 65.4%
- **`none` holds 1,278 of 1,539 (83%)**; `iot_hue_lightdim` has exactly zero
- 10.7% of vectors are affected; 46 carry >=8, max 32

On the query side (dev): commands 0.772% sign-down rate, negatives **1.262%**.
Predicted by the encoder — `t_encode` thresholds on `centre[i]*total` and
`total` grows with text length, so long text pushes more dims below centre.

## Two routes to a smaller blob

**Entropy-code the vectors.** Order-0 entropy is 0.764 bits/dim, so the
lossless ceiling is 93,901 bytes — a 58.2% file reduction, beating the 50%
target. **It is the wrong trade.** Decode-per-query is 983,040 symbols, serial,
~20 ms even at 20 ns/symbol against a 6.4 ms budget. Decode-at-boot avoids that
but then saves *flash only*, and flash is not the constraint: all 3840 vectors
already sit in SRAM and would still need to. We would break blob-is-the-runtime-
format to optimise the resource we have spare.

**Drop the sign plane.** 1 bit/dim, 32 B/vector, and popcount-in-place is
preserved. Measured with `compare --ship --signplane`.

## The measurement

The binary arm is not a reimplementation. Setting `s := m` on both sides makes
`diff = a.m ^ b.m`, so `(both & diff) == 0` and `t_dot` collapses to
`pc(a.m & b.m)`. The shipped scorer computes the binary case unmodified — no
second Dice, no second `TSMOOTH`, nothing to drift.

Dropping signs removes the `-2*disagree` penalty, so every score rises. The
comparison is threshold-swept; judging arm B at arm A's threshold would be a
strawman (METHOD 19). The control fires: arm A reproduces the product at
`fa=6 wa=13 missed=14 ok=165 th=136`.

| arm | th | fa | wa | missed | ok | errors |
|---|---:|---:|---:|---:|---:|---:|
| A — twin-ternary (shipped) | 136 | 6 | 13 | 14 | 165 | 33 |
| B — sign dropped, best total | 138 | 7 | 13 | 14 | 165 | 34 |
| B — sign dropped, fa matched | 140 | 6 | 13 | 15 | 164 | 34 |

Paired, arm A at 136 vs arm B at 140:

```
decisions that changed at all : 7 of 1527
overall correctness: fixed 3  broke 4  p=1.0000 (two-sided exact) not significant
```

**Blob 261,036 -> 138,156 bytes, 47.1% smaller, for a change indistinguishable
from noise on dev.** It also halves SRAM (245,760 -> 122,880), so residency and
latency should both improve on top.

## What this does NOT establish

- **Dev only.** The test set is sealed; adoption needs a pre-registered test
  eval and costs budget.
- **Arm B's threshold was chosen on the data it is scored on.** Arm A's was
  not — it is the shipped constant. The asymmetry favours B. A clean comparison
  picks B's threshold on a split of dev and scores on the rest.
- **The index was pruned for arm A.** `RSHIP_NEGTOP=2685` selected negatives
  under twin-ternary scoring, so arm B is being handicapped by an index chosen
  for its rival. This is conservative — a re-pruned binary index could do
  better — but it means the 47% is measured, and the accuracy is not final.
- **7 changed decisions on 1527 is thin.** `p=1.0` means we failed to detect a
  difference, not that none exists. Do not report this as "no cost".
- Nothing here has run on hardware. The latency claim is inference from halved
  popcounts and halved memory traffic, not a measurement.

## If we pursue it

The blob format changes, so `mkblob`, `ternary.c`, `product.c`, parity, and
`doc/BLOB_FORMAT.md` all move together, and several regress checks encode
64-byte vectors. It also retires "twin-ternary" as the project's identity —
which is either a finding worth publishing or a thing to avoid, and that is a
call to make deliberately rather than by drifting into it.
