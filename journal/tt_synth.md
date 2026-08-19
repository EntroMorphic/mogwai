# SYNTHESIZE — closing the twin-ternary gap

## Decision 1 — Fix the scoring, alone, first. (hours)

Replace the asymmetric `t_score` with Dice, which is symmetric, integer and
length-invariant:

    score = (2 * dot * 256) / (active_a + active_b + 8)

Measured drift across query lengths: **t_score 149->184 (23%), Dice
159->155 (9%)**. The bias runs strict-on-short / loose-on-long, and IoT
commands are median 5 words while negatives are longer — so it is refusing the
commands and actuating the noise.

*Success criterion:* the missed count falls materially from 32 without wrong
actuations rising. Measure this change **on its own**, before touching anything
else, or we will not know which fix mattered.

## Decision 2 — Retrieval cascade, and be explicit about the purchase.

Cascading is not an optimisation — we already scan in 1.6ms. It is a mechanism
for affording a stage 2 we could not otherwise run.

    stage 1   mask plane only, ~half the ops    12,083 -> top 100
              measured recall@100 = 97.3%
    stage 2   rerank 100 against the STORED TEXT
              exact token overlap + character n-gram containment
              no hash collisions, no dimensional bottleneck

**Do not buy more dimensions.** d=2048 twin-ternary is 6.2MB and does not fit.
The text is ~600KB and carries strictly more information than any hash width.

*Guard, from the -35 point failure:* stage 1 must **reorder, never reject**.
The final threshold remains the only refusal point. A cascade that can refuse
in stage 1 is the classification cascade that already cost us 35 points.

## Decision 3 — Derive or delete the signature mask constant.

`om[d]*4 >= cnt` (dim active in >=25% of a class) is unjustified and in the hot
path. Either derive it (a dim enters the mask iff it is more active in this
class than in the index overall) or delete the mask and use the sign plane
alone for signatures. Unvalidated constants are how the structural ranker died.

## Decision 4 — Re-measure dimension AFTER the above.

d=512 was chosen when the representation was binary. Twin-ternary carries ~2x
the information per dimension, so the optimum may have moved down, not up.
Sweep 256/512/1024 once scoring is fixed; a smaller index that fits with OTA is
worth real accuracy.

## Non-goals

- **No wider hash.** Storage-blocked, and the cascade makes it unnecessary.
- **No new mechanism until scoring is fixed.** We have one diagnosed bug with a
  mechanical signature; adding mechanisms on top of it will confound the fix.

## Success criteria

1. Dice measured alone; score drift collapses and missed falls from 32.
2. Cascade stage 1 demonstrably reorders without rejecting.
3. Signature mask constant derived from data or removed.
4. Every run appended to results/RESULTS.tsv with its git SHA.
5. If the gap does not close to within ~3 points of 90.5%, say so plainly
   rather than reaching for a fifth mechanism.
