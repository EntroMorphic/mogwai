# Reflections: closing the 25% gap on 60-class MASSIVE

## Core Insight

The 25% gap is not a representation problem. It is a measurement problem:
the 60-class evaluation measures the representation on classes it was never
built for, against labels that are partly noise, using an index that contains
contradictory entries. The representation is at its ceiling on what it was
built for; the gap is the cost of the generalisation, and most of that cost is
the data, not the architecture.

## Resolved Tensions

### Node 1 vs Node 7: the data was never audited for this evaluation

The existing `invariants.c` checks string-level train/test disjointness for
the 9-class routing problem. It was never applied to the 60-class problem,
and it does not check for *intra-train label conflicts*. The 4 conflict texts
have been in the training index since the corpus was first fetched, unobserved.

**Resolution:** audit the data first. Quantify: how many of the 832 errors are
on (a) test items that appear in train with a different label, (b) test items
whose gold label has < 30 train examples, (c) test items labelled
`general_quirky` (the catch-all). These three categories partition the data
noise from the representation signal.

### Node 4 vs Node 5: is `general_quirky` task mismatch or label noise?

Both. It is a catch-all bucket (label noise by design) *and* a non-device-
control task (task mismatch). The two causes are confounded in its 114 errors.
Partitioning it out removes both confounds simultaneously.

**Resolution:** report the 60-class accuracy two ways: all 60 classes (the
generalisation test, as pre-registered) and a *clean* subset that excludes
label noise and few-shot classes. The pre-registered number (72.0%) stands as
the generalisation result; the clean number is the representation's actual
ceiling.

### Node 5 vs Node 8: 60-class vs IoT-only

The router ships on 9 IoT intents. The 60-class test was a generalisation
test, and it generalised. But the *gap* was interpreted as a representation
problem, when it is mostly a task-mismatch + data-noise problem.

**Resolution:** partition the 60-class result by IoT vs non-IoT. If the IoT
accuracy is much higher than 72%, the representation is at its ceiling on
what it ships on, and the gap is the cost of the generalisation — which is
the right framing.

### Node 6: the cascade was tested on noisy data

The cascade's +1.5 is real but was measured against noisy labels. Some of its
"wrong" reranks may have been correct predictions scored against wrong gold
labels. Re-testing the cascade after partitioning out the data noise would
give a cleaner signal — but that's a second-order question. The first-order
question is: how much of the 832 is data, not representation?

## What I Now Understand

The error dump I built was the right instrument, but I interpreted its output
wrong. I saw three causes (label noise, semantic ambiguity, cross-scenario
confusion) and concluded "the gap is structural." But I never *quantified*
how much of the gap each cause accounts for, and I never checked whether the
*training data itself* was clean.

The 4 intra-train label conflicts are the sharpest finding. They mean the
index contains entries that *contradict each other* — the same text stored
twice with different labels. The NN will always find one of them, and 50% of
the time it's the "wrong" one. This is a data bug, not a representation bug,
and it's fixable.

The 2 train/test label disagreements are measurement errors — the test set
contains items where the gold label *disagrees with the training data on the
same text*. No system can get these right; they should be partitioned out.

The `general_quirky` catch-all is label noise by design. It's MASSIVE's
bucket for "commands that don't fit elsewhere," and the annotators disagreed
on what goes in it. The representation finding a more specific match is not
an error.

The few-shot classes (`cooking_query`, `music_dislikeness`, etc.) are
underrepresented in train. With 4-25 examples, the NN has almost nothing to
match against. These are data-sparse failures, not representation failures.

**The plan:**
1. Quantify the data ceiling: how many of the 832 errors fall into each
   noise category
2. Partition the 60-class result: all-60 (as pre-registered) vs clean subset
3. Partition by IoT vs non-IoT: what is the gap on what the router ships on?
4. If the clean IoT gap is small, the representation is at its ceiling
5. If there's still a fixable gap, re-test the cascade on clean data