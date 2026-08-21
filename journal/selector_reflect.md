# REFLECT — every claim interrogated, with the test that was actually run

## N1 — prior margin carries information — CONFIRMED, and strengthened

Dev showed 7.8 vs 24.7 on twenty items, which is worth nothing on its own.
Built `--xval`: 2-fold cross-validation INSIDE the index, rebuilding the centre,
the index vectors and the word prior from the opposite half each time. **1155
IoT items, 183 in disagreement** — 9x the dev slice, and leakage-free.

    mean prior-margin:  router_only 10.4   prior_only 30.6

Same ratio, far more data. N1 holds.

## N6 — "the threshold is fitted to twenty items" — REFUTED

This was the claim most likely to kill the idea, and the reason for building
xval at all. Cross-validated:

    prmarg>= 4  net +37      prmarg>=16  net +48
    prmarg>= 8  net +48      prmarg>=20  net +41
    prmarg>=12  net +51      prmarg>=24  net +39

**The shape is the evidence, not the peak.** A threshold fitted to noise
produces a sharp isolated spike; this is a broad plateau from 8 to 24, every
value of which is a large net gain. The dev-chosen 8 sits inside it. The
threshold is not load-bearing.

## N7 — "never evaluated against negatives" — REFUTED by measurement

The concern was real: unconstrained delegation previously produced fa=824. On
the full dev set, 192 IoT + 1335 negatives, at th=136:

    baseline              recall 85.9%   fa=1   wa=13   missed=14
    selector prmarg>=8    recall 87.5%   fa=1   wa=10   missed=14
    selector prmarg>=12   recall 87.0%   fa=1   wa=11   missed=14

**`fa` unchanged, `missed` unchanged, `wa` down 3.** The reason is structural:
the selector fires only when the router has ALREADY decided the utterance is a
command. It can reassign an operation; it cannot create an actuation.

## N9 — "0 for 3 on this family of intervention" — the base rate held, until it didn't

Soft bias inert, hard cue harmful, class delegation harmful. The difference here
is the gate. Ungated delegation is `prmarg>=0`, and it is measurably worse than
baseline (recall 85.4%, wa 14) — **the same failure as before, reproduced.** The
gate is the whole mechanism, and it is the thing the earlier attempts lacked.

## N2 — "prmarg==0 is abstention, so no threshold is needed" — TOO WEAK

Appealing because it needs no fitted constant. But cross-validation says
`prmarg>=0` nets only +11 while `prmarg>=8` nets +48. The signal is **graded**,
not binary. Abstention is real but is not the whole story; a threshold is doing
genuine work. Recorded as a claim that sounded principled and was wrong.

## N3 — the router cannot tell when it is failing — CONFIRMED

Router margin 11.1 (right) vs 11.6 (wrong); top score runs backwards. This is
why the selector must be driven by the prior's confidence: **only one of the two
channels emits a usable confidence signal.** N4 follows and is forced.

## N8 — high prmarg is confounded with agreement — VALID but not disqualifying

`both correct` has prmarg 45.1, above prior_only's 24.7. So high margin does
partly mean "easy item". But the gate is only consulted when the channels
DISAGREE, and within that slice the separation survives cross-validation. The
confound would matter for a rule applied everywhere; it does not for this one.

## The acceptance test — PASSED on the curve, not at a point

    min MISSED at wrong<=   8     12    16    20    24    28
      baseline             46     23    12     6     5     5
      selector             40     13     8     5     5     5

Dominates or ties at every operating point. This is the first intervention in
this project to pass that test.

## What is still NOT established

- **No held-out measurement.** Everything above is dev plus index CV.
- The 8.4-point oracle gap is only ~1.6 points closed. Most of the
  complementary information is still unreached.
- Not implemented in the firmware; the prior table is a 32768 x 16 int32 array
  (2 MB) as written, which does NOT fit the ESP32. **A device-side design does
  not exist yet.** Every number here is host-side.
