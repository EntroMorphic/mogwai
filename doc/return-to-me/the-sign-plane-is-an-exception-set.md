# The sign plane is an exception set, not a bit-plane

*Recorded 2026-08-23, revised the same day. Prompted by a suggestion to use Benford's Law to halve
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

## The sign plane is load-bearing — which we already knew

Before proposing to delete it, read `doc/EXPERIMENTS.md`. `--condcentre`
conditioned the centre on firing so the signs would split evenly — the obvious
fix for an "almost constant, therefore wasted" plane:

    baseline      twin-ternary  85.9% +-2.5   fa=1  wa=13  missed=14  th=136
    condcentre    twin-ternary  65.6% +-3.4   fa=1  wa=19  missed=47  th=130

**Twenty points worse.** And the doc already recorded the entropy: *"The 32
bytes of `s[]` hold ~0.06 bits per active dim and are worth 20 points anyway."*
That matches the 0.062 bits measured here independently.

The mechanism: `t_dot` is `agree - 2*disagree`, and with a near-constant sign
plane `disagree` fires only when a dim sits below its diluted mean — which
happens when it fires once inside a long utterance. **It is a length signal**,
and long utterances are exactly the negatives. That is why 83% of the
exceptions are in `none`.

So "99.27% positive, delete it" is precisely the reasoning METHOD 20 exists to
stop. The information is not 122,880 arbitrary sign bits. It is **1,539
exceptions to a rule that holds 99.84% of the time.** Represent it as that.

## Three routes, and the right one

**Entropy-code the vectors.** Order-0 entropy 0.764 bits/dim, ceiling 93,901
bytes (58.2% off). Wrong trade: decode-per-query is 983,040 symbols, ~20 ms
against a 6.4 ms budget; decode-at-boot saves *flash only*, and flash is not
the constraint. Breaks blob-is-the-runtime-format to optimise the resource we
have spare.

**Drop the sign plane (lossy).** Measured with `--signplane`. 47.1% off, and on
dev the change is not detectable — arm A `fa 6 wa 13 missed 14`, arm B at
matched fa `fa 6 wa 13 missed 15`, paired 7 of 1527 decisions moved, fixed 3
broke 4, `p=1.0000`. **But `p=1.0` is a failure to detect, not an absence**, and
`--condcentre` says this plane can be worth twenty points under a different
perturbation. Retained as a measurement; not a proposal.

**Store the exceptions (lossless).** The right answer. Keep the 32-byte active
mask, make `+1` implicit, and store a stream of `uint8` dimension positions
where the sign is below centre. `RD=256`, so a position is exactly one byte.

### The algebra is exact

With `s = m & ~E`, and sign bits clean outside the mask (verified: zero words
with `s & ~m`), inside `both` we have `q.s = ~Eq` and `b.s = ~Eb`, so `diff`
there is `Eq ^ Eb`:

    disagree = |Eq & bm| + |Eb & qm| - 2*|Eq & Eb|

An identity, for every input — not an approximation. Eight popcounts instead of
sixteen, plus bit tests on sets that are empty for 89.3% of vectors. Score
identity follows by construction: `t_score` is a pure function of
`(dot, aa, ab)`, and `aa`/`ab` read only the mask, preserved byte-for-byte.

### It is verified, including the branch that could have hidden

`compare --ship --exstream`:

    pairs compared (DEV x index) : 5,863,680
    dot mismatches               : 0

    pairs with BOTH sides having exceptions : 94,710
    pairs with |Eq & Eb| > 0                : 54,683  (80,428 overlaps)

    randomised dense/sign-balanced pairs    : 200,000
    of which |Ea & Eb| > 0                  : 177,943 (89.0%)
    dot mismatches                          : 0

The second block matters: if dev never made `|Eq & Eb|` nonzero, the
`-2*inter` correction would be an unexercised branch and "0 mismatches" would
prove nothing about it. It fires 54,683 times on real data, and the randomised
arm hammers the dense, evenly-signed corners real data never reaches.

### The size

| layout | vector payload | vs today |
|---|---:|---:|
| today, `m` + `s` planes | 245,760 B | — |
| masks only | 122,880 B | |
| + 1 count byte/vector + positions | **128,259 B** | **47.8% smaller** |
| + `uint16` offset table + positions | 132,101 B | 46.2% smaller, *random access* |

Whole file 261,036 -> 143,535 B (45.0%) sequential, or 147,377 B (43.5%) with
random access. Mean **34.4 bytes per vector**, down from 64.

Prefer the offset-table variant unless profiling says otherwise: the sequential
form requires every consumer to walk vectors in order, and `--rankoracle`,
`prune`, and the chunked SRAM lift all index `TI[j]` directly. 3.8 KB is a
cheap price for keeping random access.

### Why this costs no test budget

It is a storage-layout change, not a model change. Routing is bit-identical by
construction and by measurement, so every number ever recorded on test remains
exactly valid. Nothing new is consulted and no decision can move. The device
parity check is the natural place to enforce it permanently.

### The real prize is SRAM, not flash

We have been fighting to keep 245,760 B resident alongside WiFi and TLS, and
the networked build sits at 83% after reclaiming WiFi IRAM. At ~132 KB the
economics change: full residency alongside the network stack stops being tight.
Fewer loads and half the popcounts should cut latency too — but that is
inference from the operation count, **not measured**, and it goes on hardware
before it goes in a README.

## What is still open

- **Nothing has run on hardware.** The latency and residency claims are
  arithmetic, not measurements.
- **Variable-length records touch a lot of code.** `mkblob`, `ternary.c`,
  `product.c`, `lift.c`, parity, `doc/BLOB_FORMAT.md`, and several regress
  checks encode 64-byte vectors. The offset-table variant contains the damage.
- **`--exstream` is a feasibility proof, not a guard.** If the format is
  adopted, the bit-identity test belongs in the suite and on the device.
- **The exception count is data-dependent.** 1,539 is a property of this corpus
  and this prune. A different corpus, a different `RSHIP_NEGTOP`, or a
  re-centred encoder moves it. The format degrades gracefully — worst case is
  `mask + count + all-active-positions` — but the 34.4 B/vector figure is not a
  constant of nature.
- **`RD <= 256` is load-bearing** for one-byte positions. Static-asserted in
  `exstream()`; it would need widening at `RD=512`.

## Verdict on the original suggestion

**Wrong law, right instinct.** Benford does not describe a three-symbol
alphabet; Zipf is closer to the language skew being reached for, and neither is
needed. Measuring the actual distribution told us the coding model, and the
data turned out to be a sparse binary mask with a tiny exception set. The "50%"
was very nearly right, for a completely different reason.
