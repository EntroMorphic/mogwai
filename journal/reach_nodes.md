# NODES

N1. **Untried data exists.** nlu_evaluation_data (25,715 utts, same taxonomy,
    MASSIVE's parent), clinc_oos, SNIPS. Identified turn 9, never loaded.

N2. **Validation IoT is index-eligible.** ~140 utterances, not test, currently
    used only for threshold selection.

N3. **The vocabulary fired globally.** It overrode confident-correct
    retrievals. Breaking things was structural, not a strength problem.

N4. **Margin is computable and unused.** top-1 minus top-2 similarity is a free
    uncertainty signal we have never exploited.

N5. **"Unwinnable" was one pass of eyeballing.** At least 3 of 7 are
    challengeable; one ("darkness has fallen") was my rule being wrong, not the
    label.

N6. **Fuzzy matching untried.** "okug"->"plug" is edit distance 2.

N7. **The full-negative index cut wrong actions 38% with zero new mechanism.**
    The one data lever we did pull paid immediately.

N8. **Retrieval recall is 99.5% at 769 iot examples.** Unknown what it is at
    3000.

## Tensions

T1. **Rigour as brake vs rigour as steering.** Confidence intervals told me
    what I could not distinguish; I let that stop generation instead of
    redirecting it.

T2. **Global mechanisms vs targeted mechanisms.** Breaks-zero is nearly
    impossible for a global rule and nearly free for one gated to the uncertain
    cases.

T3. **Cross-dataset transfer is unknown.** nlu_evaluation_data may have
    different phrasing conventions; more data could add noise.
