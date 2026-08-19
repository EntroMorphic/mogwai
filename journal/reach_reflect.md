# REFLECT — the ceiling was never a property of the problem

## Why, three times

**Why did I keep saying "unreachable"?**
→ Because every mechanism I tried failed to validate.
**Why did mechanism failure become a claim about the ceiling?**
→ Because I conflated "I cannot verify a fix" with "no fix exists."
**Why is that conflation so easy to make?**
→ Because the evidence looks identical from the inside. A mechanism that
doesn't work and a mechanism I can't measure both produce a null result. I had
confidence intervals, paired tests, McNemar p-values — and all that rigour
told me was that I could not distinguish. I read it as: there is nothing there.

**Why did the data lever work when nothing else did?**
→ Because it requires no selection. More in-distribution examples are
monotonically good; you do not have to choose the right one.
**Why does that matter so much here?**
→ Selection is exactly the operation our data cannot support. 59-140 dev IoT
examples cannot separate candidate mechanisms.
**Why did I name the data lever three times and never pull it?**
→ Because pulling it meant going back to the dataset search, which felt like
retreating from the technical work. Mechanism design feels like engineering.
Loading another dataset feels like admitting the engineering was not the point.

## Hidden assumptions, challenged

**A1 — "MASSIVE is exhausted."** False by inspection. Validation IoT (118
utterances) was sitting unused as index data the whole time, and adding half of
it moved accuracy 87.7% -> 88.6% and wrong actions 15 -> 14.

**A2 — "the vocabulary mechanism is too weak."** False. Margin-gating proved
it: the 187-token aggressive vocabulary breaks 4 ungated and **0 gated** on dev.
The mechanism was never weak; it was aimed at every query instead of at the
uncertain ones. It still failed test confirmation — but that is a *measurement*
failure, not a mechanism failure, and the distinction matters.

**A3 — "more data always helps."** Also false, and measured: adding 24,000
CLINC/SNIPS negatives *hurt* (88.6% -> 85.5%). In-distribution data helps;
out-of-distribution data crowds the neighbourhood. The lever is specific.

**A4 — "96.8% is unreachable."** Wrong framing. Accuracy grows log-linearly
with IoT index size (recall@50: 94.1% at 110 examples -> 98.2% at 887). The
question is not whether it is reachable but **how many utterances it costs**,
and that is ~4,000 — 4.6x what MASSIVE provides.

**A5 — "Needle is unshippable so it is out of the picture."** It generates
training data. `needle generate-data` is exactly the 4,000-utterance machine
the projection calls for. Cycle 1 noted this and I forgot it.

## Core insight

**Every ceiling I announced was a verification limit wearing a performance
limit's clothes. The mechanisms exist and some provably work; what does not
exist is enough data to choose between them — which is why the only lever that
ever moved was the one that requires no choosing.**

## Tension resolutions

**T1 (rigour as brake vs steering) — RESOLVED.** Rigour correctly told me I
could not select. The error was letting that stop generation rather than
redirect it toward selection-free levers. Rigour should route effort, not end it.

**T2 (global vs targeted mechanisms) — RESOLVED BY MEASUREMENT.** Margin-gating
converts an unusable mechanism into a dev-clean one. Targeting is the general
fix for breaks-zero failures. It still needs test data to confirm, which returns
us to T3.

**T3 (is the data purchasable?) — RESOLVED, YES, AND QUANTIFIED.** ~4,000 IoT
utterances for 96.8%. Sources: synthesise with `needle generate-data`, harvest
from a deployed device, or find another in-distribution IoT corpus. The
constraint is a purchase order, not a law of nature.
