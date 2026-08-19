# RAW — how do we close the gap to 100%?

Honest state: 87.3% iot accuracy, 39 wrong actuations, 15 missed. Retrieval
ceiling 99.5% at K=50. So ~12 points sit in ranking, and I just failed to close
them — the structural ranker looked like +4.5 and turned out to be me fitting
test failures into a lexicon and then tuning four hyperparameters on the same
test set. Under honest protocol the leakage-free version gained exactly nothing.

What scares me. There is a pattern in what survives and it is not about
cleverness. Polarity: zero hyperparameters, fixed 7, broke 0. Signature veto:
zero hyperparameters, fixed 16, broke 0. Both significant at every threshold.
Now the list of what died: distilled encoders at three widths (four
hyperparameters each), feature fusion (weights), cascades (gate thresholds),
signature routing as classifier (tile count), the structural ranker (alpha, K,
threshold, veto mode). Every single failure had knobs. Every single success had
none. That cannot be a coincidence and I do not think it is about the ideas.

The uncomfortable reading is that at 769 train / 140 dev / 220 test IoT
examples, anything with a fitted parameter gets fitted to noise, and we cannot
tell the difference because the instrument that would tell us is the same size.
We do not have a modelling problem. We have a measurement problem wearing a
modelling problem's clothes.

What's probably wrong with my first instinct. My instinct is to build a better
ranker. But a better ranker needs to be selected, and selection is exactly the
step we cannot do. The instinct is to add capability when the constraint is
verification.

The other thing I keep avoiding. Is 100% even the right target? "turn down the
lights to medium" is gold-labelled lightoff. "please put all the lights" is
truncated ASR. If some percentage of the 220 is unrecoverable, then the ceiling
is not 99.5% and chasing 100% means fitting noise deliberately. I have never
measured how much of the residual is unwinnable, and I have been quoting a
99.5% ceiling that only accounts for retrieval, not for labels.

Open questions:
1. What fraction of the 220 is actually unwinnable (label noise, ASR garbage)?
2. Is there a zero-parameter ranker, or is ranking inherently parametric?
3. Can we get more validation data without more test data?
4. Why do zero-parameter mechanisms break zero cases — is that structural?
5. Should we be closing the gap at all, or reporting it honestly and stopping?
