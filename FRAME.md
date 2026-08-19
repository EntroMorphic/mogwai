# Project frame — read this before trusting any number

**This is an exploratory project. There is no product and no real user.**
Declared 2026-08-19.

The target domain — smart-home style commands (lights / thermostat / sensor /
GPIO) — was **invented** by the assistant from Needle's documentation examples.
It was never specified by a stakeholder.

## What this means for the numbers

Any routing accuracy figure here (13/16, 5/10, or anything later) measures
**relative** capability between architectures on a synthetic corpus. It does
**not** predict real-world behaviour, because there is no real world attached.

Do not quote these numbers as product metrics. Do not let them decide anything
outside this repo.

## What is still genuinely valid

- Board bring-up, ESP-AT backup and verified restore
- C encoder matching host to cos 0.999973
- int8/int32 precision, tested across four widths on the full corpus
- The 2.84x optimisation and its two negative results
- Distillation: cos-to-teacher 0.9871 on a clean held-out split

These are engineering facts about implementations, not claims about a task.

## The one methodological rule going forward

A test set must be produced by a process **independent** of the training
corpus, and independence must be *verified*, not assumed — by measuring each
test query's nearest-neighbour similarity to the training set and excluding
near-duplicates. See `heldout.py`.
