# REFLECT — the guarantee I was holding and did not use

## Why, three times

**Why did I report an impossible result as a finding?**
→ Because I was reading the number, not the relationship between the two
things being measured.
**Why did the relationship not surface?**
→ Because I had never written it down. "Twin-ternary contains binary" lived in
the design rationale, not in the test harness. Nothing in the code asserted it.
**Why does that matter more than the leak itself?**
→ Because the leak was one bug and I have made three of its kind, but the
*missing assertion* is why a bug of that kind can run for four turns without
tripping anything. **A guarantee that is not encoded is not a guard, it is a
belief — and beliefs lose to numbers.**

**Why did "too good to be true" not stop me?**
→ Because I said it and moved on, and saying it felt like having handled it.
**Why is naming a suspicion so satisfying?**
→ It discharges the discomfort without paying the cost of the check. It reads,
to me and to the reader, as rigour.
**Why is that the dangerous form?**
→ Because it *inoculates*. A caveat attached to a number makes the number look
examined. I flagged 99.5% as suspicious in the same breath I built on it.

## Hidden assumptions, challenged

**A1 — that the dedup set was protecting the index.** It protected the paths
that existed when I wrote it. I later added dev-carving, which pushes to V_t and
`continue`s — never touching `hs_add`. The guard did not fail; it was bypassed
by a path that did not exist when it was designed.

**A2 — that the test number is now known.** False. The 71.4% measures a
configuration (d=256, mask=none, veto=off) selected entirely from leaked dev. We
spent a test evaluation to measure a config we would not have chosen.

**A3 — that the veto is harmful.** Void. Clean dev shows 11 wrong / 16 missed
with it against 17 / 7 without — a safety trade, and one the operating-curve
framing says is legitimate. It passed a paired McNemar in Python; I discarded it
on leaked evidence and should not have.

**A4 — that each contamination was a distinct mistake.** Three instances, one
shape: evaluation data touching training data. Fitting a lexicon to test,
iterating against test, and leaking dev into the index are the same error
wearing different clothes.

**A5 — that more guards will fix it.** Guards are per-instance. What generalises
is *assertions on invariants*, because they fire on any cause — a leak, a bad
threshold, a broken encoder — without knowing which.

## Core insight

**I was holding a structural guarantee — twin-ternary strictly contains binary —
that made the anomaly self-diagnosing, and I read the measurement instead of the
guarantee. Contamination will keep finding new paths around per-instance guards;
what catches it regardless of route is asserting the relationships the design
already promises, in code, where a violation halts the run instead of getting
written up.**

## Tension resolutions

**T1 (guards protect only known paths) — RESOLVED BY MOVING UP A LEVEL.**
Stop adding guards at data-entry points. Assert invariants at measurement time:
twin-ternary >= binary; index and eval sets disjoint; dev near-duplicate rate
within tolerance of test's. Each fires regardless of how it was violated.

**T2 ("too good to be true" as inoculation) — RESOLVED BY A RULE.**
A suspicion voiced is a task created. If a number is called suspicious, either
test it in the same turn or do not build on it. No caveated results.

**T3 (spent budget, void config) — RESOLVED BY ACCOUNTING.**
Mark evaluation #1 void in the ledger with the reason. Do not spend #2 until
the four voided decisions are re-derived on clean dev.

**T4 (unused structural guarantees) — RESOLVED BY INVENTORY.**
Enumerate what the design promises and encode each as an assertion:
superset (ternary >= binary), disjointness (index ∩ eval = empty), boundedness
(scores within +-RD), monotonicity (recall@K non-decreasing in K).
