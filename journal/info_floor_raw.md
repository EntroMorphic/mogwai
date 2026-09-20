# Raw Thoughts: Intrinsic Information Floor

## Stream

We have two measured floors now, but they are not the same kind of object. The
factor floor is behavioral and structural: only `pol+color+comp+loc+support`
passes across the frozen runtime-choice universe. The bit floor is
representational and contingent: with the current class-code assignment and
score equation, 20 visible bits confuse `clean the flat` with `make coffee`, and
21 bits fixes it. The danger is treating the second as if it were intrinsic
information content. It might be. It might also be bit ordering, hash-code luck,
candidate-fit bias, or one overloaded distinction landing late in the code.

The current semhash code is not a learned bit allocation in the deep sense. It is
a deterministic class code derived from label/name hashes, compared by truncated
Hamming distance. That means bit positions are not necessarily ordered by
semantic usefulness. If bit 21 separates cleaning from coffee, that does not
prove 21 independent semantic bits are required. It proves that the current
prefix of 20 bits lacks one necessary distinction under the current scoring
policy.

The current score has two forces: code agreement and candidate class fit. At low
bit widths, Hamming discrimination weakens while candidate fit remains fixed.
So the bit floor may partly measure how much semantic code is needed to overcome
candidate-fit bias. A better allocation or an adaptive fit coefficient could
lower the observed width without reducing semantic capability. Conversely, if
no reweighting or bit selection lets 20 pass, then cleaning-vs-coffee really is
outside the smaller code's capacity.

The most useful next move is not to guess a smaller encoding. It is to separate
three hypotheses: ordering artifact, scoring artifact, and true capacity. The
LMM shape here is to stop compressing blindly and map the grain of the failure.

## Questions

- Which bits are actually load-bearing at 21 bits?
- Is the 21st bit uniquely responsible for cleaning/coffee, or is 21 just the
  first width where aggregate Hamming margin crosses a threshold?
- If candidate-fit weight is reduced at 20 bits, does cleaning beat coffee?
- Could a selected subset of fewer than 21 bits preserve all behavior?
- Are failures below 21 concentrated in a few semantic families or distributed?
- Is the current code assignment semantically meaningful enough to optimize, or
  should the direct factors move toward explicit ternary signatures instead?

## First Instincts

- Do not shrink the runtime code yet.
- Add diagnostics before changing the scorer.
- Treat `21` as an observed prefix floor, not an intrinsic lower bound.
- The next bedrock is probably a selected-bit or weighted-bit floor, not a raw
  prefix-width floor.
