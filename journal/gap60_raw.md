# Raw Thoughts: closing the 25% gap on 60-class MASSIVE

## Stream of Consciousness

The cascade gave +1.5 and plateaued. I tried widening the shortlist, mask-only
stage 1, IDF weighting — all plateaued or hurt. The error dump showed three
causes: label noise (~14%), semantic ambiguity (~26%), and cross-scenario
confusion (~60%). I concluded "the gap is structural, not algorithmic." But
that conclusion was reached before I checked the data.

The data check just found:

1. **4 texts in train have multiple labels.** "good night" is both
   `audio_volume_mute` and `iot_hue_lightoff`. "olly book me a taxi" is both
   `calendar_remove` and `transport_taxi`. These are genuine annotation
   errors — the same text genuinely could be either intent, and the annotator
   picked different labels for different occurrences. The router cannot
   distinguish them because *there is nothing to distinguish* — same text,
   different label. This is label noise in the training data itself.

2. **21 train/test overlaps.** 19 agree on label, 2 disagree. "i helped a poor
   needy today olly" is `iot_coffee` in train and `general_quirky` in test.
   "olly tell me my alarms" is `general_greet` in train and `alarm_query` in
   test. These are test items where the nearest neighbour is an *exact match*
   in train, but with a *different label*. The router finds the exact match,
   copies the train label, and is scored wrong. This is a data construction
   problem, not a representation problem.

3. **5 classes with < 30 train examples.** `cooking_query` has 4 train
   examples and 0 test. `iot_hue_lighton` has 22 train and 3 test.
   `music_dislikeness` has 14 train and 4 test. These are few-shot classes
   where the index has almost nothing to match against. No representation can
   classify reliably from 4 examples.

4. **`general_quirky` is 32.5% accuracy and 114 errors — 13.7% of the gap.**
   This is MASSIVE's catch-all bucket. The confusion matrix shows it scattering
   to 30+ classes. Many of these are arguably correctly answered under a
   different label. The label is noise.

So the question isn't "how do we close the gap" — it's "how much of the gap is
*the data* and how much is *the representation*?" I jumped to the cascade
without cleaning the data first. That was a mistake. The repo's own
discipline says: check the data before you trust the result.

The first question: what happens to the 72.0% if we remove the label noise
from the *test* set? Not the train set — the test set is what we're measured
against. If a test item has a label that no representation could predict
because the same text has a different label in train, that's not the
representation's error. It's a measurement error.

But we can't just *change* the test set — that's the budgeted resource, and
changing it is exactly the kind of leak the methodology exists to prevent.
What we *can* do is measure: how many of the 832 errors are on test items
where (a) the text appears in train with a different label, or (b) the text
is in the 4 conflict texts, or (c) the gold label has < 30 train examples.
That gives an upper bound on how much of the gap is data, not representation.

The second question: what happens if we *deduplicate* the training index? The
4 conflict texts create 2 entries each with different labels — the nearest
neighbour will always find one of them, and 50% of the time it's the "wrong"
one. Removing the duplicates (or picking one label per text) might help the
training side.

The third question, the one I haven't asked: **is 60-class NN even the right
task?** The router ships as a 9-class routing problem. The 60-class result
was a generalisation test, and it generalised. But the gap analysis showed
that the gap is dominated by classes the router was never designed to
handle — `general_quirky`, `qa_factoid`, `cooking_recipe`. These are
open-domain QA and general chat, not device commands. The representation
was built for short, imperative IoT commands ("turn off the kitchen light"),
not for "what is elvis favorite ride" or "internet please". The gap may be
a *task mismatch*, not a representation failure.

## Questions Arising

- How many of the 832 errors are on test items with train conflicts or
  few-shot labels? (Quantify the data ceiling)
- Would deduplicating the 4 conflict texts in train change the result?
- Is the gap on IoT classes (the ones the router ships on) actually much
  smaller than the 60-class gap?
- Is there a data cleaning step that doesn't touch the test set but improves
  the index (e.g. removing the 4 conflicts from train)?
- The 21 train/test overlaps with 2 label disagreements — those 2 are
  *impossible* errors. How much do they move the number?

## First Instincts

- The 25% gap is probably 30-40% data, 60-70% representation, and the
  representation part is mostly on non-IoT classes the router doesn't ship on.
- If I partition the 60-class result by IoT vs non-IoT, the IoT accuracy is
  probably much higher than 72% — the router was never trying to classify
  "what is elvis favorite ride".
- The cascade didn't help because it was trying to fix representation errors
  on data that was noisy. Cleaning the data first might change the cascade's
  result too.
- I think the real finding is: **the representation is at its ceiling on
  what it was built for; the 60-class gap is mostly task mismatch and data
  noise, not a representation problem.**