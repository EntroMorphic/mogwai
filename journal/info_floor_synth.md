# Synthesis: Finding The Intrinsic Information Floor

## Decision

Treat `21` as the current **observed prefix-code floor**, not the intrinsic
semantic information floor. Keep it pinned, then run targeted diagnostics to
separate scoring policy, bit ordering, and true capacity.

## Current Facts

| Measurement | Result |
|---|---:|
| Factor floor | `1/32` masks pass |
| Required factors | `pol+color+comp+loc+support` |
| Prefix bit floor | `21` bits |
| Last failing width | `20` bits |
| Final failing case | `clean the flat -> make coffee` |
| Topology runtime need | none |

## Planned Experiments

### 1. Fit-Coefficient Sweep

Add a diagnostic that evaluates:

```text
S(q,c) = code_score(q,c) + alpha * c.score
```

for fixed bit widths, especially `20`, across a small grid of `alpha` values.
Current `alpha` is `1/8`.

Success criteria:

| Outcome | Interpretation |
|---|---|
| 20 bits passes with smaller alpha | floor is partly scoring-policy-induced |
| 20 bits fails at alpha = 0 | prefix lacks the cleaning/coffee distinction |
| many cases change | candidate fit is structurally important, not just a bias |

### 2. Leave-One-Out At 21 Bits

At `CODE_BITS=21`, remove one bit position at a time and rerun frozen + Holdouts
A/B/C.

Success criteria:

| Outcome | Interpretation |
|---|---|
| one bit breaks only cleaning/coffee | likely bit-order artifact |
| many bits break different cases | 21-bit prefix is broadly load-bearing |
| no removals break | redundancy exists despite prefix floor |

### 3. Selected-Bit Floor

Search for a passing subset of bits not constrained to be a prefix. Start with a
greedy deletion pass from 64 bits, then optionally a greedy construction pass.

Success criteria:

| Outcome | Interpretation |
|---|---|
| subset < 21 passes | intrinsic floor is below prefix floor |
| no subset < 21 found | 21 becomes more plausible as an information floor |

## Do Not Do Yet

- Do not change runtime scoring.
- Do not lower `CODE_BITS` in production paths.
- Do not add a `cleaning` factor unless repeated blind failures show a reusable
  relation that belongs outside semhash.
- Do not optimize storage before selected-bit and alpha diagnostics are known.

## Bedrock Criterion

Call an intrinsic information floor only when all are true:

- prefix floor is known,
- fit-coefficient sweep cannot lower it materially,
- leave-one-out shows distributed bit load,
- selected-bit search cannot find a smaller passing subset,
- new blind semantic-code probes do not collapse the conclusion.

Until then, the honest statement is:

```text
Observed prefix-code floor: 21 bits.
Intrinsic semantic information floor: unresolved.
```

## Execution Result

Two diagnostics now resolve the first-order ambiguity:

```text
RUNTIME_CHOICE_FIT_SWEEP bits=20 passing=1/7
RUNTIME_CHOICE_BIT_LOO bits=21 passing=20/21
```

At 20 bits, `alpha=0` passes and every tested nonzero candidate-fit weight
fails. At 21 bits, omitting any earlier bit still passes, while omitting bit `20`
fails. The 21-bit result is therefore a current prefix/scoring artifact, not an
intrinsic semantic information floor.

## Outcome Table

| Probe | Setting | Result | Interpretation |
|---|---:|---:|---|
| `--bit-floor` | prefix bits `1..64` | `min_bits=21`, `passing=44/64` | Current runtime score needs 21 leading bits. |
| `--bit-forensic` | width `20` | `clean the flat -> make coffee` | The last failure is semantic-code separation, not topology or referent binding. |
| `--fit-sweep` | `20` bits, `alpha=0` | pass | Code alone is sufficient at 20 bits on this pinned universe. |
| `--fit-sweep` | `20` bits, any tested `alpha>0` | fail | Candidate self-fit introduces the final error. |
| `--bit-loo` | `21` bits, omit `0..19` | pass | Earlier bits are not individually necessary. |
| `--bit-loo` | `21` bits, omit `20` | fail | The decisive distinction sits in one late prefix bit. |

## Recommendation

Keep `21` pinned as the safe serialized-prefix width under the current scorer,
but do not call it an information floor. The next floor to measure is selected
bit capacity:

1. Rank bits by correct-vs-runner-up margin contribution across all pinned cases.
2. Greedily construct the smallest bit subset that preserves frozen + Holdouts
   A/B/C.
3. Repeat the candidate-fit sweep on that selected subset.

Until selected-bit diagnostics exist, do not lower runtime width, remove
candidate fit globally, or add a cleaning/coffee factor. The current result is a
scientific clarification: prefix width mostly measured serialization order and
ranking policy, not semantic capacity.

## Updated Claim

```text
Observed serialized-prefix floor under current ranking: 21 bits.
Observed 20-bit code-only pass: yes.
Observed distributed 21-bit load: no.
Intrinsic semantic information floor: unresolved; likely below 21.
```
