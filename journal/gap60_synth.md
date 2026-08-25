# Synthesis: closing the 25% gap on 60-class MASSIVE

## Architecture

The 25% gap is not one thing. It is four things, and three of them are data.
Before any further representation work, partition the gap:

1. **Data ceiling:** how many of the 832 errors are on noisy items?
2. **Clean accuracy:** what is the accuracy after removing noise?
3. **IoT-only accuracy:** what is the gap on what the router ships on?
4. **Cascade re-test:** does the cascade help on the *clean* partition?

## Key Decisions

1. **Do not touch the test set.** It is the budgeted resource. The 72.0%
   pre-registered result stands. We *partition* errors by cause; we do not
   *remove* test items to improve the number.

2. **Deduplicate the training index** by removing the 4 label-conflict texts.
   Keep one copy of each (the first occurrence). This is a data cleaning step,
   not a test-set change. The index is the training data; cleaning it is
   always allowed.

3. **Report two numbers:** the pre-registered 72.0% (all 60 classes, as
   measured) and a "clean" accuracy that partitions out the three data-noise
   categories. The clean number is not a new claim; it is an analysis of the
   existing result.

## Implementation Spec

### Step 1: quantify the data ceiling

For each of the 832 errors, classify it as one of:
- **conflict:** test text appears in train with a different label
- **few-shot:** test item's gold label has < 30 train examples
- **catch-all:** test item's gold label is `general_quirky`
- **representation:** none of the above

Report: how many errors fall into each category, and what the accuracy is
after partitioning each out.

### Step 2: IoT vs non-IoT partition

Partition the 60 classes into:
- **IoT (9):** the classes the router ships on (`iot_*`)
- **non-IoT (51):** everything else

Report accuracy on each partition. The IoT accuracy is the one that matters
for deployment; the non-IoT accuracy is the generalisation cost.

### Step 3: deduplicate the training index

Remove the 4 label-conflict texts from the training index (keep one copy of
each). Re-run the 60-class evaluation on the *validation* set (not test — no
budget unit) to see if the clean index changes the dev result.

### Step 4: cascade on clean data

If step 1 shows a significant data ceiling, re-run the cascade on the
*validation* set with the deduplicated index, partitioned by the noise
categories. Does the cascade help on the "representation" partition?

## Success Criteria

- [ ] The 832 errors are partitioned by cause
- [ ] The clean accuracy is reported alongside the pre-registered 72.0%
- [ ] The IoT-only accuracy is reported
- [ ] The training index is deduplicated
- [ ] The cascade is re-tested on clean data (dev only)
- [ ] The result is recorded in EXPERIMENTS.md as an analysis, not a new test
      evaluation (no budget unit spent)