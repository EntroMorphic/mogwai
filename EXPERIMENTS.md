# Experiment log

Every row in `results/RESULTS.tsv` is stamped with the git SHA and whether the
tree was clean. `make compare` appends automatically — tracking is structural,
not a discipline anyone has to remember.

    make fetch     # curl the corpora, record SHA256
    make compare   # build, run, append one row per variant

## Protocol (non-negotiable, learned the hard way)

- **Tune on validation. Touch test once.** Three results this session were
  invalidated by tuning on test — hyperparameters *and* hand-written lexicons.
- **Admit a mechanism only if it breaks zero cases** in a paired McNemar at
  fixed thresholds. Accuracy gains are not evidence; non-destructiveness is.
- **Report per-class with support.** Any class under 20 test examples is
  UNMEASURED, not low-scoring.
- **Report the operating curve, not accuracy.** For an actuator, a false
  actuation and a missed command are not the same cost.

## Findings that survived red-teaming

| finding | evidence |
|---|---|
| Parameter-free retrieval ~= 22MB transformer | 90.5% vs 91.8% MiniLM, real MASSIVE IoT |
| Polarity check is non-destructive | fixed 7/7/6/5, broke 0, McNemar p=0.016-0.06 |
| Signature veto is non-destructive | fixed 16/12/9/6, broke 0, p=0.0000-0.03 |
| Signatures classify badly, detect well | -8.2 pts as classifier; -35% wrong actions as veto |
| Data beats architecture, every time | +11.8 corpus swap; +2.3 from 478 NLU utterances |
| In-distribution only | CLINC/SNIPS negatives cost 3.1 pts |
| Ternary buys 16x footprint, 120x speed | 24.7MB/766ms float -> 1.5MB/1.6ms |
| Twin-ternary > binary on sparse hashes | 80.5% vs 74.1%, +6.4 pts for 1 extra bit/dim |

## Invalidated (kept as negative results)

| claim | why it failed |
|---|---|
| 13/16 routing | test contamination + threshold tuned on test -> 5/10 honest |
| structural ranker +4.5 pts | lexicon written from test failures; 4 hyperparams on test |
| decisive vocabulary (303 tok) | broke 28 wrong actions, fixed 4 |
| margin-gated vocabulary | breaks 0 on dev, failed test confirmation (59 dev IoT) |
| "96.8% recoverable" | assumed diagnosable == fixable; rules that fix also break |
| "MASSIVE is exhausted" | 118 validation IoT unused; NLU-Eval never loaded |

## Open

1. C is 10 pts behind the Python result (80.5 vs 90.5). Prime suspect: L1 vs L2
   normalisation changes which dims clear the centre.
2. `om[d]*4 >= cnt` in the signature mask is an unjustified constant.
3. Never measured on the ESP32. The board still runs an archived encoder.

## The acceptance test was wrong (2026-08-19)

"Admit a mechanism iff it breaks zero cases" is **necessary but not
sufficient**. A mechanism that only ever converts wrong actuations into
abstentions will *always* show broke=0, and will therefore always appear to
pass — while contributing nothing.

The correct test is **curve dominance**: does the (wrong actuations, missed)
frontier move, or does the system merely slide along the frontier it already
had? A threshold slides along it for free.

| mechanism | breaks zero | moves the curve |
|---|---|---|
| twin-ternary vs binary | no (broke 3) | **yes, +5.2 pts** |
| signature veto (hash-derived) | yes | no — inert at every attenuation |
| retrieval cascade + text rerank | yes | no — identical on every axis |
| word prior (lift, text-derived) | yes, at all 5 thresholds | **no** — 3 tie / 2 lose / 3 win by 1-2 |

All three rejected mechanisms passed breaks-zero. Only the representation
change moved the frontier.

## Prior-signal separation (the-reflex, EntroMorphic)

Tested the architecture from `github.com/EntroMorphic/the-reflex`: prior-holder,
evidence-reader, structural wall, disagreement detector, evidence-deference.

Finding: the framework needs an unstated **component (0) — the prior must be
derived from information the evidence channel cannot represent.** Our first
prior was a majority-bit summary of the same hash vectors retrieval scans; it
was inert at every attenuation setting because it structurally could not
disagree. A prior that mirrors the evidence channel is not a wall, it is a
mirror.

Rebuilt from words (which char-n-gram hashing provably destroys: order-
insensitive, collision-prone). Raw P(c|word) collapsed to the majority class —
"none" holds 88% of the index, so it won every vote; the prior was 11.4%
correct. Lift against the class base rate fixed it: **11.4% -> 82.9%** standing
alone, one line of arithmetic.

And it still did not move the curve. Retrieval over 10,500 real utterances
already knows what the words say. On the ESP32 the prior carries *temporal*
information the classifier structurally cannot see; ours carried lexical
information retrieval had already indexed. Separation is necessary, an
independent channel is necessary, and neither is sufficient if the channel is
redundant.

## What the ESP32's silicon can actually take off the CPU

Asked after the profile: which of this work can be handed to hardware?
Surveyed, then measured rather than asserted (both prior perf hypotheses were wrong).

| Peripheral | Verdict |
|---|---|
| **Second core** | **Taken. 1.97x, parity exact.** |
| SIMD / popcount instr | Absent on Xtensa LX6. The 63% has no hardware home. |
| AES/SHA/RSA accel | Wrong primitives. Encode is 0.05% anyway. |
| ULP coprocessor | 8-instruction FSM. Cannot. |
| DMA -> parallel-out -> GPIO loopback -> PCNT (GIE-style hardware bit-count) | **Priced and rejected.** PCNT counts *edges*, capping a full scan at 268 ms vs 78.8 ms on the CPU. The XOR would still need the CPU, so it adds a stage rather than removing one. Viable on S3/C6 with PARLIO; not on classic ESP32. |

Dual-core scan: index halved by range, no shared state, no ordering constraint —
the scan is embarrassingly parallel. Acquire/release fences around the job
descriptor; worker pinned to core 1 via task notification.

    one core   78855 us
    two cores  40093 us   (1.97x)
    parity     64/64 class and score, device-2core == device-1core == host

**Cumulative on-device: 200.4 -> 102.1 (clocks) -> 78.8 (precomputed active) -> 40.1 ms.**
Every step held PARITY EXACT.

**The remaining cost is software, not hardware.** Post-split the breakdown is
unchanged in proportion: popcount ~63%, flash loads ~37%. Neither has a
peripheral that beats the CPU on this part. The open levers are all code —
popcount byte-LUT in IRAM, d=128, pruning the index to the ~1,300 IoT entries.
