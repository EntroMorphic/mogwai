# REFLECT — what the two planes are actually for

## Why, three times

**Why did twin-ternary help?**
→ Because sparse vectors were agreeing on empty dimensions.
**Why did that matter so much?**
→ Because agreement on absence is not evidence, and binary cannot express the
difference between "both say no" and "neither knows".
**Why is that the deepest form of the problem?**
→ Because it is the same error as the length bias, one level down. Both come
from treating *quantity of signal* as *strength of signal*. Long queries score
higher because they have more active dims, not because they match better.
Empty dims agreed because absence looked like agreement. **The representation
kept confusing "how much" with "how well".**

**Why does the cascade tempt us?**
→ Because recall@100 is 97.3% at half the cost, and that looks like free money.
**Why is it not obviously free?**
→ Because we already scan in 1.6ms. Saved compute with nowhere to go is not a
saving.
**Why does that reframe the question?**
→ A cascade is not an optimisation here; it is a *purchase mechanism*. Its
entire value is what stage 2 can afford that stage 1 could not. Asking "does
cascading help?" is malformed. The question is "what would we buy?"

## Hidden assumptions, challenged

**A1 — that the 10-point gap is about the representation.** Partly false. The
length bias is a *scoring* bug with a mechanical signature (149 -> 184), and it
maps directly onto the 32 missed commands. Fix the score before blaming ternary.

**A2 — that cascades are discredited here.** False by category error. The -35
point failure was a classification cascade whose second stage had no abstention;
every gate false-positive became a wrong action. A retrieval cascade narrows
candidates and the final threshold still refuses. Same word, different machine.

**A3 — that the hash IS the representation.** This is the one worth naming. The
hash is an *index* — a lossy 512-dim projection chosen so scanning is cheap. The
representation is the text, and we discard it at build time. 12,083 utterances
is ~600KB, which is cache-resident by any standard we have used all session. We
have been optimising a lossy proxy while holding the original.

**A4 — that more dimensions is the natural purchase.** Storage-blocked: d=2048
twin-ternary is 6.2MB and will not fit. So the obvious purchase is unavailable,
which is precisely why the text option matters.

**A5 — that om[d]*4 >= cnt is harmless.** Unjustified constants in the hot path
are how the structural ranker died. It has never been validated.

## Core insight

**The two planes exist to separate "no evidence" from "evidence against" — and
every remaining problem is the same confusion at a different altitude: length
bias confuses more signal with better match, and discarding the text confuses
the index with the representation. A cascade is not an optimisation, it is a
purchase mechanism, and the thing worth buying is not more dimensions but the
text we already threw away.**

## Tension resolutions

**T1 (cascade saves compute we do not need) — RESOLVED BY NAMING THE PURCHASE.**
Stage 1 (mask plane, 97.3% recall@100) narrows 12,083 to 100. Stage 2 reranks
those 100 against the stored strings — exact token and character overlap, no
hash collisions, no dimensional bottleneck. Cost on 100 candidates is trivial;
storage is 600KB. This buys information that no widening of the hash can.

**T2 (cascade has a bad name) — RESOLVED BY THE DISTINCTION.** Classification
cascades destroy the abstention channel; retrieval cascades do not. Guard: the
final threshold must remain the only refusal point, and stage 1 must never
reject — only reorder.

**T3 (hash vs text betrays the thesis) — RESOLVED, IT DOES NOT.** The thesis is
zero-float, integer, cache-resident. Character comparison is integer. 600KB is
cache-resident. Nothing about keeping strings requires a float or a heap.

**T4 (two causes, one gap) — RESOLVED BY ORDERING.** Fix normalisation first
and measure alone. It is the cheaper change, it has a mechanical signature we
can verify (score drift should collapse), and it is the one with a diagnosis
rather than a hypothesis.
