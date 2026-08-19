# REFLECT — the structure beneath the findings

## Why, three times

**Why did twenty experiments produce one significant result?**
→ Because the effects we were chasing (2–4 points) are smaller than the
resolution of the instrument (SE 2.2 on n=220).
**Why did we keep running them?**
→ Because each one produced a number that differed from the last, and differing
numbers feel like information.
**Why do differing numbers feel like information?**
→ Because a point estimate has no visible uncertainty attached. The mean is
legible; the interval has to be computed on purpose. I did not compute it on
purpose until turn nineteen.

**Why did we never plot an operating curve?**
→ Because the threshold was an implementation detail — a thing tuned, not a
thing chosen.
**Why was it an implementation detail?**
→ Because we inherited "accuracy" as the objective without examining it.
**Why did that go unexamined?**
→ Because accuracy is what benchmarks report, and MASSIVE is a benchmark. We
adopted the metric that came attached to the dataset, and that metric was
designed for comparing *classifiers*, not for governing *actuators*.

That is the whole error. We imported an evaluation frame along with the data.

## Hidden assumptions, named and challenged

**A1 — that errors are symmetric.** Accuracy says a false actuation and a
missed command cost the same. On hardware they do not. A device that
occasionally says "say again?" is usable. A device that runs your coffee
machine when you asked about the news is returned.

**A2 — that the architecture was the live variable.** It was not. Every
architecture we tested sat inside one confidence interval. The threshold moved
wrong actuations 12x. We searched the flat axis exhaustively and left the steep
one at whatever value a dev-set sweep happened to produce.

**A3 — that a mean over 9 classes describes the system.** It hides that
iot_wemo_on runs at 30% and inverts polarity half the time. For a light switch
the per-class floor matters more than the mean, and the floor was never reported.

**A4 — that more data always helps retrieval.** It helps discrimination between
*dissimilar* things. Polarity pairs are maximally similar in n-gram space, so
adding examples of "turn on the plug" also adds near-identical examples of
"turn off the plug". More index may not fix N4 and might worsen it.

**A5 — that "the router won" is a finding.** It is a finding about a metric.
Under a cost model weighted 10:1 against wrong actuation, the ranking of every
system we compared could change, because they differ mainly in where their
default threshold sits.

## Core insight

**The architecture was never the live variable — the operating point was, and we
spent twenty experiments searching a flat axis while leaving the steep one set
to whatever value maximised a metric that treats actuating the wrong appliance
as equivalent to asking the user to repeat themselves.**

## Tension resolutions

**T1 (accuracy vs safety) — RESOLVED BY MAKING THE TRADE EXPLICIT.**
Stop reporting accuracy as the headline. Report the operating curve. Pick a
point deliberately from a stated cost ratio. Even without a real cost model, the
dominated point must go: th=0.55 beats th=0.47 on both axes simultaneously, so
that change is free and unconditional.

**T2 (architecture vs operating point) — RESOLVED BY REALLOCATION.**
Architecture search is *done*. Not because we found the winner, but because we
established that the axis is flat relative to our measurement resolution.
Further architecture work requires a bigger test set first, and MASSIVE cannot
provide one. All remaining effort goes to the operating point and to N4.

**T3 (retrieval wins / retrieval cannot do polarity) — RESOLVED BY SPLITTING
THE PROBLEM.** These are not competing; they are complementary. Retrieval
selects the *action family* (lights vs plug vs thermostat) and is good at it.
Polarity (on vs off) is a separate, tiny, closed decision — two outcomes, cued
by a handful of tokens — and belongs to a dedicated deterministic check, not to
the similarity metric. This is the one place a composite architecture is
justified, and it is justified by a measured failure rather than by a hope.

**T4 (per-class vs sample size) — RESOLVED BY DEMOTING THE MEAN.**
Report per-class with support counts always. Treat any class under ~20 test
examples as unmeasured rather than as a low score. iot_hue_lighton at 33% is
1-of-3 and means nothing; iot_wemo_on at 30% is 3-of-10 with a coherent failure
mode and means something.

**T5 (measurable vs important) — RESOLVED BY NAMING THE MECHANISM.**
The same failure as the previous LMM cycle, one level up. Then it was building
instead of specifying; now it is optimising instead of asking what to optimise.
Guard: before any further comparison, state the cost function.
