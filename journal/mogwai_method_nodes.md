# NODES - Mogwai Method and JEV-shaped MCU intelligence

## Node 1: The product is the interface shape, not home automation

Mogwai's durable contribution is not controlling lights. It is a pattern for
turning messy local input into a software-readable semantic decision under MCU
constraints.

Why it matters: home automation can remain the benchmark fixture while the method
generalizes to other embedded decisions.

## Node 2: JEV's transferable power is structured decisions, not model size

The relevant JEV properties are: predefined output space, type-safe values,
parallel decisions, confidence/uncertainty and direct software composition. The
irrelevant property for Mogwai is frontier-scale modeling.

Tension: borrowing JEV's ambition without importing JEV's architecture.

## Node 3: The MCU translation is a compiled decision surface

On an MCU, "System One" cannot mean broad latent intelligence. It can mean a
precompiled decision surface: all valid outputs known in advance, encoded into a
small artifact, scored in bounded time.

Dependency: the schema must exist before the model. If the output is not known,
Mogwai cannot be type-safe.

## Node 4: Refusal is part of the output type

Mogwai's existing threshold and `none` behavior is not an error path. It is a
first-class semantic result: the device declines to propose meaning.

Why it matters: refusal is the embedded counterpart to calibrated uncertainty.
Without it, typed outputs become dangerously overconfident actuation triggers.

## Node 5: Scores are not probabilities yet

JEV claims calibrated probabilities. Mogwai currently has scores, margins,
thresholds and operating curves. These can support abstention and cost-sensitive
decision points, but they are not probabilities unless calibrated empirically.

Tension: the method wants JEV-like confidence, but must not overclaim.

## Node 6: The semantic engine must not become the operational layer

Richer typed outputs invite rules, permissions, state, audit and side effects to
creep into the engine. That would collapse Mogwai into a policy system.

Boundary: Mogwai proposes typed meaning; downstream software authorizes or
refuses action.

## Node 7: Offline intelligence is allowed if runtime remains bounded

The method can use large models or heavier tooling to generate corpora, schemas,
negative cases, labels or calibration sets. The MCU runtime should remain small,
static, deterministic and parity-proven.

Tension: learned artifact generation versus non-learned runtime execution.

## Node 8: Parallel output has an MCU-specific meaning

JEV outputs all typed probabilities in one query. On Mogwai, parallel may mean:
score every candidate in one flat pass, answer several independent typed
questions from one encoded input, or emit a ranked vector of candidates rather
than a single string.

Decision point: define this precisely before claiming "JEV-like" behavior.

## Node 9: The artifact boundary is the trust boundary

The blob is not just data. It is the compiled semantic decision surface. Its ABI,
hashes, dimensions, thresholds, score semantics and reference queries are the
thing that make the method reproducible.

Dependency: every new method claim should compile into an artifact with host and
device parity checks.

## Node 10: The first proof must be a method benchmark, not a new feature

Adding actor/action/object/location slots would be tempting, but it might only
prove feature creep. A better proof is a repeatable workflow: define schema,
compile artifact, evaluate operating curve, flash device, prove parity.

Why it matters: if the product is method, the output should be reproducibility
across domains, not one impressive domain.

## Tensions

- JEV-like calibrated confidence vs Mogwai's current score/margin semantics.
- General method vs current twin-ternary implementation.
- Rich typed output vs preserving the semantic/operational boundary.
- Offline model-assisted compilation vs MCU runtime determinism.
- Feature demo vs method proof.
