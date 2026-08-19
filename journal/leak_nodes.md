# NODES

N1. **Twin-ternary contains binary.** All-ones mask reduces exactly to binary.
    Superset losing = measurement error, by construction.

N2. **I had the guarantee and did not use it.** Reported the anomaly as a
    finding and planned next steps around it.

N3. **The leak: 75.6% of dev IoT utterances present verbatim in the index**
    via NLU-Eval, which MASSIVE derives from.

N4. **Cause: dev items were never hs_add'd.** The dedup set existed; the new
    dev-carving path bypassed it.

N5. **Dev read 99.5%.** I called it "too good" in the same message I reported
    it, and took no action.

N6. **Four decisions voided:** d=256, mask=none, veto=off, Dice validation.

N7. **The veto was rejected on leaked evidence** after passing a paired
    McNemar in Python. Clean dev shows a trade, not harm.

N8. **Clean dev restores the expected ordering:** binary 78.2 +-3.0,
    twin-ternary 87.0 +-2.4. ~2.3 SE. Theory and measurement agree again.

N9. **Third variant of one failure.** Python: lexicon fitted to test. C:
    iterated against test. C: dev contaminated. Same shape.

N10. **The test evaluation is spent and its config was leak-derived**, so the
     71.4% describes a configuration we would not have chosen.

## Tensions

T1. **Guards keep being built for the previous instance.** hs_add existed to
    stop exactly this and a new code path walked around it.

T2. **"Too good to be true" is a signal I emit but do not act on.** Naming a
    suspicion is not the same as testing it, and it feels like diligence.

T3. **Spent test budget vs voided config.** We paid for a test number that
    measures a configuration chosen by leaked evidence.

T4. **Structural guarantees are cheap checks and I do not use them.** Superset
    relations, monotonicity, bounds — each is a free assertion.
