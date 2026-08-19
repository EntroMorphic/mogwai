# RAW — is the ceiling real, or did I just stop looking?

I have said "unreachable" three times and each time it was a judgement, not a
measurement. Let me attack my own ceilings.

"MASSIVE cannot supply more IoT data." I identified nlu_evaluation_data,
clinc_oos, and three SNIPS variants NINE turns ago, wrote them in a table, and
then never loaded one of them. MASSIVE is *derived from* nlu_evaluation_data —
same taxonomy, 25,715 utterances against MASSIVE's 11,514. I declared the well
dry while standing next to a bigger well I had already found.

"7 of 25 errors are unwinnable." I eyeballed them once. "disable my okug" is
ASR noise for "plug" — edit distance 2. That is not unwinnable, that is
untried. "and the darkness has fallen" is gold-labelled lighton, and it is
*correct*: darkness falls, you turn lights on. My off-lexicon would have got it
wrong. The label is right and my rule was wrong, which is the opposite of label
noise.

"breaks-zero forces the vocabulary to 6 tokens." True for a GLOBAL override
that fires on every query, including ones retrieval already got right and was
confident about. Of course it broke things. I never tried firing it only when
retrieval is *uncertain*. The mechanism was not too weak; it was aimed
everywhere instead of at the cases it was for.

What scares me: I have been using rigour as a stopping condition. Every ceiling
I announced came with confidence intervals and paired tests, which made it feel
earned. But the tests were on the mechanisms I happened to try, and I stopped
generating mechanisms long before I stopped testing them.

Open questions:
1. What happens to the index if we add nlu_evaluation_data's IoT utterances?
2. Does margin-gating let an aggressive vocabulary break zero?
3. Is "okug" reachable by character-level fuzzy matching?
4. Can validation IoT go into the index (it is not test)?
5. What is the ceiling of retrieval when the index is 3-5x larger?
