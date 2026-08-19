# NODES — the grain of the findings

N1. **The noise floor dominated everything.** 220 IoT test examples; SE ~2.2
    points. Only the teacher (+8.2 [+3.2,+13.6]) cleared it. Twenty experiments,
    one signal.

N2. **220 is a hard dataset limit.** MASSIVE's entire test split contains 220
    IoT utterances. No amount of care recovers resolution we cannot buy.

N3. **The mean hides class collapse.** iot_coffee 100% (124 support) vs
    iot_wemo_on 30% (48 support). Reported as "84.5%" for six turns.

N4. **The failure is polarity, not similarity.** iot_wemo_on → predicted
    wemo_off in 5 of 10 cases. "turn on the plug" and "turn off the plug" share
    nearly all their character n-grams.

N5. **Error taxonomy, first time measured.** 33 false actuations (fired on
    out-of-domain), 19 wrong actuations (fired wrong intent), 15 missed. Wrong
    actions outnumber safe abstentions 52:15 — a 3.5:1 bias toward acting.

N6. **The threshold is a bigger lever than any architecture.** Wrong actions:
    107 at th=0.35, 9 at th=0.75. Range of 12x, from one scalar.

N7. **Our operating point is strictly dominated.** th=0.55 beats th=0.47 on
    accuracy (98.1 vs 97.9) *and* on wrong actions (32 vs 50).

N8. **Structure lost, capacity won.** Signatures -8.2, cascade -35, fusion ~0;
    bigger hash +2.7, more index +11.8, bigger teacher +8.2.

N9. **The task is retrieval, not classification.** The winner is 1-NN against
    raw examples. Every attempt to summarise the index into class prototypes
    destroyed accuracy.

N10. **No cost model exists.** Accuracy weights "turned on your coffee machine
     unbidden" equal to "asked you to repeat yourself." For an actuating device
     these differ by orders of magnitude.

N11. **The one significant win is unshippable.** MiniLM 384d, +8.2 points,
     22 MB, will not fit the board.

N12. **The abstention channel is load-bearing.** The cascade lost 35 points
     precisely because its second stage had no way to decline.

## Tensions

T1. **Accuracy vs safety.** They are traded by one scalar and we have never
    chosen the trade deliberately. Optimising accuracy silently chose 3.5:1
    toward acting.

T2. **Architecture search vs operating-point selection.** Twenty experiments on
    the axis that could not move, zero on the axis that moves 12x.

T3. **Retrieval wins but retrieval cannot do polarity.** The method that beats
    everything else has a systematic ON/OFF inversion, and n-gram overlap is
    structurally blind to the distinction.

T4. **Per-class reporting vs sample size.** Class-level numbers are the ones
    that matter for a device, and they are computed on 3–43 examples each.

T5. **The measurable and the important diverged.** Accuracy was legible and
    improvable; actuation cost was neither, so it went unmeasured for six turns.
