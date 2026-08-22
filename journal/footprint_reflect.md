# REFLECT — what survived being tested

## N1/N2/N3 — CONFIRMED, and this is the cycle's finding

The asymmetric architecture was the most promising idea and it was built and
measured. Exact nearest-neighbour over the 1155 positives (72 KB) plus a
statistical model of none-ness from the word channel:

    positives only, th=180, veto on:   fa=1  wa=3  missed=74  59.9%
    full index,     th=136:            fa=1  wa=13 missed=14  85.9%

**The veto contributes nothing.** Positives-only with the veto is marginally
worse than without it. Rejection is not a property the word channel can express.

The first version of this test was mis-specified — it used th=136, tuned for the
full index, against a positives-only index whose scores run higher. That gave
fa=1149 and would have rejected the idea for the wrong reason. Re-run with the
threshold swept, the idea still fails, but now for the right reason.

**Why it fails is the interesting part.** Rejection asks "is this close to things
that are not commands?" That is a similarity judgement, and it must happen in the
same representation as the positives. A different channel cannot answer it,
however good that channel is at classification. The 584 KB is not redundancy —
it is the only evidence the system has about what a non-command looks like.

## N4/N5/N6 — text-as-storage is dominated twice over

Text is genuinely 1.83x smaller than the vectors (358 vs 656 KB), which was the
surprise of the cycle. But entropy-coded vectors are 327 KB — smaller still, and
they need no regeneration. Text-as-storage loses on size AND costs 558 ms of
re-encoding per query. Killed on both axes.

## N8/N9 — the hardware is not the constraint here

The C6 fabric trick used PARLIO, which this part does not have. Its nearest
equivalent was already priced at 3.4x slower than the CPU. And every peripheral
with addressable memory sums to under 20 KB against a 656 KB index.

The deeper point: **peripherals compute, they do not store.** A dataflow fabric
can transform a stream, which is what the Ternary-CfC needs — its state is small
and its computation is the expensive part. Here the computation is cheap (8% of
time) and the STATE is the expensive part. The two problems have opposite shapes,
so the technique does not transfer.

## What was NOT tested

- Whether a *learned* compact negative model (rather than the existing word
  prior) could reject. Everything here reuses machinery that already exists.
- Product quantisation or any codebook method over the vectors.
- Whether rejection needs 9345 negatives or would survive on 2000 — the pruning
  curve says the frontier degrades, but no one has asked how few negatives are
  needed at matched fa specifically.
