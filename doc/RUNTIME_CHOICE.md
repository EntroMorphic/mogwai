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

It reports accuracy, commit precision, learned coverage, wrong-act rate,
missed/none rate, mean margin, collision rate, reachable-but-not-selected count,
selected-but-not-reachable count, polarity failures, OOD gates, per-query
knownness, and route-support state counts (`learned_reachable`,
`residual_rescue`, `unsupported`). `commit_precision` is `ok/(cases-misses)`:
correct choices plus correct `NONE` decisions among cases where the variant did
not miss an in-domain action. `learned_coverage` is
`learned_reachable/in-domain-cases`, which is the metric to drive upward at a
fixed zero wrong-actuation rate. The current diagnostic decision is:

```text
decision: semhash_neighborhood now matches residual_combo; keep exact topology and expand the adversarial set before NSW.
next: polarity failures are zero under residual_combo on this probe.
next: reachable-but-not-selected exists; improve selection/rerank before NSW.
next: OOD knownness gate preserves NONE on this probe; expand OOD negatives.
```

`residual_combo` is intentionally simple integer evidence fusion, not a tuned
policy: raw topology plus scaled semhash evidence plus explicit lexical polarity
compatibility, gated by the existing `none` manifold basin and a conservative
query-knownness floor for low-energy OOD queries. The learned projection also
receives a tiny audited seed set of bridge positives, balanced compositional
route positives, and hard OOD negatives; these seeds update only the host
learned weights and are not inserted into the exact top-k index. Red-team
expansion now includes holdout bridge, contraction
negation, inverse negation, `keep ... from getting ...`, `avoid ...`,
`prevent ... from getting ...`, unseen `stop ... getting ...` negation, and
polarity-bearing transit OOD. The residual path gets the current twenty-three
case probe to `23/23`, preserves `NONE` on all five OOD cases, and removes
polarity failures. `semhash_direct` reaches `22/23`, with
`commit_precision=22/23` and `learned_coverage=17/18`.
`semhash_neighborhood` now matches `residual_combo` at `23/23`, with
`commit_precision=23/23`, `learned_coverage=18/18`, zero wrong actuation, and
all five OOD cases still rejected. This only held after semhash
candidate scoring included the same explicit polarity compatibility term; the
balanced route seeds alone made the representation more reachable but too
permissive on literal non-negated candidates.

The evaluator has a built-in red team:

    c/bin/runtime_choice_eval --redteam

The current 23-case probe is frozen as regression. New generalization pressure
goes through a separate blind holdout mode:

    c/bin/runtime_choice_eval --holdout
    c/bin/runtime_choice_eval --holdout-redteam

The holdout reports attribution so perfect accuracy cannot hide rule dependence:

```text
attribution learned_accept=2 learned_reject=1 topology_rescue=0 operator_factor=7 domain_reject=10 hard_ood_veto=0 residual_rescue=0 wrong=0
```

On the current 20-case blind holdout, residual, polarity-aware semhash direct,
and polarity-aware semhash neighborhood all reach `20/20`, with `9/9` learned
in-domain coverage and zero wrong actuation. The OOD successes remain split by
cause: near-class and metaphorical lighting collisions are attributed to
`domain_reject`, not the legacy hard veto bucket, while `write a grocery list`
is the first pinned `learned_reject`. The holdout exposed three real gaps before
pinning: `avoid increasing ...` was not recognized as an upward brightness axis
because `increasing` was missing from the factorized polarity vocabulary,
`activate the hallway lamps` needed compositional support for `activate -> ON`,
`lamps -> lighting`, and target/location compatibility, and learned abstention
needed a support/knownness gate separate from winner margin. The red-team probes
`activate the porch lamps` and `activate the hallway speaker` pin that the
activation factor cannot leak across unsupported locations or unsupported
objects. Metaphorical OOD cases such as `brighten my day`, `increase the account
balance`, and `dim the appetite` are still counted as explicit domain rejects,
not learned rejects.

This 20-case holdout is now frozen as Holdout A. Do not remediate it further.
Fresh generalization pressure goes into Holdout B:

    c/bin/runtime_choice_eval --holdout-b
    c/bin/runtime_choice_eval --holdout-b-redteam

Holdout B started as a first-shot combinatorial transfer set, deliberately run
before remediation. Its first-shot baseline was intentionally not perfect:

```text
semhash_neighborhood accuracy=10/12 learned_coverage=5/5 wrong_act=2/12 residual_rescue=0
attribution learned_accept=2 learned_reject=1 topology_rescue=0 operator_factor=3 domain_reject=4 hard_ood_veto=0 residual_rescue=0 wrong=2
```

The two visible first-shot failures were unsupported-location transfer probes
(`don't let the garage lights dim`, `activate the foyer lighting`). They were
recorded as `wrong`, not hidden as learned rejects or residual repairs. This
preserved the first-shot evidence: in-domain combinatorial coverage transferred,
learned abstention transferred once, and the next real boundary was
unsupported-location domain support beyond the explicit Holdout A locations.

