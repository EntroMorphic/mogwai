# Runtime-choice probe

`c/bin/choice_probe` is the first host-only probe for Jev-like runtime choices.
It does **not** add a learned semantic hash and it does **not** implement NSW.
It asks a narrower question first:

> Before adding learned weights, can exact top-k neighborhoods over Mogwai's
> existing exemplar corpus add useful signal beyond direct twin-ternary scoring?

The tool builds the shipped index shape, encodes a query and arbitrary candidate
descriptions with the current encoder, and reports:

- direct twin-ternary score between query and candidate text
- exact top-k exemplar neighborhoods for query and each candidate
- top-k overlap
- class-histogram agreement
- maximum candidate-to-candidate direct similarity
- warning flags for low margin, candidate collision, and `none` basin wins

The required runtime-choice measurements are printed on the `metrics:` line:

- `direct`: direct query-to-winning-candidate similarity in the active encoder
- `neighborhood_overlap` / `neighborhood_agreement`: graph-neighborhood evidence
- `margin`: winner score minus runner-up score
- `reachable`: whether the winner was reachable in the explored neighborhood;
  for `choice_probe`, this means overlap or strong class-neighborhood agreement,
  and for `semhash_probe`, this means positive learned-code agreement

Run the demo:

    c/bin/choice_probe --demo --k=8

Run the built-in red team:

    c/bin/choice_probe --redteam

`make regress` requires this to print `REDTEAM checks=27/27 score=100/100`.

## Red-team findings

The current raw topology is useful, but unsafe as a decision rule.

Positive result: `dim lights` versus candidates such as `lower brightness` shows
the intended topology-rescue effect. Direct score alone is weak, but both strings
land in a strongly `iot_hue_lightdim` neighborhood.

Negative result: `make the room less luminous` still confuses lower/raise
brightness. The graph exposes the collision; it does not solve polarity.

Negative result: out-of-domain phrases such as `refund the customer` fall into
the large `none` basin. Class-histogram agreement can look strong there, so any
future production rule must gate topology by direct fit, basin purity, and an
explicit `none` outcome.

Safety rule: treat `combo` as a feature inspection aid only. It is deliberately
not calibrated, not thresholded, and not a production runtime-choice policy.

## What this proves

It proves the measurement path, not the architecture. Exact neighborhood
signatures can surface useful manifold information and known failure modes while
remaining integer-only and deterministic.

## What comes next

The next fair experiment is a learned semantic hash measured against this exact
baseline:

1. raw direct score
2. raw direct score plus exact neighborhood signatures
3. learned semantic hash direct score
4. learned semantic hash plus exact neighborhood signatures

Only after exact neighborhoods show value should NSW replace exact top-k as an
acceleration structure.

## Learned semantic hash proof

`c/bin/semhash_probe` is the first learned-weight version of the runtime-choice
probe. It trains a tiny integer multiclass perceptron from the shipped exemplar
labels and uses the winning learned class as a compact semantic code. Query and
candidate descriptions pass through the same learned encoder.

Run it:

    c/bin/semhash_probe --demo
    c/bin/semhash_probe --redteam

The first result is intentionally modest but useful: on the darker/brightness
demo, the learned hash separates `increase brightness` from `decrease
brightness`, where raw direct score could not. It still reports low-margin and
collision cases, and it is still host-only research code, not a production
policy.

`semhash_probe` reports the same required measurements, with
`neighborhood_agreement` meaning learned semantic-code agreement rather than
exact top-k class-histogram agreement.

## Dataset evaluator

`runtime_choice_eval` is the next artifact after the single-case probes. It uses
a fixed tagged runtime-choice eval set:

- query
- arbitrary candidate descriptions
- correct candidate index, or `-1` for `NONE`
- tags: `paraphrase`, `polarity`, `collision`, `out-of-domain`, `bridged`,
  `unbridged`

It scores every case five ways:

- `raw_direct`
- `raw_neighborhood`
- `semhash_direct`
- `semhash_neighborhood`
- `residual_combo`

