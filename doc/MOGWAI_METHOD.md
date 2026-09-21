# Mogwai Method

The Mogwai Method is a way to build MCU-class System One decision engines. It
borrows JEV's interface shape, not JEV's architecture:

    unstructured state in -> typed software-readable decisions out

For Mogwai, the runtime target is smaller and stricter: a static artifact, a
bounded integer hot path, explicit refusal, and host/device parity. The goal is
not chat and not a general agent. The goal is local, inspectable decisions that
ordinary embedded software can consume directly.

## Contract

A Mogwai Method system has five parts:

1. **Schema** - the valid output space, declared before fitting or compiling.
2. **Evidence** - examples, negatives, boundary cases and a held-out split.
3. **Artifact** - the compiled semantic decision surface, including metadata.
4. **Runtime** - a bounded scorer that emits typed candidates or refusal.
5. **Operational layer** - downstream code that authorizes, actuates and audits.

The semantic engine stops at proposed meaning. It does not own policy, state,
permissions, audit, side effects or GPIO decisions.

## Minimal ABI

The first ABI is intentionally small. It generalizes beyond a flat class label
without pretending to be a full ontology.

```c
typedef enum {
    MOG_REFUSAL_NONE = 0,
    MOG_REFUSAL_BELOW_THRESHOLD = 1,
    MOG_REFUSAL_AMBIGUOUS = 2,
    MOG_REFUSAL_NEGATIVE = 3,
    MOG_REFUSAL_UNSUPPORTED = 4,
} mog_refusal_t;

typedef struct {
    uint16_t schema_id;
    uint16_t intent_id;
    uint16_t object_id;
    uint16_t attribute_id;
    int16_t score;
    int16_t margin;
    uint8_t confidence_bucket;
    uint8_t refusal_reason;
} mog_decision_t;
```

The fields have fixed meaning:

| field | meaning |
|---|---|
| `schema_id` | compiled schema/version identifier |
| `intent_id` | primary decision type |
| `object_id` | target object, subsystem or entity |
| `attribute_id` | secondary semantic value such as severity or polarity |
| `score` | raw backend score for the selected candidate |
| `margin` | winner score minus second-best score |
| `confidence_bucket` | empirical bucket when calibrated; heuristic bucket in prototypes |
| `refusal_reason` | why no semantic proposal should be accepted |

Scores are not probabilities unless a calibration table has been measured and
pinned. Until then, Mogwai may expose score, margin and explicitly named
heuristic buckets only.

## Pipeline

Every domain should follow the same sequence:

1. Write the schema first.
2. State the cost ratio: wrong output vs refusal.
3. Collect positive examples, negatives and boundary cases.
4. Freeze a held-out split before threshold or margin selection.
5. Compile a candidate surface with schema metadata, thresholds, calibration
   metadata and reference cases.
6. Report an operating curve, not only accuracy.
7. Select threshold and margin from the cost ratio.
8. Pin artifact metadata and reference cases.
9. Prove host/runtime parity before device claims.
10. Only then flash firmware, if the target requires it.

## Operating Curve

The standard report is cost-sensitive:

| metric | meaning |
|---|---|
| accepted correct | proposed typed meaning matches ground truth |
| accepted wrong | proposed typed meaning is wrong |
| missed actionable | ground truth was actionable but runtime refused |
| refused negative | ground truth was negative and runtime refused |
| false action | negative accepted as actionable |
| wrong action | actionable input accepted as the wrong action |

Accuracy is not a headline metric. A useful report sweeps threshold and margin so
the operating point can be selected from an explicit cost ratio.

## Calibration Ladder

Mogwai has three confidence levels:

1. **Level 0: score only.** Raw score and margin. No confidence claim.
2. **Level 1: confidence bucket.** Validation data maps ranges to buckets such as
   low, medium and high.
3. **Level 2: calibrated probability.** A pinned reliability table maps score and
   margin to empirical probability with error bounds.

The current method prototype is Level 0 plus a heuristic bucket demonstration. It
must not claim Level 1 empirical confidence or JEV-style calibrated probability
until a validation reliability table exists.

## First Second-Domain Proof

The first proof outside home automation is **device log triage**. It is chosen
because it is software-native, typed, non-actuating and MCU-plausible. Inputs are
short unstructured status or log lines. Outputs are typed decisions:

| field | values |
|---|---|
| intent | `ignore`, `inspect`, `throttle`, `reset`, `alert` |
| object | `system`, `network`, `storage`, `sensor`, `power` |
| attribute | `info`, `warning`, `critical` |

The proof target is not production accuracy. The current evaluator compiles the
source cases into a bounded `MOG1` wire artifact, then parses and validates that
byte image before evaluation. The parsed artifact carries schema metadata,
selected threshold, selected margin, calibration metadata, encoded exemplars and
embedded reference cases. It still uses a zero-centre twin-ternary demo backend
and heuristic confidence buckets, so it is not a production blob and not
calibrated probability.

    schema -> examples/negatives -> host evaluator -> operating curve -> typed ABI

Firmware remains untouched until the host-only proof produces stable semantics.
The wire compiler/parser now lives outside the evaluator and has a standalone
guard. The next proof is to make `MOG1` a real on-disk artifact with a separate
compiler command, rather than a demo artifact built in memory.

## Non-Goals

- No policy engine.
- No authorization system.
- No audit log.
- No runtime schema mutation.
- No on-device learning.
- No probability language without calibration.
- No C6 product claim without target-specific product validation.

## Acceptance Criteria

A domain is a Mogwai Method proof only when all are true:

1. The schema existed before tuning.
2. Refusal is represented explicitly.
3. The held-out split existed before threshold selection.
4. The report includes wrong/false/missed/refused counts and an operating curve.
5. The runtime emits the minimal typed ABI.
6. Artifact metadata can identify schema, backend and score semantics.
7. Host and runtime-equivalent results agree bit-exactly.
8. The operational layer can be replaced without changing the semantic artifact.
