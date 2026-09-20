# Nodes of Interest: Intrinsic Information Floor

## N1: Two Floors, Two Meanings

The factor floor says five semantic relations are currently necessary. The bit
floor says the current prefix-code representation needs 21 visible bits.

Why it matters: removing factors and shrinking code width are different axes.
Conflating them would produce the wrong optimization target.

## N2: Prefix Width Is Not Information Content

The `--bit-floor` sweep reveals the smallest prefix of the current code that
passes. It does not prove no smaller subset, reordered code, or weighted code can
pass.

Tension: the result is concrete and pinned, but easy to overinterpret.

## N3: Candidate Fit Becomes a Bias at Low Width

The score combines Hamming code similarity with `c.score / 8`. As code width
shrinks, code discrimination weakens but candidate-fit bias remains constant.

Dependency: any intrinsic information claim must test whether 20 bits fails even
when candidate fit is attenuated or removed.

## N4: The Last Failure Is Localized

At 20 bits the final failure is `clean the flat -> make coffee`. That suggests
the cliff may be a localized semantic distinction rather than broad collapse.

Tension: a localized failure could mean one badly placed bit, not a global
capacity limit.

## N5: Bit Importance May Be Sparse

If only a few of the first 21 bits are load-bearing, the code has redundancy or
bad ordering. If many leave-one-out removals fail, the 21-bit prefix is closer
to a real information floor.

## N6: Selected-Bit Floor Is the Next Truer Floor

The prefix floor asks, "how many leading bits?" A selected-bit floor asks,
"what is the smallest subset of bits, anywhere in the 64-bit code, that passes?"

Why it matters: selected-bit floor is closer to implementation bedrock than
prefix width.

## N7: Weighted Bits Are a Different Hypothesis

If the selected-bit floor is still high, maybe bit weights matter. A single
cleaning/coffee bit may need more weight, or candidate-fit weight may need to
fall with code width.

## N8: Explicit Factors May Eventually Replace More Code Bits

Color and location already moved relations out of topology/code. Cleaning vs
coffee could be another explicit domain/action relation, but adding that too
early risks turning the factor system into a growing hand-built ontology.

## N9: The Best Next Experiments Are Diagnostic, Not Remedial

The system passes at 21. Remediation is unnecessary. The useful work is to
explain whether 21 is a code-layout artifact or semantic capacity.
