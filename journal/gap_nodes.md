# NODES — the grain of the gap

N1. **Honest baseline: 87.3% / 39 wrong / 15 missed.** Retrieval + polarity +
    veto, threshold tuned on dev, test touched once.

N2. **Retrieval is not the constraint.** recall@50 = 99.5%. ~12 points are
    ranking failures.

N3. **The ranker failed, twice.** Hand lexicon: leaked. Auto lexicon from train:
    leakage-free and gains nothing (+0.0 acc, +7 wrong).

N4. **Everything that survived has zero hyperparameters.** Polarity (fixed 7,
    broke 0), veto (fixed 16, broke 0). Significant at every threshold tested.

N5. **Everything that failed had hyperparameters.** Encoders (3 widths), fusion,
    cascades, signature-as-classifier, structural ranker.

N6. **Both survivors break zero cases.** Not "net positive" — strictly
    non-harmful. They only ever convert an action into an abstention, or fix a
    polarity flip that was always wrong.

N7. **The data scale forbids selection.** 769 train / ~140 dev / 220 test IoT.
    Dev cannot resolve the differences we need to choose between.

N8. **Label noise is unquantified.** "turn down the lights to medium" is gold
    lightoff. "please put all the lights" is truncated. The 99.5% ceiling counts
    retrieval only, not recoverability.

N9. **Wrong actions (n=2974) are measurable; iot accuracy (n=220) is not.**
    Two metrics with an order of magnitude difference in resolution.

N10. **MASSIVE cannot supply more IoT test data.** 220 is the entire split.

## Tensions

T1. **Improve vs verify.** Every remaining improvement needs selection; we
    cannot select. Adding capability we cannot validate is indistinguishable
    from adding noise.

T2. **100% as a target vs label noise as a floor.** Chasing the last points
    means fitting mislabelled and garbled examples.

T3. **Zero-parameter mechanisms are validatable but few.** The design space of
    knob-free checks is small and we may have already exhausted the obvious ones.

T4. **More data helps everything, and is the one thing we never tried.** Every
    honest gain this session came from data (my templates -> MASSIVE: +11.8),
    never from architecture.

T5. **The session's own method keeps finding the same answer.** Two LMM cycles
    have now concluded "the constraint is data/measurement, not design." A third
    arriving at the same place is either confirmation or a rut.
