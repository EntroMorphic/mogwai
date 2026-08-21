# SYNTHESIZE — what to actually do

## The finding

A selector works, and the mechanism is a gate, not a blend.

    baseline           recall 85.9%   fa=1   wa=13   missed=14
    selector prmarg>=8 recall 87.5%   fa=1   wa=10   missed=14

Dominant on the full operating curve. Three previous attempts to use the word
channel failed because they blended it in everywhere. This one asks a prior
question first — **does the prior have an opinion worth hearing?** — and defers
to the router when the answer is no.

## Why it works when the others did not

1. **Only one channel has a usable confidence.** The router's margin does not
   predict its own correctness (11.1 right vs 11.6 wrong). The prior's does
   (10.4 vs 30.6 cross-validated). A selector must therefore be driven by the
   prior's self-assessment — that is forced by the measurements, not chosen.
2. **The gate is the mechanism.** Ungated (`prmarg>=0`) reproduces the earlier
   failure exactly. Gated, it dominates. The earlier attempts were not wrong to
   try the word channel; they were wrong to consult it unconditionally.
3. **It cannot create an actuation.** It fires only after the router has
   accepted, so `fa` is structurally untouchable by it. That is what makes it
   safe for something that actuates hardware.

This is the-reflex architecture arriving at its own conclusion: prior as a
voice, not a veto, with a disagreement detector that can actually fire.

## What to do next, in order

1. **Do not ship it yet.** It is dev + index-CV only. The 126 threshold looked
   this good on dev and did not transfer.
2. **Solve the device problem first — and it is already solved on paper.**
   Measured: `prior_t` is **2.13 MB** (w_cls 32768x16 int32 = 2.00 MB, w_tot
   0.12 MB) against a 4 MB flash that already carries a 0.64 MB index. But the
   router never reads the count matrix at inference — `pr_vote` needs only the
   argmax class and the margin. Storing those directly:

       32768 buckets x (int8 class + int16 margin) = 96 KB   -- 23x smaller

   That fits comfortably. It is also strictly better than shrinking PR_HASH,
   which would add collisions. Build the reduced table in `mkblob`, verify
   host-device parity on it, and the device problem is closed.
3. **Then pre-register and spend budget unit #4.** Predict the held-out
   `wa` and `fa` before running. The falsifier should be: if `wa` does not
   improve on held-out data, the gate is dev-specific and the selector is cut.
4. **Only then reconsider the threshold.** A better classifier may move the
   whole operating curve, which could reopen the 136-vs-126 question that was
   settled against a worse classifier.

## What this does not solve

The oracle bound is 94.3%; this reaches 87.5% on dev. **Roughly 1.6 of the 8.4
available points.** The remaining gap is still real, still unreached, and now
has a demonstrated method for attacking it: find a signal that predicts which
channel is right, gate on it, and never let the second channel manufacture an
actuation.

---

## OUTCOME (appended after test evaluation #4)

**The selector failed on held-out data and was cut.**

    baseline   recall 84.1%   fa=8   wa=15   missed=20
    selector   recall 82.7%   fa=8   wa=18   missed=20

`wa` got worse. The falsifier written above fired, and the selector was cut
rather than re-tuned.

**Both structural predictions held exactly** — `fa` 8 and `missed` 20, unchanged.
So the mechanism was precisely as described in this document: the gate fires only
after the router accepts, and cannot manufacture an actuation. It was right about
*what it does* and wrong about *whether that helps*.

**What this synthesis got wrong, and why it is worth keeping:**

1. It called the dev result dominant without testing significance. Paired
   McNemar afterwards: fixed 6, broke 3, **p = 0.508**. The "first intervention
   to dominate the curve" was noise that happened to dominate.
2. It treated index cross-validation (+48 of 183) as evidence the signal was
   real. It was the strongest pre-test evidence available and it was wrong. The
   near-duplicate explanation was tested and refuted; no confirmed cause exists.
3. It was right that only one channel emits a usable confidence, right that
   ungated delegation reproduces the earlier failure, and right that the device
   blocker was solvable (2.13 MB → 74 KB, bit-exact).

**The reasoning was sound and the conclusion was wrong.** That is the value of
keeping the artifact: the method reached a plausible, well-argued, testable
claim, and the test killed it. A cycle that produces a falsifiable claim which
then falsifies is working, not failing.

**Still open:** the 8.4-point oracle gap is untouched. What is now known is that
a margin-gated selector does not reach it, and that dev + index CV cannot tell
you whether a selector works.