The remediation added `garage`, `foyer`, and `basement` to the explicit
unsupported-location/domain boundary, plus two new red-team probes so the fix did
not only patch the failing strings. Current Holdout B is now:

```text
semhash_neighborhood accuracy=14/14 learned_coverage=5/5 wrong_act=0/14 residual_rescue=0
attribution learned_accept=2 learned_reject=1 topology_rescue=0 operator_factor=3 domain_reject=8 hard_ood_veto=0 residual_rescue=0 wrong=0
```

For atomic failure analysis:

    c/bin/runtime_choice_eval --details

It pins case validity, neighborhood improvements, collision-rate invariance
across abstain gates, negation handling including normalized `don't` -> `don t`,
near-class OOD abstention, abstain reachability cleanup, reachability
accounting, route-state accounting, commit precision, learned coverage, and
out-of-domain wrong-act accounting.

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

Seeded projection lifted that floor on the ten-case probe:

| Case | Exact measurement after seeding | Meaning |
|---:|---|---|
| 3 | Correct candidate changes to `sem=295`, `code=256`, `reachable=yes`; `semhash_neighborhood` winner is correct with margin `126` | The missing bridge is now present in learned-code space, without adding exemplars to the exact top-k index. |
| 5 | Query semhash prediction becomes `none`; all learned variants gate to `NONE` | The original OOD collapse is fixed on this probe. |
| 9 | Query semhash prediction becomes `none`; learned variants gate to `NONE`, and residual also gates by `knownness=98 < 120` | Near-class OOD is protected both by learned `none` and by the residual knownness guard. |

The current twenty-three-case red team found the next floor:

| Case | Exact measurement | Meaning |
|---:|---|---|
| 11 | Query `please don't brighten the hallway` normalizes contraction negation to `don t`; balanced route seeding and polarity-aware semhash make `dim` reachable and selected | Contraction-negation phrasing is now represented, but only with the explicit route factor. |
| 12 | Query `refund the light rail pass` has `knownness=117 < 120`; semhash predicts `none`, and residual gates to `NONE` | Near-class OOD remains protected after the holdout expansion. |
| 13 | Query `don't increase the hallway brightness`; semhash direct and neighborhood now select `dim` with reachability | Negated-increase composition moved from residual rescue to learned-route support. |
| 14 | Query `do not make the hallway brighter`; semhash direct and neighborhood now select `dim`; residual remains correct | Strong raw topology for the literal brightening phrase must be countered by a structural polarity term. |
| 15 | Query `don't dim the hallway`; semhash direct, semhash neighborhood, and residual now select reachable `brighten` | Inverse negation is represented after balanced route seeding; keep reachable-but-not-selected visible on the remaining semhash-direct edge before NSW. |
| 16 | Query `keep the hallway from getting brighter`; top raw basin is `none`, but semhash and residual now select reachable `dim` because explicit polarity prevents the `none` gate from vetoing the route | `none` is a manifold gate for unsupported queries, not a veto over explicit compositional polarity. |
| 17 | Query `keep the hallway from getting darker`; semhash direct/neighborhood and residual now select reachable `brighten` | The route channel needs normalized phrase-level negation, not just token antonyms. |
| 18 | Query `avoid making the hallway brighter`; semhash direct/neighborhood and residual now select reachable `dim` | Prevention verbs are polarity operators, not ordinary semantic content. |
| 19 | Query `prevent the hallway from getting darker`; semhash direct/neighborhood and residual now select reachable `brighten` | Balanced inverse-prevention routes prevent one-sided dim overfitting. |
| 20 | Query `stop the hallway getting brighter`; initially followed the literal brightening route until `stop` became a prevention cue | Unseen prevention verbs must map into the same route operator, not into lexical similarity. |
| 21 | Query `stop the hallway getting darker`; semhash neighborhood and residual now select reachable `brighten` | The route needs balanced inverse-prevention coverage, not one-sided dimming. |
| 22 | Query `stop the light rail getting brighter`; polarity is present, but transit terms force learned/residual variants to `NONE` | Explicit polarity must not bypass the OOD boundary. |

The current floor is therefore no longer reachability on this probe; it is
generalization pressure. Semhash neighborhood has `18/18` learned coverage and
zero residual rescues, but this was achieved with audited host-only route seeds
and an explicit transit OOD gate. The next adversarial set must test more unseen
inversion verbs and polarity-bearing OOD near-misses without weakening the
`NONE` and knownness gates that protect cases 5, 6, 9, 12, and 22.

The next primary metric is learned coverage at fixed zero wrong-actuation rate:

```text
LC0 = learned_reachable / in-domain cases, subject to wrong_act = 0
```

Current runtime-choice floor:

| Variant | Commit precision | Learned coverage | Wrong act |
|---|---:|---:|---:|
| `raw_direct` | `7/22` | `2/18` | `15/23` |
| `raw_neighborhood` | `7/22` | `4/18` | `15/23` |
| `semhash_direct` | `22/23` | `17/18` | `1/23` |
| `semhash_neighborhood` | `23/23` | `18/18` | `0/23` |
| `residual_combo` | `23/23` | `18/18` | `0/23` |
