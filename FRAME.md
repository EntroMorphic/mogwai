# Project frame — read this before trusting any number

**This is exploratory research. There is no product and no real user.**
Declared 2026-08-19, revised 2026-08-21.

## What changed since this file was first written

The original frame said the domain was *invented by the assistant* and the
corpus *synthetic*. **That is no longer true and the correction matters.**

The numbers now come from **MASSIVE** (`mteb/amazon_massive_intent`, en) —
11,514 train / 2,033 validation / 2,974 test real human utterances across 60
intents, of which 9 are `iot_*` — supplemented by **NLU-Evaluation-Data**,
MASSIVE's parent corpus. These are human-collected datasets, not templates.

So the *data* is real. Three things still are not:

1. **The task framing is inherited, not specified.** That these 9 `iot_*`
   intents are the ones worth routing, and that the other 51 are "negatives",
   is MASSIVE's annotation scheme — not a decision any stakeholder made about a
   device.
2. **The device assignment is arbitrary.** Whether a "kitchen light" is a Hue or
   a Wemo is a property of someone's installation, unknowable from the
   utterance. Several residual errors are exactly this, and no model can fix them.
3. **Some labels are wrong.** "turn out the lights" is labelled
   `iot_hue_lightup`. The router answers `lightoff` and is scored wrong for being
   right. The corpus ceiling is **~98%, not 100%**.

## What these numbers do and do not support

They support **relative** claims between representations measured on the same
split under the same protocol — twin-ternary versus binary at matched bytes,
d=256 versus d=128, pruned versus unpruned. Those comparisons are controlled and
the controls are documented.

They do **not** support absolute claims about deployed behaviour on someone's
hardware, because nobody's hardware is attached and no stakeholder defined what
success means. Do not quote them as product metrics.

**And read the right column.** `recall` is blind to false actuations by
construction. For an actuator, `fa` — fired on something that was not a command
— is the number that matters.

## What is genuinely established

- Twin-ternary beats binary at **matched bytes per vector**, on the full
  operating curve, not at a single tuned point. Binary saturates: its d=256 and
  d=512 curves are identical.
- The device runs the **same arithmetic as the host**, bit-exactly, verified on
  64 reference queries at four index sizes and two dimensions.
- The on-device cost model is **quantitative and predictive**: 59.8 ns/byte +
  326 ns/vector, residuals within 1.8% across a 4× range of bytes per vector.
- Board bring-up, ESP-AT backup and **verified restore**.
- Optimisations are real and each preserved parity: 200.4 → 43.5 ms.

## What was established and then invalidated

Kept as negative results, not erased. See EXPERIMENTS.md.

- A +4.5-point lexicon gain that was fitted to test failures.
- Four design decisions made on a 75.6%-leaked dev split.
- Test evaluation #1.
- "Pruning improves false actuations" — a threshold artifact.
- Two two-point cost models, both refuted by a third point.

## The methodological rule

A test set must be produced by a process **independent** of the training corpus,
and independence must be *verified, not assumed* — by exact-string disjointness
**and** by checking that no test item shares an encoded code with an index entry.
Both are enforced by assertions that abort, in `c/src/invariants.c`. The rule
originally pointed at `heldout.py`; that file is archived and the enforcement is
now structural rather than advisory. See [doc/METHOD.md](doc/METHOD.md).
