# SYNTHESIZE — make contamination structurally impossible

## Decision 1 — Assert the invariants in the harness. (hours)

The harness must refuse to report when the design's own promises are violated:

    assert  index ∩ (dev ∪ test) == empty        (by normalised string)
    assert  twin-ternary iot_acc >= binary iot_acc - tolerance
    assert  |dev near-dup rate - test near-dup rate| < 5 points
    assert  all scores within [-RD, +RD]

A violated invariant **aborts the run**. It does not print a warning, because a
warning is a caveat and caveats inoculate. This is the generalisation of the
leak fix: guards protect known paths, assertions fire regardless of route.

*Justification:* the superset assertion alone would have caught this bug four
turns and one wasted test evaluation ago.

## Decision 2 — Re-derive the four voided decisions on clean dev.

All were chosen from a dev set that was 75.6% leaked:

| decision | chosen on leaked dev | status |
|---|---|---|
| d = 256 | tied with 512 | **re-sweep** |
| signature mask = none | all variants identical | **re-test** |
| veto = off | "94.3 -> 99.5 without it" | **void; clean dev shows a trade** |
| Dice normalisation | +2.7 (1.1 SE) | **re-test at matched granularity** |

Clean-dev baseline to beat: binary 78.2 +-3.0, twin-ternary 87.0 +-2.4.

## Decision 3 — Void test evaluation #1 in the ledger.

It measured d=256 / mask=none / veto=off — a configuration selected entirely
from leaked evidence. Record it as VOID with the reason rather than deleting
it; a spent budget with a known cause is worth more than a clean-looking ledger.

Do not spend #2 until Decision 2 is complete.

## Decision 4 — One rule for suspicion.

**A number called suspicious is either tested in the same turn or not built on.**
I wrote "99.5% is very high. And suspicious" and then made four decisions from
it. The caveat made it look examined.

## Non-goals

- **No new mechanisms** until the voided decisions are re-derived. We do not
  know which of the current four are real.
- **No test evaluations** until then.

## Success criteria

1. Harness aborts on any invariant violation; verified by deliberately
   reintroducing the leak and confirming it halts.
2. Four decisions re-derived on clean dev, each with error bars.
3. Ledger records evaluation #1 as VOID with cause.
4. Twin-ternary >= binary on every configuration tested — if it ever is not,
   that is a bug and the run stops.
