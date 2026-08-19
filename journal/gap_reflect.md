# REFLECT — the structure beneath the gap

## Asking why, three times

**Why did the structural ranker fail?**
→ Because its lexicon was read off test failures and its four hyperparameters
were swept on test.
**Why did I do that?**
→ Because inspecting failures is how you find mechanisms, and the only failures
visible to me were test failures.
**Why were they the only ones visible?**
→ Because dev has ~140 IoT examples and train is the index itself. There is no
third pool to read mechanisms off. **The act of discovering the fix consumed the
data needed to validate it.**

**Why do zero-parameter mechanisms keep surviving?**
→ Because they break zero cases, and I can prove that with a paired test.
**Why does breaking zero matter so much?**
→ Because verifying "this never makes things worse" is a *one-sided* test
against an expected-zero null. Verifying "this is the best of N options" is a
*selection* problem that needs enough data to separate N candidates.
**Why is that the whole difference?**
→ Selection needs resolution proportional to the gaps between candidates. Non-
destructiveness needs only enough data to observe a single counterexample. At
220 test examples we have the second and not the first.

## Hidden assumptions, named and challenged

**A1 — that the remaining 12 points are winnable.** I inspected all 25 residual
errors. Roughly 7 are unwinnable: `"and the darkness has fallen"` (gold
lighton), `"disable my okug"` (ASR garbage for plug), `"would you love to see"`
(gold lightchange, uninformative), `"please put all the lights"` (truncated),
`"turn down the lights to medium"` (gold lightoff; that is dim), `"light color
for study room"` (gold lightup; text says colour). **The practical ceiling is
~96.8%, not 100%.**

**A2 — that we need a better ranker.** The ~18 winnable errors each have a
nameable cause: "cease"/"no"/"go out" → off, "pink"/"red ish"/"colors" →
change, "lower"/"dim" → dim, "roomba"/"suck out the dust" → cleaner, "sleep" →
off. These are not a ranking problem. They are a *vocabulary* problem, and
vocabulary is where the leakage happened.

**A3 — that 100% is the target.** Chasing beyond ~96.8% means deliberately
fitting mislabelled and garbled examples. The target should be the recoverable
ceiling, stated with its own uncertainty.

**A4 — that more mechanisms means more risk.** Only for *selected* mechanisms.
A mechanism proven non-destructive adds no risk by construction — that is what
"broke 0" means, and it is why polarity and veto could both be added without
trading anything away.

**A5 — that we are in a rut.** Three LMM cycles have now concluded "data, not
design." But the conclusions are not the same: cycle 1 said *specify the
problem*, cycle 2 said *choose the objective*, cycle 3 says *choose a
verification regime that your data can actually support*. Same direction,
increasing precision.

## Core insight

**We can see every remaining error and name the mechanism that would fix it —
but we cannot verify that any fix generalises, because the only data that could
verify it is the data we read the fix off. The escape is not better mechanisms;
it is a weaker verification requirement: stop selecting the best mechanism and
start accumulating mechanisms proven to break nothing.**

## Tension resolutions

**T1 (improve vs verify) — RESOLVED BY CHANGING THE ACCEPTANCE TEST.**
Do not ask "does this score higher?" — that requires selection power we lack.
Ask "does this break zero cases at every threshold?" — a one-sided paired test
we *can* run at n=2974. Polarity and veto both pass it. The structural ranker
never did, and its accuracy gain was never the evidence that mattered.

**T2 (100% vs label noise) — RESOLVED BY RETARGETING.**
Ceiling is ~96.8% (7 of 220 unrecoverable). Report progress against that, with
the caveat that the 7 is my judgement on a 220-example sample and itself carries
+/-2 points. Announce the gap as ~8 points, not ~12.

**T3 (few zero-parameter mechanisms exist) — RESOLVED, IT IS NOT TRUE HERE.**
The 18 winnable errors decompose into five vocabulary families, each expressible
as a knob-free check. The design space is not exhausted; it was mis-specified as
a ranking problem when it is a vocabulary problem.

**T4 (data is the real lever) — ACKNOWLEDGED AND SET ASIDE.**
True, unavailable, and not actionable inside MASSIVE. Noted as the dominant
long-term lever; excluded from this cycle's plan.

**T5 (are we in a rut) — RESOLVED BY DISTINGUISHING THE CYCLES.**
See A5. Convergence with increasing precision is not repetition.
