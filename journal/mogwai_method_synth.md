# SYNTHESIZE - The Mogwai Method

## Thesis

Mogwai should become a method for building MCU-class System One decision engines:
compile a bounded semantic decision surface offline, then run it on-device as a
small deterministic runtime that returns typed candidate meanings, scores,
margins and refusal reasons.

Borrow JEV's interface ambition, not its architecture:

    unstructured local state in
        -> typed software-readable decisions out
        -> uncertainty/refusal attached
        -> ordinary code composes the result

For Mogwai, the constraint is stricter:

    MCU budget, static artifact, bounded runtime, no string generation,
    host/device parity, no policy or actuation inside the semantic engine.

## Definition

The **Mogwai Method** is a repeatable pipeline:

1. Define a typed decision schema.
2. State the operational cost ratio for wrong output vs refusal.
3. Build examples, negatives and boundary cases against that schema.
4. Compile them into a compact artifact.
5. Run a bounded MCU runtime that scores the enumerable output space.
6. Return typed candidates with scores, margins and refusal reasons.
7. Select an operating point from an operating curve, not from accuracy.
8. Prove host/device parity on embedded reference cases.
9. Keep authorization, policy, state and side effects downstream.

## Non-negotiable invariants

1. **Typed outputs only.** The output space is declared before the artifact is
   built. No free-form generation is part of the runtime contract.
2. **Refusal is a value.** `none`, `unknown`, `ambiguous` or `below_threshold` is
   not a failure path. It is one of the values downstream software must handle.
3. **Scores are not probabilities unless calibrated.** The runtime may expose
   raw score, margin and confidence bucket. It may expose probability only after
   an empirical calibration table is measured and pinned.
4. **The artifact is an ABI.** Dimensions, thresholds, schema hash, score
   semantics, calibration metadata and reference cases are part of the compiled
   object, not loose documentation.
5. **The device proves equivalence.** Every backend must have host/device parity
   tests for class, typed fields and score semantics.
6. **No policy in the semantic engine.** Mogwai proposes meaning. A downstream
   operational layer authorizes, refuses, actuates and audits.
7. **Report the frontier.** Accuracy is secondary. The method reports wrong
   action, missed action, refusal and cost-sensitive operating curves.

## First target artifact: `MogwaiDecision`

Do not jump straight to a large schema. Add the smallest ABI that generalizes
beyond flat class labels:

```c
typedef struct {
    uint16_t schema_id;
    uint16_t intent_id;
    uint16_t object_id;
    uint16_t attribute_id;
    int16_t  score;
    int16_t  margin;
    uint8_t  confidence_bucket;
    uint8_t  refusal_reason;
} MogwaiDecision;
```

This is illustrative, not final. The important move is to separate:

- semantic candidate fields: `intent_id`, `object_id`, `attribute_id`;
- evidence fields: `score`, `margin`, `confidence_bucket`;
- refusal fields: `refusal_reason`;
- schema/version fields: `schema_id`.

The runtime can still be twin-ternary underneath. The ABI should not expose that
as the method.

## First experiment: method proof, not feature proof

Run the next LMM/build cycle on a second tiny domain. The goal is to prove the
pipeline repeats, not to maximize accuracy.

Candidate domains:

| domain | why it is useful | risk |
|---|---|---|
| device log triage | structured state in, typed severity/cause out, no actuation risk | may be too text-heavy |
| sensor anomaly labels | MCU-native, bounded labels, easy refusal | less language-like |
| command plus slot extraction | closest to current router, easiest transition | may look like mere home automation expansion |
| safety interlock explanation | forces refusal and downstream policy boundary | hard to collect data |

Recommendation: use **device log triage** or **sensor anomaly labels**. Avoid a
home-automation slot demo as the first method proof; it will not falsify enough.

## Acceptance criteria for the next proof

The next proof succeeds only if all are true:

1. A schema is written before examples are generated.
2. At least one typed output has more structure than a flat class label.
3. A refusal outcome is represented explicitly in the schema.
4. A held-out set exists before threshold selection.
5. The report shows an operating curve with wrong/refused/missed counts.
6. A blob embeds schema/version metadata and reference cases.
7. Host and firmware-equivalent tests agree bit-exactly.
8. If flashed, device output reaches parity on reference cases.
9. The downstream policy layer can be mocked without changing the semantic blob.

## Calibration plan

Do not promise JEV-style probabilities yet. Add a calibration ladder:

1. **Level 0: score only.** Raw score and margin. No confidence claim.
2. **Level 1: confidence bucket.** Buckets learned from validation reliability:
   `low`, `medium`, `high`, `refuse`.
3. **Level 2: calibrated probability.** A pinned reliability table maps score and
   margin to empirical probability with error bars.

Mogwai currently lives between Level 0 and Level 1. The method should make Level
2 possible without pretending it already exists.

## What to build next

1. Write `doc/MOGWAI_METHOD.md` as the formal method once this synthesis is
   accepted.
2. Define a minimal typed decision ABI in docs before code.
3. Pick one non-home-automation domain and run the full pipeline.
4. Add a host-only compiler/evaluator before touching firmware.
5. Add firmware-equivalent parity tests before flashing.
6. Flash only after the host proof closes.

## What not to build yet

- No generalized policy engine.
- No on-device learning.
- No runtime schema mutation.
- No probability language before calibration exists.
- No large multi-slot ontology until one small typed ABI is proven.
- No C6 production claim until product firmware, pins, WiFi reserve and power are
  validated separately.

## Final sentence

JEV shows the automation interface: fast typed decisions instead of strings.
Mogwai's opportunity is to make that interface physical: a compiled, inspectable,
refusing semantic decision surface that fits on MCU-class hardware.