It reports accuracy, wrong-act rate, missed/none rate, mean margin, collision
rate, reachable-but-not-selected count, selected-but-not-reachable count,
polarity failures, OOD gates, and per-query knownness. The current diagnostic
decision is:

```text
decision: residual_combo beats the current champion; keep combined evidence and expand the adversarial set.
next: polarity failures are zero under residual_combo on this probe.
next: reachable-but-not-selected exists; improve selection/rerank before NSW.
next: residual still has selected-but-not-reachable cases; make the learned representation create the missing bridge.
next: OOD knownness gate preserves NONE on this probe; expand OOD negatives.
```

`residual_combo` is intentionally simple integer evidence fusion, not a tuned
policy: raw topology plus scaled semhash evidence plus explicit lexical polarity
compatibility, gated by the existing `none` manifold basin and a conservative
query-knownness floor for low-energy OOD queries. The learned projection also
receives a tiny audited seed set of bridge positives and hard OOD negatives;
these seeds update only the host learned weights and are not inserted into the
exact top-k index. Red-team expansion added a
negation polarity case and a near-class OOD case (`light rail refund`) that uses
IoT vocabulary but should still be `NONE`. The residual path gets the current
ten-case probe to `10/10`, preserves `NONE` on all three OOD cases, and removes
polarity failures. After seeded projection, `semhash_neighborhood` also reaches
`10/10` on this probe.

The evaluator has a built-in red team:

    c/bin/runtime_choice_eval --redteam

For atomic failure analysis:

    c/bin/runtime_choice_eval --details

It pins case validity, neighborhood improvements, collision-rate invariance
across abstain gates, negation handling, near-class OOD abstention, abstain
reachability cleanup, reachability accounting, and out-of-domain wrong-act
accounting.

## Atomic floor

`runtime_choice_eval --details` prints the primitive measurements used by every
variant: direct score, top-k overlap, class-histogram agreement, raw-topology
score, semhash score, learned-code agreement, polarity compatibility,
knownness, gate reason, reachability, winner, runner-up, and margin.

Measured floor before seeded projection:

| Case | Exact measurement | Meaning |
|---:|---|---|
| 3 | Correct candidate has `overlap=0/8`, `hist=324`, `code=-136`, `reachable=no`; wrong raise candidate has `direct=88` vs correct `86`, `raw_topo=122` vs `118`, and semhash `21` vs `-102` | The representation/topology still has not created the missing bridge. Residual wins only because polarity adds `+80` to lower and `-120` to raise. |
| 5 | Query top basin is `none`, but semhash predicts `iot_cleaning`; semhash chooses candidate 0 with `score=265`, `margin=306` | Learned semhash still collapses some OOD text toward known classes unless the manifold `none` gate runs first. |
| 8 | Negated query has `qpol=-1`; raw/topology/semhash all choose `on`; residual chooses `off` because polarity compatibility is `+80` for off and `-120` for on | Polarity is an independent action factor, not something general similarity handles safely. |
| 9 | Query `light rail refund` has `knownness=98`, below `RESIDUAL_KNOWN_GATE=120`; all non-residual variants choose candidate 2, residual gates to `NONE` | Near-class OOD remains the hard OOD floor; query-knownness is currently the measured guardrail. |

Seeded projection lifts that floor on the current ten-case probe:

| Case | Exact measurement after seeding | Meaning |
|---:|---|---|
| 3 | Correct candidate changes to `sem=295`, `code=256`, `reachable=yes`; `semhash_neighborhood` winner is correct with margin `126` | The missing bridge is now present in learned-code space, without adding exemplars to the exact top-k index. |
| 5 | Query semhash prediction becomes `none`; all learned variants gate to `NONE` | The original OOD collapse is fixed on this probe. |
| 9 | Query semhash prediction becomes `none`; learned variants gate to `NONE`, and residual also gates by `knownness=98 < 120` | Near-class OOD is protected both by learned `none` and by the residual knownness guard. |

The current floor is no longer this ten-case probe. The next floor must be found
by expanding adversarial bridges, OOD negatives, and polarity/negation cases
until one of those guarantees breaks under measurement.
