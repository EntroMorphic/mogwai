# LMM journal — 6 cycles, 24 artifacts

Applications of the **Lincoln Manifold Method** ([github.com/anjaustin/lmm](https://github.com/anjaustin/lmm)),
run at decision points where the next step was genuinely unclear. Four phases
per cycle, each a separate file:

    RAW        everything observed, unfiltered, no interpretation
    NODES      the observations decomposed into discrete claims
    REFLECT    each claim interrogated — what would make it false?
    SYNTHESIZE what to actually do

Read `*_synth.md` for conclusions; the earlier phases are the working, kept
because the reasoning is the point.

| cycle | question | what came out |
|---|---|---|
| `esp32_nl_control` | Is an LLM on an MCU the right shape for this at all? | No. The router path — no training, no float, no parameters |
| `findings` | What do we do with what we found? | The encoder was archived in favour of the router |
| `reach` | How far can this approach actually go? | Composite/cascade architectures — later measured inert and cut |
| `gap` | How do we close the accuracy gap without fooling ourselves again? | Curve dominance over "breaks zero"; the acceptance test was wrong |
| `leak` | The dev split was 75.6% leaked. Why, and how is it made impossible? | Assertions that abort, not warn. `c/src/invariants.c` |
| `tt` | Twin-ternary underperformed base-3 expectations — what is off? | The leak was masking it; the representation was fine |

## Two of these produced things still in the shipping system

`leak` produced `invariants.c` — the abort-don't-warn assertions that later
caught two further leaks nobody suspected (MASSIVE train repeats utterances;
train and test share utterances).

`gap` produced the acceptance rule that a change must move the (wrong, missed)
**frontier**, not merely fix cases without breaking any. That rule later
overturned the index-pruning result, which looked positive at a single tuned
point and is not.

## And one produced a negative result

`reach` argued for composite signature-cascade architectures. Built, measured,
found identical on every axis, and cut. `cascade.c` is retained; the finding is
that it does nothing. See EXPERIMENTS.md.
