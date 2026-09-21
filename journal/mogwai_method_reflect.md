# REFLECT - Mogwai Method and JEV-shaped MCU intelligence

## Core insight

The Mogwai Method is a way to compile a bounded semantic decision surface for an
MCU: unstructured local state in, typed candidate meanings plus refusal evidence
out, with the runtime kept deterministic, inspectable and parity-proven.

## Reflection on the nodes

JEV matters here because it changes the interface expectation. The old frame is
"an AI talks to a person." The JEV frame is "an intelligence call returns typed
values software can branch on." Mogwai already lives closer to the second frame
than the first. It never wanted to generate prose. It wanted to let software use
messy input without writing brittle if-statements.

The MCU constraint changes the implementation but not the interface ambition.
JEV can afford a frontier model behind the interface. Mogwai cannot. Therefore
Mogwai's intelligence has to be mostly moved into the artifact boundary: schema,
examples, negatives, representation, thresholds, calibration set and reference
queries. Runtime intelligence is the cheap execution of a decision surface built
offline.

This makes the method look like a compiler more than a model. The source program
is a typed decision schema plus evidence. The compiler emits a blob. The target
machine is an MCU. The runtime is small and boring. The proof is parity plus an
operating curve.

## Resolved tension 1: JEV-like confidence vs honest Mogwai scores

Resolution: do not call Mogwai scores probabilities by default. Define three
levels:

1. `score`: raw model/runtime similarity or factor score.
2. `confidence`: empirically mapped margin/score bucket from validation data.
3. `probability`: only allowed when calibration has been measured and pinned.

This preserves the JEV direction without lying. The first Mogwai Method can emit
scores, margins and refusal reasons. A later calibration pass can attach
probability tables if the reliability curve supports it.

## Resolved tension 2: General method vs twin-ternary backend

Resolution: twin-ternary is the first backend, not the method. The method's
invariants are stronger and more portable:

- outputs are typed and enumerable;
- the runtime is bounded;
- refusal is a valid output;
- artifacts are ABI-pinned;
- host/device parity is proved;
- operating curves are reported instead of a single accuracy number;
- policy and actuation stay outside.

Any backend that satisfies those invariants can be a Mogwai backend. Twin-ternary
is the proven one.

## Resolved tension 3: Rich outputs vs policy creep

Resolution: separate semantic type from operational type. A Mogwai output can say
"candidate meaning: turn/light/off/location=bathroom, confidence bucket high."
It must not say "therefore drive GPIO4" or "user is authorized" or "audit passed."
Those belong downstream. The method should make this a hard rule, not a style
preference.

## Challenged assumptions

### Assumption: a method needs one universal representation

Probably false. A method needs stable invariants and evaluation rules. The
representation can vary by domain and budget.

### Assumption: MCU intelligence must be trained on-device to count

False for this path. The intelligence can be compiled offline and executed on
device. On-device learning is a different product with different failure modes.

### Assumption: type-safe output is enough for automation safety

False. Type safety prevents malformed outputs. It does not prove correctness,
authorization or safe actuation. Mogwai must keep refusal and downstream policy.

### Assumption: a higher-cardinality decision surface automatically generalizes

False. More output types can make calibration worse and refusal harder. Domain
growth must be measured by operating curves and false-action cost, not schema
ambition.

## What would falsify the direction

- A second domain cannot be compiled without domain-specific hacks larger than
  the runtime itself.
- Scores and margins fail to support stable refusal under held-out data.
- Typed factor outputs produce more false actions than flat labels at the same
  recall because errors become compositional.
- The artifact ABI grows until it no longer fits MCU flash/SRAM budgets.
- The operational layer repeatedly needs semantic-layer state or policy to make
  the output usable.

## What would confirm it

- Two different domains share the same pipeline: schema -> examples/negatives ->
  blob -> operating curve -> firmware parity.
- The runtime ABI returns the same shape of typed candidate object in both.
- Calibration/refusal curves are stable enough that downstream code can select an
  operating point from a stated cost ratio.
- The artifact remains small enough for ESP32-class flash and SRAM.
- The downstream layer can authorize/refuse without reaching inside the semantic
  engine.

## What I now understand

The method should not start by making Mogwai more expressive. It should start by
making Mogwai more formal. Define the compiled semantic decision surface and its
proof obligations. Then add expressivity only when it can pass the same method:
typed schema, bounded runtime, refusal, operating curve, artifact ABI, parity.
