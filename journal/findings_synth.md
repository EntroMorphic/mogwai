# SYNTHESIZE — what to do with what we found

## Decision 1 — Move the threshold to 0.55. Free, unconditional. (minutes)

th=0.47 is **strictly dominated**:

| threshold | accuracy | wrong actions | safe abstentions |
|---|---|---|---|
| 0.47 (current) | 97.9% | 50 | 13 |
| **0.55** | **98.1%** | **32** | 24 |

Higher accuracy *and* 36% fewer wrong actuations. There is no tradeoff to
consider; 0.47 was a dev-set artefact we never checked against the curve.

## Decision 2 — Stop reporting accuracy. Report the operating curve. (hours)

Accuracy is the wrong headline for an actuator. Ship this table instead:

| threshold | accuracy | wrong actions | abstentions | ratio |
|---|---|---|---|---|
| 0.35 | 96.2% | 107 | 7 | 1:0.1 |
| 0.55 | 98.1% | 32 | 24 | 1:0.8 |
| 0.65 | 97.3% | 20 | 59 | 1:3.0 |
| 0.70 | 96.8% | 13 | 82 | 1:6.3 |
| 0.75 | 96.3% | 9 | 100 | 1:11.1 |

**A 12x reduction in wrong actuations is available from one scalar.** That is a
larger effect than every architectural change we tested, combined.

*Choose the point from a stated cost ratio.* If a wrong actuation costs 10x an
abstention, 0.70 is the operating point and the accuracy loss (1.3 points) is
irrelevant. Nobody has stated the ratio; state it before shipping.

## Decision 3 — Add a polarity check. The one justified composite. (hours)

`iot_wemo_on` is predicted as `wemo_off` in 5 of 10 cases. "turn on the plug"
and "turn off the plug" are near-identical in n-gram space; the similarity
metric is structurally blind to the distinction.

Retrieval selects the **action family**; a deterministic rule selects
**polarity**:

    family  = 1-NN over the index          (what we already have)
    polarity= token scan: on|off|up|down|start|stop|kill|enable|disable...
    intent  = family with polarity overridden when a polarity cue is present

This is composite architecture justified by a *measured* failure rather than a
hope — which is exactly what the earlier fusion and cascade attempts lacked, and
why they lost 3 to 35 points.

## Decision 4 — Demote the mean. Report per-class with support. (minutes)

| intent | support | test n | acc |
|---|---|---|---|
| iot_coffee | 124 | 36 | 100% |
| iot_hue_lightup | 76 | 27 | 93% |
| iot_hue_lightoff | 153 | 43 | 84% |
| iot_wemo_off | 52 | 18 | 78% |
| **iot_wemo_on** | 48 | 10 | **30%** |
| iot_hue_lighton | 22 | **3** | *unmeasured* |

Any class under ~20 test examples is **unmeasured**, not low-scoring. Six turns
of "84.5%" concealed a class that inverts polarity half the time.

## Decision 5 — Architecture search is closed. (no work)

Twenty comparisons; one significant result (the 22 MB teacher, unshippable).
Every candidate sat inside one confidence interval on n=220, and MASSIVE's test
split contains no more IoT examples. **Further architecture work requires a
bigger evaluation set, which this dataset cannot supply.**

Keep: char n-gram 1-NN, d=2048, ternary 2-bit index (16x smaller, −1.4 ns).
Drop: signatures, cascades, fusion, all three distilled students.

## The structural finding, for future work

**Structure lost; capacity won.** Signatures (compress 769 → 9): −8.2. Cascade:
−35. Fusion: ~0. Bigger hash: +2.7. More index: +11.8. Bigger teacher: +8.2.

This task rewards *remembering*, not *abstracting* — a closed command set with
repetitive users is a retrieval problem wearing a classification costume. Expect
the same wherever those conditions hold, and stop reaching for classifiers.

## Success criteria for the next cycle

1. A stated cost ratio for wrong-actuation vs abstention.
2. Operating point chosen from that ratio, not from accuracy.
3. Polarity check implemented; iot_wemo_on measured again.
4. Per-class table with support counts is the default report.
5. Zero new architecture comparisons.
