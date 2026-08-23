# Oracles 1 & 2, and the rejection boundary

*Measured 2026-08-23. Dev only — no test budget spent. All numbers reproducible
from the flags named; nothing here is an estimate.*

Prompted by reading `../lcvdb/` (a ternary vector DB: 2 bits/dim coarse scan,
then a bounded shortlist rerank in MTF7 at 14 bits/dim). LCVDB solves
**retrieval**, which mogwai does not have as a problem — its argmax is exact
over 3,840 vectors. What transferred was the *shape*: coarse scan, then let a
richer signal decide the ordering. LCVDB showed where to look, not what to use.

---

## The one-line summary

The remaining error splits into **two unrelated mechanisms**, and only one of
them is about ranking:

- **ordering failure** — the right answer is in the shortlist, mis-ordered
- **abstention failure** — the right answer is *already rank 1* and the global
  threshold vetoes it

Two thirds of `missed` is the second kind. No reranker touches it.

---

## Oracle 1 — is a second tier reachable? `--rankoracle`

For every dev error, the rank of the exemplar that would give the right answer:
a correct-class exemplar for `wrong-act`/`missed`, any negative for `UNBIDDEN`.

### At the shipped threshold (th=136)

              K=1    K=2    K=4    K=8   K=16   K=32   K=64  >64/none  total
    wrong-act   0      6      8     11     11     12     12        1     13
    missed      9      9     10     10     11     12     13        1     14
    UNBIDDEN    0      4      6      6      6      6      6        0      6

- **All six false actuations have a negative at rank 2–3.**
- Eleven of thirteen `wa` have the true class within top-8.
- **Nine of fourteen `missed` are rank 1 with gap 0** — argmax already correct,
  killed by the threshold. This was not what we were looking for and is the
  larger finding.

### With the threshold removed (th=0)

              K=1    K=2    K=4    K=8   K=16   K=32   K=64  >64/none  total
    wrong-act   0      6      8     11     12     13     14        1     15
    missed      0      0      1      1      1      2      2        1      3
    UNBIDDEN    4     26     33     37     38     38     38        0     38

**All 38 false actuations have a negative within top-16; 37 within top-8; 26 at
rank 2.** (The four at "K=1" are score ties, not true rank-1 negatives — a
rank-1 negative would already be a correct reject.)

---

## What the threshold is actually buying

Removing it entirely — which *is* contrastive rejection under the coarse metric,
since a positive at rank 1 means best-positive > best-negative by definition:

    th=136    fa= 6  wa=13  missed=14  iot_ok=165   (85.9%)
    th=  0    fa=38  wa=15  missed= 3  iot_ok=174   (90.6%)

**The global threshold buys 32 false-actuation rejections at the price of 9
missed commands.** Stated that way it is obviously a trade worth re-examining
rather than a law.

### The ceiling of the combination

Contrastive rejection **plus a perfect reranker over top-8**:

| | shipped | ceiling |
|---|---|---|
| `fa` | 6 | **1** |
| `wa` | 13 | 4 |
| `missed` | 14 | 2 |
| recall | 85.9% | **96.4%** |

This **exceeds the old channel-selection oracle of 94.3%**, and correctly so:
that oracle bounded one selector architecture over existing channels. A
different decision rule is a different hypothesis space and gets its own bound.

Of the residual, at least two `wa` are corpus defects rather than model error —
see below.

---

## Oracle 2 — does the discarded magnitude do the reordering? `--rerankoracle`

`t_encode` reduces an `int16_t` count to one trit, so a dimension hit once and
hit five times encode identically. The residual it destroys is the distance from
its own decision surface:

    delta_i = acc[i]*RSCALE - centre[i]*total

Fine score: products of those residuals over dims active in both, quantised to
B bits against each vector's own max (MTF7-style adaptive scale), same Dice
denominator. **Ordering only** — accept/reject kept on the coarse score so
`missed` cannot move and any change is attributable to reordering.

    baseline (coarse argmax)        fa= 6  wa=13  missed=14  iot_ok=165
    best of 24 configs (full, K=2)  fa= 6  wa=13  missed=15  iot_ok=164
    worst (4 bits, K=16)            fa=14  wa=24  missed=15  iot_ok=153

