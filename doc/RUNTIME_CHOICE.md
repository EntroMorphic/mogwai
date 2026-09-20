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

It scores every case four ways:

- `raw_direct`
- `raw_neighborhood`
- `semhash_direct`
- `semhash_neighborhood`

It reports accuracy, wrong-act rate, missed/none rate, mean margin, collision
rate, reachable-but-not-selected count, selected-but-not-reachable count, and
polarity failures. The current diagnostic decision is:

```text
decision: semhash_neighborhood improves the learned path; add topology there next.
next: raw_neighborhood is still the strongest baseline; improve the learned projection before replacing it.
next: polarity failures persist; add a factorized polarity channel.
next: reachable-but-not-selected exists; improve selection/rerank before NSW.
```
