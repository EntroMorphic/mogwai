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

## Popcount table: killing the 47%

Xtensa LX6 has no popcount instruction, so `__builtin_popcount` compiles to a
~15-op SWAR sequence, and `t_dot` runs 16 of them per vector. The profile put
that at 47% of scan time — the largest single cost, and the one with no
hardware home.

**Placement constraint:** the table must live in **DRAM, not IRAM**.
ESP32-classic IRAM permits only 32-bit aligned access, so a byte load from it
faults. `DRAM_ATTR` also keeps it out of `.rodata`, which would land in flash
and be read back through the cache MMU — defeating the point.

Verified **exhaustively**: the table equals `__builtin_popcount` on all 2^32
words (`c/test/t_popcnt.c`), so the swap cannot change any result. The
algebraic rewrite `dot = pc(both) - 2*pc(both&diff)` was checked against the
original two-mask form over 2M random vector pairs.

| variant | 1 core | 2 cores | DRAM |
|---|---|---|---|
| builtin SWAR | 77.5 ms | 39.5 ms | 0 |
| **8-bit table (shipped)** | **43.8 ms** (1.77x) | **25.9 ms** | **256 B** |
| 16-bit table | 38.5 ms (2.01x) | 25.8 ms | 64 KB |

All three held PARITY EXACT (64/64 class and score vs host).

**The 16-bit table was rejected**, and the reason is the useful finding: it is
12% faster on one core but *identical* on two (25.80 vs 25.85 ms). With the
popcount cost gone the dual-core scan is **no longer compute-bound** — both
cores now contend for the same flash cache. 64 KB of DRAM for 0.05 ms is a bad
trade, and it says the next bottleneck moved.

**Cumulative on-device: 200.4 -> 102.1 -> 78.8 -> 40.1 -> 25.9 ms (7.7x).**

Also made `esp32_router/main/{router,ternary}.{c,h}` symlinks into `c/src/`.
They were hand-copies and had already drifted once; host and device now cannot
diverge by construction.

### Red-team of the popcount result

Six attacks. Four found something.

**1. n=1, no error bars.** The claim "16-bit table is identical on two cores"
came from 25.80 vs 25.85 ms, single-shot. Same reasoning that produced the
"+2.7 Dice was 1.1 SE" retraction. Remeasured, 15 repeats, min/median/max:

    full index   TPOPCNT=1   1 core 43439/43440/43440   2 cores 25749/25749/25750
    full index   TPOPCNT=2   1 core 38193/38193/38194   2 cores 25750/25751/25752

Spread is 1-2 us. The claim SURVIVES, far more strongly than it was made:
25749 vs 25750 against a 1 us spread.

**2. Mechanism inferred from a null result, never tested.** "Identical, therefore
flash-bound" is an inference, not evidence. Tested directly against a
cache-resident index (400 vectors, 25 KB, fits the 32 KB cache):

    SMALL index  TPOPCNT=1   1 core   747   2 cores   366   (2.04x)
    SMALL index  TPOPCNT=2   1 core   543   2 cores   256   (2.12x)

Cache-resident, the 16-bit table IS faster on two cores (256 vs 366, 1.43x) and
scaling is ~2x. Flash-resident, both variants hit the same wall at 25.75 ms no
matter how fast the arithmetic. **Flash contention confirmed by direct test.**

Quantitatively: 2-core full scan = 25749 us / 10500 vec = 588 cycles/vector
aggregate, against a single core's *touch-only* cost of 646 cycles/vector. Two
cores in aggregate barely exceed one core's raw memory throughput — the flash
cache MMU is a shared, serialising resource.

**3. Confound: is degraded scaling just sync overhead?** Scaling fell 1.97x ->
1.69x as compute got cheaper, which fixed dispatch cost would also produce.
Measured directly (dispatch+join with zero work): **29 us**. The full-index gap
from ideal is ~4000 us, 138x larger. Not sync. Confound eliminated.

**4. DRAM placement was asserted, not verified.** Checked the symbol:
`PC8 @ 0x3ffb167c`, section `.data` — internal SRAM. And the counterfactual,
proving the attribute is load-bearing rather than cargo cult:

    static const uint8_t WITHOUT[256]     -> .rodata   (linker-mapped to FLASH)
    static DRAM_ATTR const uint8_t WITH[] -> .dram1    (internal SRAM)

**5. `make compare` was a silent no-op.** It listed prerequisites but carried no
recipe — the recipe was attached to `test:` alone. It exited 0 and printed
nothing: a validation command that passed without validating. Fixed; `make test`
now refuses (it is a habit-typo for a budgeted resource) in favour of explicit
`make compare` (dev) and `make testset` (burns one test evaluation).

**6. Accuracy drift.** Re-ran the host comparison on the LUT path:
85.9% +-2.5 iot, 14 wrong, 14 missed, 656 KB — identical to the recorded config,
as the exhaustive 2^32 proof requires.

**Consequence for the remaining levers.** The bottleneck is now *bytes scanned*,
not arithmetic. This re-ranks everything: index pruning and d=128 attack the
real constraint; further arithmetic optimisation is worthless on two cores.