**Negative, and monotonically worse with K** — the signature of a metric worse
than the one it is reranking, not of missing bits.

### Why: the sign plane is 99.3% ones

    residual stats over 211,285 active dims of 3,840 index vectors
      delta > 0 (sign bit set): 209,746  (99.3%)
      |delta|  min=1  mean=13630  max=146327

`centre[d]` is the mean rate over **all** index vectors, including the ~77%
where that dimension is zero — but `t_encode` consults it only for dims that
fired, so any firing dimension clears it. The residual therefore has almost no
sign structure, and a metric built on products of near-always-positive
quantities is dominated by magnitude rather than agreement. **It does not nest
the coarse metric**, which is why more of it made things worse.

### The obvious fix is 20 points worse

Condition the mean on firing so the sign splits evenly (`--condcentre`):

    baseline     twin-ternary  85.9% ±2.5   fa=1  wa=13  missed=14  th=136
    condcentre   twin-ternary  65.6% ±3.4   fa=1  wa=19  missed=47  th=130

`t_dot` is `agree - 2*disagree`. A near-constant sign plane means `disagree`
fires only when a dimension is genuinely *below* its diluted mean — which
happens when it fires once inside a long utterance. **That length signal is
worth 20 points.** The asymmetry is load-bearing, not waste. The "structural
flaw" diagnosis was wrong and the experiment said so.

---

## What is established, and what is not

**Established**

- Candidate coverage is not the blocker. The right answer is in the coarse
  top-8 for 37/38 `fa` and 11/13 `wa`.
- Two thirds of `missed` is an abstention problem, not a ranking problem.
- The global threshold trades 9 missed commands for 32 rejections.
- Two specific uses of the discarded magnitude are worse than the status quo.
- The 99.3%-ones sign plane is load-bearing.

**Not established**

- That the magnitude is uninformative. Both negatives share one design fault:
  **neither fine metric strictly refines the coarse one.** A fine score that
  reduces exactly to `t_score` at minimum precision, with extra bits acting only
  as a tie-break, cannot lose to baseline by construction. That experiment has
  not been run and is the honest next step.
- That any of this transfers. **The channel-selection oracle measured 94.3% and
  the selector built to capture it failed held-out and was cut** (test
  evaluation #4). An oracle is an upper bound, not a result.

---

## Two scoreboards, not one

Two of the thirteen `wa` are corpus defects:

    "turn out the lights"    labelled iot_hue_lightUP
                             the router said lightoff, which is CORRECT
    "turn on kitchen light"  labelled iot_wemo_on, rank 19
                             which physical device that is cannot be inferred
                             from the utterance at all

So the target should not be "100% accuracy". A full-information oracle reaching
100% on dev would be **learning the annotator's mistakes**, and should be read
as a warning sign rather than a result. Keep both numbers:

- **corpus accuracy** — against the labels exactly as supplied
- **adjudicated accuracy** — against the meaning, with defects reported, never
  silently corrected

A system that scores 99.x% because it refuses to call *"turn out the lights"* a
light-*up* command is the better system.

---

## Where I would go next, in order

1. **The abstention side, not the ranking side.** It is the larger error class,
   the mechanism is simpler, and it is the same lever as the zero-`fa` goal seen
   from the other end. The measured trade (32 rejections for 9 commands) is the
   thing to attack.
2. **A strictly-refining fine metric** — one that provably reduces to `t_score`.
   Only then does a precision sweep mean anything.
3. **Only then** the flash sidecar: MTF7 at 14 bits/dim × 256 = 448 B/vector =
   1.72 MB flash for 3,840; reading K=16 per query is ~7 KB ≈ 0.3 ms at the
   measured 24 MB/s, about 5% of the current latency, with the SRAM-resident
   coarse tier untouched.

Verifying any of it on held-out data costs a budget unit, of which few remain.

## Reproducing

    c/bin/compare --ship --rankoracle              # oracle 1 at th=136
    c/bin/compare --ship --fixth=0 --rankoracle    # oracle 1 with no threshold
    c/bin/compare --ship --rerankoracle            # oracle 2 + residual stats
    c/bin/compare --condcentre                     # the 20-point negative
    c/bin/compare --ship --curve                   # the threshold curve
