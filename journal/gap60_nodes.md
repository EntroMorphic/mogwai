# Nodes of Interest: closing the 25% gap on 60-class MASSIVE

## Node 1: Label noise in the training data

4 texts in train have multiple labels. "good night" → both `audio_volume_mute`
and `iot_hue_lightoff`. These create contradictory entries in the index — the
NN will always find one, and ~50% of the time it's the "wrong" one.

**Why it matters:** this is the simplest possible data bug — duplicate text
with conflicting labels — and it's never been cleaned. The index contains
832 errors worth of signal, and some of that signal is self-contradictory.

## Node 2: Train/test overlaps with label disagreement

21 test texts appear verbatim in train. 19 agree on label, 2 disagree. The 2
disagreements are *impossible errors* — the exact text is in train with a
different label, so the NN finds the exact match, copies the train label, and
is scored wrong. No representation can fix this; it's a measurement error.

**Why it matters:** these 2 items are in the 832 errors and are not the
representation's fault. They should be partitioned out before quoting any
accuracy number.

## Node 3: Few-shot classes

5 classes have < 30 train examples: `cooking_query` (4), `general_greet` (25),
`iot_hue_lighton` (22), `music_dislikeness` (14), `audio_volume_other` (18).
With 4-25 examples in the index, the NN has almost nothing to match against.
These are few-shot failures, not representation failures.

**Why it matters:** the per-class accuracy table shows `iot_hue_lighton` at
33.3% and `cooking_query` at N/A (0 test). These are few-shot artifacts, not
representation limits.

## Node 4: `general_quirky` is 32.5% accuracy — 114 errors, 13.7% of the gap

MASSIVE's catch-all bucket. The confusion matrix shows it scattering to 30+
classes. Many of the errors are arguably correct under a different label:
"internet please" → `qa_definition` (nearest: "please defined about internet")
is not a representation error — the representation found a *better* match than
any `general_quirky` example.

**Why it matters:** `general_quirky` is label noise by design. It's a bucket
for "commands that don't fit elsewhere," and the annotators disagreed on what
goes in it. The representation can't be penalised for finding a more specific
match.

**Tension with Node 3:** some few-shot classes are *also* catch-all buckets.
`general_greet` has 25 train examples and 1 test example — is that a few-shot
problem or a "this label is ill-defined" problem?

## Node 5: The 60-class gap is dominated by non-IoT classes

The router ships on 9 IoT intents. The 60-class test was a generalisation
test. But the error dump shows the top error classes are `general_quirky`
(114), `qa_factoid` (62), `calendar_query` (47), `calendar_set` (41),
`play_music` (40). Only `play_music` is arguably device-control. The
representation was built for "turn off the kitchen light", not "what is elvis
favorite ride" or "please summarize the latest george r. r. martin book."

**Why it matters:** the 25% gap may be a *task mismatch*, not a representation
failure. The IoT classes the router ships on may have a much smaller gap.

**Tension with Node 4:** `general_quirky` is both a task mismatch (it's not
device control) and label noise (it's a catch-all). The two causes are
confounded in the 114 errors.

## Node 6: The cascade was tested before the data was cleaned

The cascade gave +1.5 and plateaued. But it was tested on noisy data: the 4
train conflicts, the 2 test/train label disagreements, and the few-shot
classes were all in the evaluation. If the cascade's rerank changed a
prediction on a noisy item, that counts as "wrong" even if the new prediction
was arguably correct.

**Why it matters:** the cascade's +1.5 might be underestimated (some of its
"wrong" reranks were actually correct but scored against a wrong gold label)
or overestimated (some of its "correct" reranks were on items where both old
and new predictions were wrong). The data noise cuts both ways.

## Node 7: The data has never been audited for this evaluation

The existing `invariants.c` checks for train/test text overlap (and catches
it at the string level for the 9-class routing problem). But it was never
applied to the 60-class problem, and it doesn't check for *label conflicts
within train* — same text, different label. The data cleaning step is missing
from the pipeline.

**Why it matters:** the repo's discipline is "check the data before trusting
the result." The 60-class result was reported, pre-registered, and published
without this check. The result is still valid (the predictions held), but the
*interpretation* — "25% gap, mostly representation" — may be wrong.

## Node 8: Partition by IoT vs non-IoT

The router ships on 9 IoT intents. If I partition the 60-class result into
IoT (the shipped task) and non-IoT (the generalisation test), the IoT
accuracy is probably much higher than 72%. The gap on IoT classes is the one
that matters for deployment; the gap on `qa_factoid` is academic.

**Why it matters:** this reframes the question from "close the 25% gap" to
"what is the gap on what the router actually ships on, and is *that* gap
fixable?"