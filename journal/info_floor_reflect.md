# Reflections: Intrinsic Information Floor

## Core Insight

The current 21-bit result is a **prefix-code floor**, not yet an intrinsic
semantic information floor.

## Resolved Tensions

### Concrete measurement vs overinterpretation

The pinned floor is real: widths 1-20 fail, widths 21-64 pass on the frozen
universe. But it measures a specific arrangement: current class codes, current
bit order, current unweighted Hamming score, and current candidate-fit term.

Resolution: keep the pinned prefix floor, but label it precisely. Use new
diagnostics to approach the intrinsic floor.

### Scoring policy vs representation capacity

The cleaning/coffee cliff could be caused by insufficient semantic code
information or by candidate-fit bias overwhelming weak code separation at 20
bits.

Resolution: sweep the candidate-fit coefficient at fixed width 20. If reducing
fit weight makes 20 pass, the floor is partly scoring-policy-induced. If 20
still fails at zero fit weight, the code prefix lacks the distinction.

### Bit order vs bit count

If the decisive bit appears at position 21, a prefix sweep says 21 bits are
needed. But a reordered or selected code could expose that bit earlier.

Resolution: run leave-one-out and selected-bit diagnostics. The intrinsic
question is not "first 21?" but "which bits are necessary?"

## What Would Make The Current Interpretation False

- A selected subset of fewer than 21 bits passes all frozen/holdout cases.
- A small reduction of candidate-fit coefficient lets 20 bits pass.
- Leave-one-out at 21 shows only one or two critical bits.
- Reordering bits by measured importance lowers the passing prefix below 21.

## What Would Strengthen The Intrinsic Floor Claim

- 20 bits fails even when candidate fit is removed.
- Many leave-one-out removals at 21 fail on different cases.
- Greedy selected-bit search cannot find a passing subset below 21.
- New blind semantic-code probes push the floor upward smoothly rather than
  revealing a single late bit.

## The Grain

The wood wants to split into three experiments:

1. Fit coefficient floor: is the cliff scoring-induced?
2. Leave-one-out / bit importance: are specific positions critical?
3. Selected-bit floor: can fewer non-prefix bits preserve behavior?

Only after those should the implementation be changed.
