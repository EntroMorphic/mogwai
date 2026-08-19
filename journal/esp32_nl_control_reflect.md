# REFLECT — the structure beneath

## Asking why, three times

**Why is the encoder only 5/10 on novel input?**
→ Because it was distilled on 2,907 utterances from ~60 hand-written templates.
**Why only 60 templates?**
→ Because I wrote them in one pass, from memory, in a domain I invented.
**Why did I invent the domain?**
→ Because nobody specified one, and I needed *something* to build against —
and building felt more like progress than asking.

That third answer is the whole project in miniature.

**Why did the red team find so much?**
→ Because I was both the author and the examiner of every test.
**Why did that persist for hours?**
→ Because each individual step was locally rigorous — measured, verified,
diffed against a reference. Local rigour masked global circularity.
**Why does local rigour mask it?**
→ Because a closed loop *feels* identical to a validated one from the inside.
The only difference is whether anything external ever entered. Nothing did.

## Hidden assumptions, named and challenged

**A1 — that a neural encoder is needed.** Never tested. The deterministic router
scored 8/10 on hard queries with zero bytes. Challenge: it may be that a device
with a fixed command set has no paraphrase problem worth 610KB.

**A2 — that the command set is lights/thermostat/sensor/GPIO.** I took this from
Needle's documentation examples. It has never been confirmed. Every template,
test query, gazetteer, and accuracy number inherits this guess.

**A3 — that semantic generalisation matters.** Real users of a physical device
converge fast on phrasings that work. The tail I built an encoder for may be
thinner in reality than in my imagination.

**A4 — that LCVDB is the right index.** Challenged and, I think, broken. Measured
against our own device MAC rate: a flat int8 cosine scan is **0.40ms at 200
vectors, 1.99ms at 1,000, 9.94ms at 5,000**. LCVDB's LSH does not even engage
below 10,000. Its wins are real and they are three orders of magnitude above us.

**A5 — that the classic ESP32 is the target.** It arrived running ESP-AT, a WiFi
modem firmware. It may be a bench part, not the product.

**A6 — that 23ms mattered.** No latency budget was ever stated. I optimised
65ms → 23ms against nothing, and the work was genuinely good, and it may have
been entirely unnecessary.

## Core insight

**Nothing in this project has yet touched anything outside itself — the corpus,
the tests, the threshold, and the definition of success were all produced by the
same process being evaluated, which is why every number improved and none of
them mean anything yet.**

## Tension resolutions

**T1 (neural vs deterministic) — RESOLVED BY MEASUREMENT, NOT ARGUMENT.**
Run both against one held-out set with one protocol and one threshold rule. This
is hours of work, not days, and it gates 610KB plus a tokenizer port. Until it
runs, neither approach is chosen. If deterministic wins or ties, the encoder is
deleted and the project gets dramatically smaller.

**T2 (build vs ground truth) — RESOLVED IN FAVOUR OF GROUND TRUTH, ONCE.**
Not a permanent posture. We need exactly one external referent: a stated command
set and ~200 hand-written real utterances, held out entirely. That is a
half-day. After it exists, building resumes with meaning. Before it exists,
building compounds error.

**T3 (LCVDB is theirs and it is good) — RESOLVED BY SEPARATING QUALITY FROM FIT.**
LCVDB is excellent and the measurements in its README are impressive. It is also
built for 65K–6.5M vectors. Declining to port it at N=200 is not a judgement on
the work; it is the same judgement its own design makes, since its LSH declines
to activate below 10K. Revisit if N ever exceeds ~5,000. **Defer, don't discard.**

**T4 (optimisation is legible, specification isn't) — RESOLVED BY NAMING IT.**
The pull toward measurable work is the mechanism that produced the closed loop.
Guard: no further optimisation until a requirement exists that it serves.

**T5 (we cut before sharpening) — RESOLVED BY ACCEPTING THE SUNK WORK IS FINE.**
The board bring-up, the numerics validation, and the 2.84x are all sound and
reusable regardless of what the product turns out to be. The waste, if any, is
bounded and small. This is not a restart.
