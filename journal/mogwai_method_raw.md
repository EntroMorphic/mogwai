# RAW - Mogwai Method and JEV-shaped MCU intelligence

## Stream

The prompt is not "make Mogwai do home automation better." That was the proving
domain. The real question is whether the work so far can be abstracted into a
method for building MCU-class System One intelligence: fast, typed, bounded,
calibrated-enough decisions that software can consume directly.

JEV's useful shape is not chat. It is "unstructured state in, typed probabilistic
decisions out." It gives up string generation and gains type safety, speed,
parallel output and composability inside ordinary code. Mogwai already made a
similar trade under much harsher constraints: no cloud, no floats in the hot
path, no giant model, no runtime policy engine, no free-form text output. It
routes raw-ish language into a bounded semantic decision with refusal. It proves
host/device parity. It makes the artifact small enough to live on an ESP32.

The danger is imitation. If the next move is "build tiny JEV," the work will
almost certainly collapse into either a poor classifier or an over-scoped neural
port. The power to borrow is methodological: define the output space first,
compile it into a typed decision surface, score all possibilities in one bounded
pass, attach honest confidence/refusal semantics, and let surrounding software
compose the result. That sounds like Mogwai, but Mogwai is still too tied to
class labels and IoT intents.

The possible "Mogwai Method" might be a recipe:

1. Start with a task where output structure is known in advance.
2. Convert that structure into a compact semantic manifold: labels, factors,
   slots, or typed decisions.
3. Build a deterministic or near-deterministic encoder that maps messy input
   into the same space.
4. Keep all choices enumerable and score them in parallel or one flat pass.
5. Treat rejection and uncertainty as first-class outputs, not errors.
6. Prove host/device parity and pin the artifact as an ABI.
7. Put authorization, policy and actuation outside the semantic engine.

That is not a model architecture. It is a development method for constrained
intelligence.

What scares me: calibration. JEV claims probabilities and confidence, not just a
score. Mogwai has margins, thresholds and operating curves, but those are not yet
calibrated probabilities. Calling them probabilities would be dishonest. The
method must distinguish confidence, margin, abstention cost and empirical
calibration. It may be enough to say MCU-Mogwai produces calibrated decisions
only after a calibration pass exists; before that, it produces ranked typed
candidates with refusal.

Another fear: typed output can become a policy engine by stealth. Once outputs
are richer than labels, the temptation is to encode rules, permissions, state,
audit, side effects and temporal logic in the semantic layer. That would ruin the
clean boundary. Mogwai should propose meaning; another layer decides action.

The strongest possibility: Mogwai can be positioned as the embedded counterpart
to System One models. Not because it matches frontier intelligence, but because
it shares the automation interface shape: software-readable structured decisions
instead of strings. For MCUs, the question is not "can it know everything?" It is
"can it make the small decision local, fast, inspectable and safe enough to be
called constantly?"

The naive approach would be to add multi-slot extraction to the current router:
actor/action/object/location/time/confidence. That might be useful, but it risks
premature architecture. The method probably needs a higher-level grammar of
decision surfaces before adding features: what are valid outputs, what evidence
supports each output, what is refused, and how does the downstream layer consume
it?

Open questions:

- What is the smallest typed decision ABI that generalizes beyond class labels?
- Can Mogwai emit calibrated probabilities, or only ranked scores and margins?
- What counts as "parallel output" on an MCU: all candidates scored in one pass,
  all typed questions answered independently, or both?
- How does a developer create a new Mogwai domain without hand-authoring a giant
  brittle corpus?
- Can the method remain non-neural, or does the method allow learned artifact
  generation while keeping runtime deterministic?
- What benchmark would prove this is more than an IoT router with better words?

First instinct: define the Mogwai Method as a constrained-intelligence compiler.
Input: a typed decision schema, examples, negatives and cost ratios. Output: a
blob plus a tiny runtime that returns typed candidates, scores, margins and
refusal reasons. The compiler may use larger models offline, but the MCU runtime
must remain bounded, inspectable and parity-proven.

Probably wrong: assuming the current twin-ternary representation is the method.
It may be only one backend. The method is stronger if it treats representation as
replaceable, but preserves the contract: typed output, bounded decision surface,
refusal, calibration protocol, artifact ABI, host/device parity.
