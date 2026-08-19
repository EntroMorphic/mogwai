# SYNTHESIZE — closing the gap, without fooling ourselves again

## The retarget

| | |
|---|---|
| current (honest, dev-tuned, test once) | **87.3%** iot, 39 wrong, 15 missed |
| retrieval ceiling (recall@50) | 99.5% |
| **recoverable ceiling** (7 of 220 unwinnable) | **~96.8%** |
| **real gap** | **~8 points, not 12** |

Unwinnable examples, for the record: `"and the darkness has fallen"`,
`"disable my okug"`, `"would you love to see"`, `"please put all the lights"`,
`"turn down the lights to medium"` (gold-labelled `lightoff`; it is `dim`),
`"light color for study room"` (gold `lightup`; text says colour).

## Decision 1 — Change the acceptance test. This is the whole method.

**Admit a mechanism if and only if it breaks zero cases** in a paired McNemar at
four fixed thresholds (0.45/0.50/0.55/0.60), on n=2974. Do not admit on accuracy.

*Why this works where selection does not:* proving non-destructiveness is a
one-sided test against an expected-zero null and needs only enough data to
observe a counterexample. Selecting the best of N candidates needs resolution
proportional to the gaps between them, which 220 examples do not provide.

*Consequence:* a mechanism may be admitted even if its accuracy gain is not
significant, because it cannot cost anything. This is the opposite of the rule
that produced the ranker fiasco.

## Decision 2 — Treat the gap as VOCABULARY, not ranking.

The ~18 winnable errors decompose into five families:

| family | evidence | knob-free check |
|---|---|---|
| off-idioms | "cease", "no lights", "go out", "sleep" | token → force `off` |
| colour | "pink", "red ish", "colors", "dim color" | colour token → force `change` |
| magnitude | "lower", "a lower setting", "dim" | token → force `dim` |
| appliance nouns | "roomba", "suck out the dust", "coffee pot" | noun → force device |
| light-up idiom | "light up the lights" | phrase → `on`, not `up` |

Each is a deterministic override with **no parameter to tune**, so each is
admissible under Decision 1 or rejected outright.

## Decision 3 — Build the vocabulary from TRAIN, then verify on TEST.

The lexicon must be derived from the 769 training utterances (log-odds per
class, as in `archive/structural_ranker/ranker_auto.py`), **never by reading
test errors**. Test is then used once, only to confirm breaks-zero.

*Guard:* if a term cannot be justified from train, it does not go in — even if
it would obviously fix a test case I have already seen. I have seen the test
errors; that knowledge is contaminated and must not enter the artefact.

## Decision 4 — Report against the recoverable ceiling.

Headline becomes: **87.3% of 96.8% recoverable**, with the operating curve
(n=2974) as the primary table and iot accuracy (n=220) as secondary, flagged
low-resolution. Any class under 20 test examples stays UNMEASURED.

## Explicit non-goals

- **No more selection among architectures.** Closed two cycles ago; the data
  cannot support it.
- **No tunable ranker.** Four hyperparameters on 140 dev examples is how we got
  here.
- **Not 100%.** ~7 of 220 are unrecoverable; targeting them means fitting noise.

## Success criteria

1. Every admitted mechanism has zero hyperparameters.
2. Every admitted mechanism shows **broke 0** at all four thresholds.
3. Lexicon terms are traceable to train frequency, not to test inspection.
4. Progress reported against 96.8%, not 100%.
5. If nothing passes the breaks-zero bar, we ship at 87.3% and say so.
