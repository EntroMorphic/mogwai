# RAW — the twin-ternary gap, and whether cascading helps

Twin-ternary bought 6.4 points over binary for one extra bit per dimension,
exactly as predicted: sparse hash vectors were agreeing on hundreds of empty
dims that carried no information. Good. But we are still 10 points behind the
Python version we replaced, and I just found two reasons that are not "ternary
was a bad idea."

The score is not length-invariant. Mean best-score by query length: 149 for
2-4 words, 184 for 8-10. A 23% drift. One global threshold is therefore
systematically stricter on short queries and looser on long ones — and IoT
commands are median 5 words while the negatives (calendar, news, qa) run
longer. So the bias runs exactly the wrong way: long out-of-domain utterances
clear the bar and get actuated, short in-domain commands fall short and get
refused. That is 32 missed commands with a mechanical explanation. Dice
normalisation flattens it to 9%.

The mask plane alone gives recall@100 of 97.3% at half the operations. That is
a real shortlist. Which raises the cascading question honestly: what would we
spend the savings on? Scanning 12,083 vectors already costs 1.6ms. Saving
compute we do not need is not a win.

What scares me: we tried a cascade before and it lost 35 points. But that was a
classification cascade — a gate that decided in-domain, then a second stage
with no way to abstain. Every gate false-positive became a wrong action. A
retrieval cascade is a different animal: it narrows candidates and the final
threshold still refuses. I need to be careful not to let a bad memory of one
architecture veto a different one that happens to share a name.

The thing I keep circling: we store 12,083 utterances at build time, hash them
into 512 dimensions, and throw the text away. The hash is lossy by
construction. But the raw text is only ~600KB — we could afford to keep it.
Stage 1 narrows to 100 on ternary; stage 2 reranks those 100 against the actual
strings. That is strictly more information than any hash can carry, on a
candidate set small enough to make it free.

Open questions:
1. How much of the 10 points is normalisation versus dimension?
2. Is the arbitrary om[d]*4>=cnt signature mask costing us?
3. If the cascade buys compute, what do we spend it on — dimensions, or text?
4. Does keeping the text break the zero-float, cache-resident thesis?
5. Is d=512 even the right width now that the representation changed?
