# Experiment log

Every row in `results/RESULTS.tsv` is stamped with the git SHA and whether the
tree was clean. `make compare` appends automatically — tracking is structural,
not a discipline anyone has to remember.

    make fetch     # curl the corpora, record SHA256
    make compare   # build, run, append one row per variant

## Look up a fact

| question | answer | where |
|---|---|---|
| What ships? | twin-ternary, d=256, 10500 vectors, 656 KB, threshold 126 | [Shipped threshold](#shipped-threshold-moved-136---126) |
| How fast on device? | 43.5 ms 1-core / 26.7 ms 2-core, PARITY EXACT | [Popcount table](#popcount-table-killing-the-47) |
| What predicts latency? | 59.8 ns/byte + 326 ns/vector; bytes are 92% | [Cost model](#what-the-failed-lever-bought-a-resolved-cost-model) |
| Is 2-bit really better than 1-bit? | Yes, at **matched bytes**, on the whole curve | [Red-team of test eval](#red-team-of-test-evaluation-2) |
| What is the held-out number? | 84.1% ±2.5 — **at threshold 136, not the shipped 126** | [Test eval #2 result](#test-evaluation-2--result) |
| Why isn't accuracy the headline? | It is recall; it cannot see false actuations | [Metric is blind](#the-accuracy-metric-is-blind-to-false-actuations--read-every-number-above-with-this-in-mind) |
| How good could this ever get? | ~98%; some labels are wrong, some are unknowable | [Residual errors](#what-the-residual-errors-actually-are) |
| What did we get wrong? | [Invalidated](#invalidated-kept-as-negative-results), and every `Red-team of…` section |

## Contents

**Ground rules**
- [Protocol (non-negotiable, learned the hard way)](#protocol-non-negotiable-learned-the-hard-way)
- [Findings that survived red-teaming](#findings-that-survived-red-teaming)
- [Invalidated (kept as negative results)](#invalidated-kept-as-negative-results)
- [Open](#open)
- [The acceptance test was wrong](#the-acceptance-test-was-wrong-2026-08-19) — why "breaks zero" is not acceptance
- [Prior-signal separation (the-reflex, EntroMorphic)](#prior-signal-separation-the-reflex-entromorphic)

**Making it fast on the ESP32** — 200.4 → 43.5 ms, parity held at every step
- [What the silicon can actually take off the CPU](#what-the-esp32s-silicon-can-actually-take-off-the-cpu) — second core yes; peripheral popcount priced and rejected
- [Popcount table: killing the 47%](#popcount-table-killing-the-47) — 256 B in DRAM, verified over all 2³² words
  - [Red-team](#red-team-of-the-popcount-result) — the dual-core scan is flash-bound, tested not inferred

**Three levers, and what each actually cost**
- [Lever 1: pruning the index](#lever-1-pruning-the-index) — condensation beats random; nothing beats the baseline
  - [On hardware: the byte → latency law](#lever-1-on-hardware-the-byte---latency-law) — predicted to 0.4% on one core
  - [Red-team](#red-team-of-lever-1) — found a real shipping bug: `mkblob` guessed the threshold
- [Lever 2: d=128 — REJECTED](#lever-2-d128--rejected) — dominated at every operating point
  - [What the failed lever bought](#what-the-failed-lever-bought-a-resolved-cost-model) — the cost model
  - [Red-team](#red-team-of-lever-2) — the smoothing confound was real, and my fix was backwards
- [Lever 3: QIO flash — ALREADY TAKEN](#lever-3-qio-flash--already-taken-my-earlier-note-was-wrong) — it was on the whole time

**The held-out evaluation**
- [Pre-registered prediction](#test-evaluation-2--pre-registered-prediction-written-before-running) — committed before the run
- [Result](#test-evaluation-2--result) — 84.1% ±2.5
  - [Bug found in the budget guard while spending it](#bug-found-in-the-budget-guard-itself-while-spending-it)
  - [Red-team](#red-team-of-test-evaluation-2) — the missing size-matched control; scoring corrected to 3/4

**Choosing the operating point**
- [Raising the threshold does not buy precision](#raising-the-threshold-for-precision--the-knob-does-not-do-what-i-claimed)
  - [Correcting my own claim](#correcting-my-own-claim)
  - [What the residual errors actually are](#what-the-residual-errors-actually-are) — the corpus ceiling
- [Shipped threshold moved 136 → 126](#shipped-threshold-moved-136---126)
- [The accuracy metric is blind to false actuations](#the-accuracy-metric-is-blind-to-false-actuations--read-every-number-above-with-this-in-mind) — read this before quoting any number above

> Sections are chronological, so later ones sometimes **overturn** earlier ones.
> Where that happens the earlier text is left standing with the correction
> recorded beneath it — the reversal is the finding. Nothing here is retracted
> silently.

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

## Lever 1: pruning the index

Bytes scanned is latency (flash-bound, above), so the index is the target.
10500 vectors, 656 KB, of which ~1155 are IoT and ~9345 are "none" negatives.

**The free prune yields nothing.** Exact-duplicate twin-ternary codes with
identical labels can never change an argmax, so dropping them is lossless.
There are **zero** of them: twin-ternary at d=256 is injective across all 10500
entries. Worth noting as a risk for lever 2 — d=128 may start colliding.

**The naive comparison inverted the answer.** Subsampling negatives and reading
the auto-tuned row suggests pruning *helps* wrong actuations (14 -> 11 at
iot-only). It does not. The threshold is retuned per run, so those rows sit at
different operating points. At MATCHED points the baseline strictly dominates
every pruned variant on both axes:

    min MISSED at wrong<=      8    12    16    20    24     KB
      baseline                46    23    12     6     5    656
      cnn                     50    27    14    12     5    418
      cnn+2                   60    40    20    14    12    245
      1-in-2                  60    40    20    14    12    364
      1-in-8                  60    40    27    20    15    145
      iot-only                60    40    29    25    20     72

    min WRONG at missed<=      8    12    16    20    24
      baseline                18    16    14    13    11
      cnn                     22    19    16    15    13
      iot-only                96    45    31    23    21

This is the "curve dominance, not breaks-zero" rule doing exactly the work it
was written for — and this time it overturned the single-point reading rather
than merely confirming it.

**Condensation beats random subsampling, and by a clean margin.** A negative
earns its 64 bytes only if it is the nearest neighbour of something. Leave-one-
out over the INDEX ONLY (the index is train, so this leaks nothing) marks 3816
negatives as nobody's neighbour. Dropping them: 656 -> 418 KB, 36% fewer bytes.
`cnn+2` at **245 KB** matches random `1-in-2` at **364 KB** on every cell — the
same frontier for a third fewer bytes.

**But nothing dominates the baseline, and the honest verdict is "not proven".**
cnn costs +2 missed and +2 wrong at matched points. On 220 IoT dev items that is
~0.9 points against a +-2.5% standard error (~5.5 items) — inside the noise. It
is neither demonstrated harmful nor demonstrated free. Recorded as an available
36% byte cut whose cost is below what this dev set can resolve, NOT as a win.

### Lever 1 on hardware: the byte -> latency law

`prune_index()` factored into `c/src/prune.{c,h}` and called by BOTH `compare`
(which measures it) and `mkblob` (which ships it). One implementation on
purpose: the two already keep separate copies of the index build, and the esp32
tree had drifted from `c/src` once. If harness and exporter prune differently,
on-device parity measures nothing. Refactor verified behaviour-preserving —
baseline 85.9/14/14/656 unchanged, condensed identical to the inline version.

Three index sizes flashed and measured, 15 repeats each:

    vectors   KB    1-core ms   us/vector   2-core ms   speedup
     10500   656      43.44       4.137       25.75      1.687
      6684   418      27.57       4.125       17.63      1.564
      3920   245      16.22       4.138        9.95      1.630

**Single core: the law is exact.** Cost per vector is constant to +-0.3% across
a 2.7x range of index sizes. Predicted 27.68 ms for the condensed index from
the baseline alone; measured 27.57 (0.4% error). The flash-bound model is
quantitative and predictive, and the intercept is zero — there is no meaningful
fixed cost.

**Two cores: NOT proportional, and a two-point fit lied.** From the first two
points I fitted a ~3.4 ms fixed term. The third point refutes it: the implied
slope is 2.128 us/vector between points 1-2 but 2.777 between 2-3, and speedup
moves non-monotonically (1.687, 1.564, 1.630). Contention between the two cores'
flash streams depends on working-set size in a way the simple model does not
capture. Recorded as unexplained rather than rationalised.

This matters less than it looks: **core 1 belongs to WiFi on any real device, so
the single-core number is the production number — and that is the one that
obeys the law.** Index size now predicts latency directly: ~4.13 us per vector.

PARITY EXACT held at all three index sizes (64/64 class and score).

**Shipping default stays the full 10500-vector index**, since it dominates the
dev curve; the condensed index is one `mkblob --prune-dup --prune-cnn` away and
buys 1.58x single-core (43.4 -> 27.6 ms) at a cost this dev set cannot resolve.

### Red-team of lever 1

**1. A real shipping bug: `mkblob` hardcoded `threshold=136`.** The threshold is
tuned on dev by `compare` and is NOT invariant under pruning:

    baseline                          th=136
    --prune-cnn                       th=136
    --prune-cnn --prune-neg=2         th=146
    --prune-neg=8                     th=154

The cnn+2 blob flashed in the previous section therefore ran at 136 instead of
146 — an operating point the harness never measured. **PARITY EXACT still
passed**, because the host reference queries were computed with the same wrong
value. That is the blind spot worth naming: *parity proves host == device, it
does not prove either is correct.* Fixed: `mkblob` now refuses to guess and
errors out telling you to run `compare` and pass `--threshold=N`. Verified the
unpruned blob is byte-identical to the shipped one.

**2. Content/size confound.** The three flashed indexes differed in content as
well as size, so byte-proportionality could have been a content effect (branch
behaviour in the argmax update). Tested directly: same blob, same vectors, only
N varies.

    N=1000  ( 62 KB)  4153 ns/vector
    N=2000  (125 KB)  4147
    N=4000  (250 KB)  4143
    N=6000  (375 KB)  4142
    N=8000  (500 KB)  4142
    N=10500 (656 KB)  4142

Flat to 0.26% across a 10.5x range, converging upward as a ~11 us fixed
per-query cost amortises. The three pruned blobs — completely different content —
measured 4125-4138 ns/vector, the same line. **Confound eliminated: the law is
about size, not content.**

**3. What parity does not cover.** A prune that dropped the wrong vectors would
still show PARITY EXACT, since host and device prune with the same shared code.
Prune correctness is established by the host accuracy/curve runs, not by parity.
Recorded so the two are not conflated later.

## Lever 2: d=128 — REJECTED

**The collision hypothesis was wrong.** I predicted d=128 might start colliding
(distinct commands mapping to identical codes, a sharper failure than gradual
degradation). Measured with `--prune-dup`: **zero collisions at d=64, 128, 256
and 512.** Twin-ternary stays injective on this corpus even at 16 bytes/vector —
128 bits is ample entropy for 10500 items. Hypothesis tested and refuted; the
failure mode is something else.

**d=128 is comprehensively dominated on the curve**, not merely worse at the
tuned point:

    min MISSED at wrong<=      8    12    16    20    24     KB
      d=128                   80    50    36    27    19    328
      d=256                   46    23    12     6     5    656
      d=512                   46    25    15    12     4   1312

    min WRONG at missed<=      8    12    16    20    24
      d=128                   --    --    26    24    24
      d=256                   18    16    14    13    11
      d=512                   21    20    16    14    13

At `wrong<=16` d=128 misses 36 against d=256's 12; at `missed<=12` it is
unreachable at any threshold. Not a trade — a strict downgrade.

**Two useful corollaries.**

*d=256 dominates d=512* almost everywhere at half the bytes, re-deriving on a
leak-free split one of the four decisions the dev leak had voided.

*Condensation dominates dimension reduction on BOTH axes:*

      d=256 + cnn      418 KB   14 missed @ wrong<=16
      d=256 + cnn+2    245 KB   20 missed @ wrong<=16
      d=128            328 KB   36 missed @ wrong<=16

Fewer bytes AND better accuracy. **Cut vectors, not dimensions.**

### What the failed lever bought: a resolved cost model

d=128 halves bytes/vector while holding vector count fixed — a clean
discriminator for whether the law scales with bytes or with vectors.

    d=256   4142 ns/vector @ 64 B/vec
    d=128   2274 ns/vector @ 32 B/vec

Time fell 45%, not 50%, giving

    cost per vector = 58.4 ns/byte * bytes + 406 ns

At d=256 bytes are **90%** of the cost and the per-vector floor is **10%**. So
byte-proportionality is very good but not exact, and any lever that cuts bytes
without cutting vector count saturates at a 10% floor.

The d=128 size sweep also **located the cache boundary directly**: N=1000 at
d=128 is 31 KB — inside the 32 KB cache — and ran at 1282 ns/vector against the
flash-resident 2274. The same N=1000 at d=256 is 62 KB, does not fit, and shows
no such discount. The cliff is where the hardware says it is.

PARITY EXACT held at d=128. Shipping config unchanged: d=256, full index.

### Red-team of lever 2

**1. The smoothing confound was real — and my proposed fix was backwards.** The
Dice denominator carries a hardcoded `+8`. At d=128 the active counts halve, so
that constant has twice the relative weight: a hyperparameter implicitly fitted
at d=256, quietly handicapping every other dimension. Made it `TSMOOTH` and
swept it. The suspicion was right that it matters and **wrong about direction**:

    d=128  TSMOOTH= 4   72.9%      d=128  TSMOOTH=16   78.1%
    d=128  TSMOOTH= 8   76.6%      d=128  TSMOOTH=32   71.4%

d=128 prefers **16**, not 4 — the optimum moves *opposite* to dimension, so the
`RD/32` scaling I proposed makes small d worse. Reverted to a constant.

**The rejection survives at d=128's best case.** Curve, best smoothing per row:

    min MISSED at wrong<=      8    12    16    20    24
      d=128 / s8             80    50    36    27    19
      d=128 / s16            67    54    36    27    20
      d=128 / s32            79    54    42    31    29
      d=256 / s8             46    23    12     6     5

36 against 12 at `wrong<=16` no matter how the constant is set. Lever 2 stays
rejected, now on a fair comparison rather than a handicapped one.

**2. Red-teamed the SHIPPING config while there.** Is 8 optimal at d=256? Swept:
2/4/8/16 are tied within noise (differences of 1-6 items against ~5.5 SE), and
only 32 degrades. No free win, but a genuine robustness result: the config is
**not perched on a tuned peak** — it tolerates an 8x range of this constant.

**3. I made the two-point-fit mistake again, one section after criticising it.**
The `58.4 ns/byte + 406 ns/vector` model was fitted on d=256 and d=128 alone.
Took a third point at d=64 (16 B/vector): predicted 1340 ns/vector, **measured
1260 — 6% over**. Refitted on three:

    B/vector   measured   model   residual
        16       1260     1283    -1.8%
        32       2274     2240    +1.5%
        64       4142     4153    -0.3%

    cost per vector = 59.8 ns/byte * bytes + 326 ns

Residuals within 1.8%. The two-point fit had overstated the per-vector intercept
by 25%. At d=256 bytes are **92%** of cost and the per-vector floor is **8%**.

PARITY EXACT at d=64. Shipping config restored: d=256, full index, TSMOOTH=8.

## Lever 3: QIO flash — ALREADY TAKEN (my earlier note was wrong)

I had recorded QIO as "still DIO — choice-group conflict, risks non-boot". That
was a misreading. `sdkconfig` looks contradictory:

    CONFIG_ESPTOOLPY_FLASHMODE_QIO=y        <- the choice
    CONFIG_ESPTOOLPY_FLASHMODE="dio"        <- the string

but the ESP-IDF Kconfig says why, explicitly:

    # Note: we use esptool.py to flash bootloader in
    # dio mode for QIO/QOUT, bootloader then upgrades
    # itself to quad mode during initialisation
    default "dio" if ESPTOOLPY_FLASHMODE_QIO

The string is only the mode used to *write* the bootloader — the ROM must read
it before quad mode exists. The bootloader upgrades itself at init. **QIO has
been active for every measurement in this series.** There is no non-boot risk to
take and no win to collect; it was collected before we started.

Measured what it is worth by forcing the alternatives:

    QIO   43498 us   (PARITY EXACT)
    DIO   62138 us   (PARITY EXACT)

Backing out the per-vector floor (326 ns x 10500 = 3.4 ms, which does not scale
with flash width):

    QIO byte term 40.1 ms   DIO byte term 58.7 ms   -> quad buys 1.47x

Not 2x, because command/address phases and cache-fill latency do not scale with
data width. So the 43.4 ms figure already includes a 1.43x flash-mode win.

**Methodological trap worth recording: `idf.py` caches `-D` variables in the
CMake cache.** "Restoring" the config by rebuilding without the override
silently reused the previous iteration's `SDKCONFIG_DEFAULTS`, producing a
66525 us reading and a PARITY FAILURE that looked like a real regression. A
config sweep MUST pass the override explicitly on every invocation, or
`rm -rf build`. Caught only because the number was slower than the slowest
legitimate variant.

## Test evaluation #2 — PRE-REGISTERED PREDICTION (written before running)

Evaluation #1 is VOID (config chosen on a 75.6%-leaked dev set). This is the
first legitimate test measurement. Committed BEFORE the run so the result cannot
be rationalised afterwards.

**Config, frozen:** twin-ternary, d=256, TSMOOTH=8, full 10500-vector index, no
prune, no prior, no veto, no cascade. Threshold tuned on DEV (=136) and applied
unchanged to test — `report()` verified to tune on `V_*` and tally on `T_*`.

**Dev reference:** 85.9% +-2.5 iot acc, 14 wrong, 14 missed, threshold 136.

**Predictions:**
1. Test IoT accuracy **82-85%**, point estimate **~84%** — slightly below dev,
   because dev supplied the threshold and (historically) config choices, so some
   optimistic bias should regress out. A test result *above* dev would be
   suspicious, not pleasing.
2. Wrong actuations **~30**. Dev had 14 against ~1300 negatives; test carries
   ~2754 negatives, ~2.1x more, so a constant false-actuation *rate* predicts
   ~30. Judging the raw count against dev's 14 would be a units error.
3. Missed **14-20** on ~220 test IoT, matching dev's rate.
4. Twin-ternary still beats binary on iot accuracy.

**Falsification:** if test accuracy lands below 80%, the dev-selected config does
not generalise and the shipping recommendation is wrong. Recorded either way.

## Test evaluation #2 — RESULT

    representation        iot acc      wrong  missed  index KB
    binary (1 bit)        75.5% +-2.9   17     40       328   th=138
    twin-ternary (2b)     84.1% +-2.5   23     20       656   th=136

**All four pre-registered predictions held.** Accuracy 84.1% against a predicted
82-85% / point ~84%; wrong 23 against ~30 predicted; missed 20 against 14-20;
twin beat binary. The <80% falsifier was not triggered.

**The config generalises.** Dev 85.9% +-2.5 -> test 84.1% +-2.5. A 1.8-point
drop, comfortably inside the error bars. The threshold was tuned on dev (136)
and applied unchanged — verified in `report()` before running.

**The false-actuation RATE improved on test**, which the raw count hides: dev
14/~1307 negatives = 1.07%, test 23/~2754 = 0.84%. Test carries 2.1x the
negatives, so comparing 23 against dev's 14 directly would be a units error —
exactly the trap the pre-registration named in advance.

**Nuance that matters for hardware control, and cuts against the headline.**
Twin-ternary's 8.6-point win over binary is *entirely* in recall: misses fall
40 -> 20, while wrong actuations RISE 17 -> 23 (McNemar on the wrong axis: fixed
7, broke 13 — it breaks more than it fixes there). For a system that actuates
physical devices, a false actuation (a light or lock operating unbidden) is
generally worse than a miss (the user repeats themselves). **Twin-ternary buys
recall and pays in precision.** That is the right trade for a demo and arguably
the wrong one for a deployed actuator, and the operating curve is where that
choice should be made — the threshold is the knob, and raising it trades back.

**Standing:** this is the FIRST legitimate held-out number for this work.
Evaluation #1 is VOID. Budget now: 2 used.

### Bug found in the budget guard itself, while spending it

`results/TEST_BUDGET` held both the prose audit note and the count.
`fscanf("%d")` cannot parse a file starting with text: it silently read 0,
incremented to 1, and truncated the file — **destroying the audit annotation and
resetting the counter on every use.** A guard that cannot count past one is not
a guard. Split: `TEST_BUDGET_COUNT` is machine-readable, `TEST_BUDGET` is
append-only and now records timestamp and full argv per touch.

### Red-team of test evaluation #2

**1. The headline comparison was NOT size-matched — and this was the real risk.**
The reported table puts twin-ternary d=256 (656 KB) beside binary d=256
(328 KB). Twin gets twice the bytes. The whole claim is "2 bits per dim beats
1 bit per dim", so the fair control is **binary at d=512**, which is also
64 B/vector — never run until now. If binary d=512 matched twin d=256, the
advantage would be capacity, not structure.

Curve, at matched bytes/vector:

    min MISSED at wrong<=      8    12    16    20    24    B/vec
      binary d=256            49    38    24    24    24     32
      binary d=512            49    38    24    24    24     64
      twin   d=256            46    23    12     6     5     64
      twin   d=512            46    25    15    12     4    128

**Twin dominates binary at every matched operating point at identical bytes** —
12 vs 24 missed at `wrong<=16`, 6 vs 24 at `wrong<=20`. The claim survives.

More telling: **binary d=256 and d=512 have identical curves.** Doubling
binary's dimensions changes nothing at all. Binary saturates, which is direct
evidence its limit is structural — forcing empty dims to -1 — and not capacity.
That is the ternary.h rationale, now measured rather than asserted.

**2. Leakage beyond exact strings: clean.** `inv_disjoint` compares normalised
strings, which is necessary but not sufficient — two different strings encoding
to the SAME twin-ternary code are indistinguishable to the router and
effectively leaked with no assertion firing. Measured: **0 of 1527 dev and 0 of
2974 test items share a code with any index entry.** Clean, but it was luck
rather than design; the check is now permanent.

**3. I graded my own predictions leniently. It was 3/4, not 4/4.** P2 predicted
"~30 wrong" by assuming a constant false-actuation rate (1.05% x 2754 negatives
= 29). Actual 23 — the rate did not hold constant, it improved. **That is a 22%
miss, and it missed in the flattering direction**, which is exactly when
self-grading should be strictest. Corrected: P1 hit (84.1% vs 82-85%/~84%),
**P2 MISS**, P3 hit (20, at the range edge), P4 hit.

**4. Error bars verified against true denominators**, which corrected an
assumption I had been carrying: dev IoT is **192**, not 220 (test IoT is 220).

    dev  iot=192 neg=1335   SE 2.5%   missed 14 (7.3%)  wrong 14 (1.05%)
    test iot=220 neg=2754   SE 2.5%   missed 20 (9.1%)  wrong 23 (0.84%)

Both reported +-2.5 are correct.

**5. The test set is not pristine, and the record should say so.** Evaluation #1
touched it and I saw the aggregate result before it was voided. Config decisions
made afterwards were re-derived on dev, but "never observed" is a stronger claim
than the history supports. Budget: 2 used.

## Raising the threshold for precision — the knob does not do what I claimed

Split `fa` (fires on a NON-command — unbidden actuation) from `wa` (acts wrongly
on a genuine command). Collapsing them into "wrong" hid the safety-critical one.

**At the shipped threshold, unbidden actuations = 1, not 14.** The "14 wrong" is
`fa=1` + `wa=13`. One actuation on a non-command out of 1335 negatives (0.07%).

    th    unbidden(fa)  wrong_act(wa)  missed  iot_acc
    136        1             13          14     85.9%   <- shipped
    148        1             10          25     81.8%
    160        1             10          37     75.5%
    172        1              5          63     64.6%
    184        1              2          81     56.8%
    190        0              1          91     52.1%

**Raising the threshold does not buy precision.** `fa` is flat at 1 from 136 all
the way to 184; it only reaches 0 at 190, where accuracy has collapsed to 52%.
The threshold trades `wa` for `missed`, and `wa` is not the mode that endangers
an actuator. **The knob is the wrong knob for this goal.**

Going the other way is the better trade: th=126 gives **88.0%** at `fa=3`, th=122
gives 89.6% at `fa=5` — still well under half a percent of negatives.

### Correcting my own claim

I wrote that twin-ternary "buys recall and pays in precision", from the combined
test figure (binary 17 wrong, twin 23). The dev split says otherwise:

    binary (1 bit)   th=138   fa=0   wa=13   ms=24   80.7%
    twin-ternary     th=136   fa=1   wa=13   ms=14   85.9%

**Identical `wa`; the entire precision difference is ONE unbidden actuation.**
With fa=1 vs fa=0 on 1335 negatives the confidence intervals overlap almost
entirely — there is no significant precision difference on dev. My warning
overstated the case. The test fa/wa split is unknown and would cost a budget
unit to obtain; the aggregate rise of 6 is NOT attributable without it.

Binary also cannot reach 84% at any threshold ("unreachable"), so twin's
advantage is not a threshold artefact.

### What the residual errors actually are

The single unbidden actuation: **"make me happy" -> iot_coffee** (score 188).

**10 of the 13 `wa` errors are intra-Hue confusions** — right device family,
wrong operation (lighton/off/up/dim/change). And the label quality caps what is
reachable:

    "turn out the lights"      labelled iot_hue_lightUP    <- clear label error;
                                the router said lightoff, which is correct
    "turn on kitchen light"    labelled iot_wemo_on, router said hue_lighton
    "turn off smart lamp in den" labelled hue_lightoff, router said wemo_off
                                <- which physical device a "kitchen light" or
                                "smart lamp" is cannot be inferred from text at
                                all; it depends on the user's installation
    "lighter shade on the lights please" labelled lightdim <- genuinely ambiguous
                                (brighter, or a paler colour?)

So roughly 3-4 of 14 errors are not fixable from the utterance alone. **The
ceiling on this corpus is around 98%, not 100%**, and a chunk of the remaining
gap is annotation noise and device-assignment arbitrariness rather than model
error. Chasing 100% here would be fitting the labeller, not the task.

## Shipped threshold moved 136 -> 126

    th=136 (tune's choice)   85.9% +-2.5   fa=1  wa=13  missed=14
    th=126 (shipped)         88.0% +-2.3   fa=3  wa=15  missed= 8

+2.1 points accuracy and 6 fewer missed commands, for 2 additional unbidden
actuations — 1 -> 3 of 1335 negatives, 0.07% -> 0.22%.

This is a **deliberate override of `tune()`, not a re-derivation.** `tune`
minimises `3*(fa+wa)+ms`, which encodes a 3:1 error preference that was never
argued for; 126 encodes a different one. Recorded as a human decision so nobody
later "fixes" the discrepancy by re-tuning.

**Single source of truth:** `RSHIP_TH` in `router.h`, read by both `mkblob` (the
exporter) and `compare --ship` (the harness). Previously the threshold was
hardcoded in `mkblob` alone, which is exactly how the cnn+2 blob shipped at the
wrong operating point with PARITY still passing.

**Hardware verified:** blob header `threshold=126`, PARITY EXACT (64/64 class and
score), 1-core 43498 us / 2-core 26670 us — latency unchanged, as expected, since
the threshold is a comparison applied after the scan.

**Scope, stated plainly: 126 was chosen on DEV.** The held-out test number at
this threshold is unmeasured, as is the test fa/wa split. The last measured test
figure (84.1% +-2.5) is at th=136 and does NOT describe the shipped config.

## The accuracy metric is blind to false actuations — read every number above with this in mind

Probing thresholds downward (126 -> 111 -> 96) kept "improving" iot accuracy to
a plateau of 89.6%. Mapping the whole curve shows what the plateau actually is:

    th      unbidden(fa)  wrong_act(wa)  missed  iot_acc
    -512        11             15           5     89.6%   <- reject NOTHING
      40        11             15           5     89.6%
      96        10             15           5     89.6%
     120         7             15           5     89.6%
     126         3             15           8     88.0%   <- shipped
     136         1             13          14     85.9%

**Peak dev accuracy occurs at th=-512 — the threshold disabled entirely.** The
plateau is not a sweet spot; it is the bare argmax classifier, and the threshold
is **inert below ~120**.

The cause: `iot_acc = iok / n_iot` is pure recall over IoT items. **It cannot see
false actuations at all** — firing on a non-command costs it nothing. So the
metric is maximised by never rejecting, and every step down from 136 was partly
walking toward a degenerate always-act config. For an actuator that is exactly
backwards: at th=96 the device fires on 10 non-commands instead of 3 for zero
accuracy gain.

**Two floors the threshold can never touch**, visible at th=-512:
`wa=15` (the intra-Hue operation confusions — identical across the ENTIRE range
-512..126) and `missed=5` (IoT commands whose nearest neighbour is a negative
index entry). The threshold only ever trades `fa` against `missed`.

**Consequence: 126 is better than 111 and 96, not worse.** It is the cheapest
point at which the threshold has begun doing real work (0.22% vs 0.75% unbidden),
paying 1.6 points of a metric that does not count the failure that matters.

**Consequence for the whole record:** every "iot acc" figure in this file —
including the held-out 84.1% — is a recall number. It is not a safety number.
The fa column is the safety number, and it was collapsed into "wrong" until this
section. Nothing above is retracted, but accuracy alone should never have been
the headline for an actuator.

## Test evaluation #3 — PRE-REGISTERED PREDICTION (written before running)

**Purpose.** Every held-out figure so far is at threshold 136. The shipped config
is 126, chosen on dev. This measures the config that actually ships, and answers
one question: **was 136 -> 126 a real gain, or dev-overfitting?**

**Config, frozen:** twin-ternary, d=256, TSMOOTH=8, full 10500-vector index, no
prune/prior/veto. Threshold forced to `RSHIP_TH`=126 via `--ship`, NOT tuned on
test. `report()` verified to apply FIXTH and tally on `T_*`.

**Reference points**

    dev  @126   88.0% +-2.3   fa=3  wa=15  missed=8    (192 iot, 1335 neg)
    dev  @136   85.9% +-2.5   fa=1  wa=13  missed=14
    TEST @136   84.1% +-2.5   fa+wa=23     missed=20   (220 iot, 2754 neg)

**Predictions**

1. **Test recall 83–88%, point ~86%.** Dev gained +2.1 going 136->126; test at
   136 sat 1.8 points under dev, so 84.1 + 2.1 - drift ≈ 86.
2. **Missed 10–14, point 12.** Dev missed fell 14->8 (-43%); test was 20 at 136.
3. **fa 3–7, point 5.** Dev fa rate at 126 is 0.22%; 0.22% x 2754 ≈ 6, discounted
   because last time the rate *improved* on test rather than holding (that was
   prediction P2 of eval #2, and it missed by 22% in the flattering direction).
4. **fa+wa total 24–32, point 28.** Higher than the 23 at th=136 — a lower
   threshold necessarily actuates more. **A drop here would be suspicious, not
   good news.**

**Falsifier, stated in advance.** If test recall at 126 is **not** meaningfully
above the 84.1% measured at 136, then the +2.1 seen on dev did not transfer, the
threshold move was fitting the dev curve, and **126 should be reverted to 136**.
Recorded either way. A result inside noise of 84.1% counts as failure to
transfer, not as a tie.

## Test evaluation #3 — RESULT: the dev gain did not transfer, 126 reverted to 136

    th=136   recall 84.1% ±2.5   fa= 8*  wa=15*  missed=20   (eval #2)
    th=126   recall 85.5% ±2.4   fa=13   wa=16   missed=16   (eval #3)

`*` The th=136 split was derived, not logged — eval #2 predates the `fa`/`wa`
split in the run log. Lowering a threshold can only move items OUT of "none", so
the 4 that left `missed` split 3-correct / 1-wrong-act, which forces fa=8, wa=15;
8+15=23 reconciles with the recorded total exactly.

**What the move bought and cost, paired:**

    +3 commands recognised    (185 -> 188 of 220)
    +1 wrong action           (15 -> 16)
    +5 unbidden actuations    (8 -> 13;  0.29% -> 0.47% of 2754 negatives)

**The recall gain is not significant.** The comparison is paired *and
one-directional*: an item correct at 136 has score>136>126, so it cannot get
worse at 126. Discordant pairs are therefore b=3, c=0 — exact two-sided
binomial **p=0.25**.

So: an unbidden-actuation rate that nearly doubled, bought a recall gain
indistinguishable from zero. For something that actuates physical hardware that
is the wrong trade. **Reverted to 136 per the falsifier pre-registered before the
run.** Rebuilt, reflashed, PARITY EXACT, 43498 us.

### Predictions scored strictly: 2 of 4

| # | predicted | actual | |
|---|---|---|---|
| 1 | recall 83–88%, point 86 | 85.5% | **hit** |
| 2 | missed 10–14, point 12 | 16 | **miss** — outside the range |
| 3 | fa 3–7, point 5 | 13 | **miss** — 2.6× the point estimate |
| 4 | fa+wa 24–32, point 28 | 29 | **hit** |

Both misses are on the error columns, and both in the same direction: **I
under-predicted false actuations from dev again.** In eval #2 the fa rate
*improved* on test (1.05% → 0.84%) and I recorded that as the lesson. Here it
*worsened* (dev 0.22% → test 0.47%). The real lesson is not "the rate improves"
but that **the fa rate does not transfer from dev in a predictable direction at
all** — it moved one way at th=136 and the other at th=126. Any threshold choice
justified by a projected dev fa rate is built on sand.

### What this says about the dev curve

The entire "lower the threshold" exploration (136 → 126 → 111 → 96) was run on
dev. The one point of it that was checked against held-out data did not survive.
The dev curve was not lying — 88.0% on dev is real — it simply does not predict
the held-out frontier closely enough to justify a 10-point threshold move.

## Asymmetric threshold ("126 up, 136 down") — REJECTED, but it found real structure

The router already knows direction: `up` = {lighton, wemo_on, lightup},
`down` = {lightoff, wemo_off, lightdim}, neutral = {lightchange, cleaning,
coffee}. And it is implementable — `route()` takes the argmax *before*
thresholding, so the class is known when the threshold is applied.

**There IS a real asymmetry in the scores** (dev, twin, by true direction):

    down     n=75   mean 189.8
    neutral  n=85   mean 187.7
    up       n=32   mean 174.4

Up-commands score ~15 points lower — genuinely harder to match. So the intuition
behind the proposal was sound.

**Two structural findings fell out.**

*The proposed policy moves one item.* `126 up / 136 down+neutral`: recall
85.9 → 86.5%, missed 14 → 13, fa 1 → 2. One item each way.

*The whole 136 → 126 effect lives in up and neutral, not down.* `126 up+neutral /
136 down` is **identical** to uniform 126 — down-commands have no scores in the
126–136 band at all. And neutral supplies 5 of the 6 recovered items, not up.
Which matters, because uniform 126 is the policy test eval #3 showed does not
transfer.

**The sweep found nothing real.** Full 3-D search, up/down/neutral each 100–180:

    combinations swept          : 68921
    "dominating" uniform 136    : 24  (0.03%)
    best missed at fa<=1,wa<=13 : 13 (baseline 14), at up/down/neu = 136/126/132
    largest total error gain    : 1 item out of 192 iot + 1335 negatives

**A one-item gain discovered by searching 68,921 combinations is what noise looks
like.** The best combinations also point the *opposite* way to the proposal —
they hold `up` at 136 and lower `down` — which is the signature of fitting, not
structure. A 192-item dev IoT set cannot resolve a 3-parameter policy; the
single-parameter version already failed to transfer.

Not shipped. No test budget spent. `--dirdump` is retained: it emits per-item
score/prediction/truth so any future threshold policy can be simulated offline
without a second parameter ever entering the router.

## Red-team of the asymmetric-threshold rejection

**Self-check first:** the offline simulator reproduces the harness exactly —
`sim(136,136,136)` → fa=1 wa=13 missed=14, recall 85.9%. That validates both the
dump slice and the simulation before any conclusion rests on them.

**The attack:** my domination test required *all three* error columns to improve,
which could hide a real `wa` reduction available as a trade. Checked the
wa-focused frontier:

    fa<=1, missed<=14  ->  min wa = 13   (baseline; nothing better exists)
    fa<=1, missed=16   ->  wa = 11       (costs +2 missed)
    fa<=1, missed=20   ->  wa =  9       (costs +6 missed)

`wa` falls only by rejecting more commands — converting wrong actions into
misses. That is not fixing the confusion, it is declining to answer. **The
rejection stands, and it establishes that `wa` is threshold-immune: a floor set
by the representation, not the operating point.**

## The wa floor: complementary information exists, but nothing extracts it

The 13 dev wrong-actions at th=136 sort into:

    4  colour commands routed away from lightchange ("warm", "lavender", "red", "blue")
    3  off-commands using "shut"/"close" landing on lightdim/lightup
    2  up/on distinctions
    2  arbitrary device assignment (hue vs wemo — unknowable from text)
    1  ambiguous ("lighter shade" — brighter, or paler?)
    1  LABEL ERROR ("turn out the lights" is labelled lightUP; the router is right)

So ~6 of 13 are not fixable from the utterance at all.

### Cues must be mined, not hand-written

The tempting fix is to read those failures and write a colour lexicon. That is
exactly the archived failure (+4.5 points fitted to test failures; the
leakage-free version gained nothing). So `c/src/cuemine.c` mines discriminative
words **from the index only**, replicating compare.c's DEV carve so dev never
contributes. Colour emerges from training data unprompted:

    colors 96x   color 93x   yellow 89x   blue 87x   green 75x   red 71x

Note "lavender" and "warm" — the words in the dev failures — do NOT appear.
The mining is independent of the errors, as intended.

**But `lightoff`'s top cues are room names**: lamp 67x, bedroom 63x, kitchen 54x,
bathroom 52x, with "off" only sixth. That is corpus bias, not semantics. And
"close" never appears, so adding it would be fitting the error list.

### Hard cues are strictly harmful

`cue.c` builds the word list at index-build time (lift >= CUE_LIFT, count >=
CUE_MIN) and overrides within a device family. Swept:

    CUE_LIFT   20    40    60    80   120   200   400   baseline
    wa         24    23    24    16    16    13    13     13

**Monotone harm, converging to baseline only when it stops firing.** Even at
lift>=80 — admitting only genuine colour terms — it costs 3 wrong actions.

### The channels ARE complementary — which refutes my explanation

Three word-channel attempts had now failed (signature prior inert, word prior
inert, hard cue harmful). I hypothesised the channels were redundant. Measured:

    both correct        156 (81.2%)
    router only           9
    word-prior only      16     <- information the router does NOT have
    neither              11     <- irreducible floor
    router 165 (85.9%)   word prior 172 (89.6%)
    ORACLE picking the right channel per item: 181 (94.3%)

**Hypothesis refuted.** The word prior alone *beats* the router on IoT
classification, and there are 16 items it gets right that the router gets wrong.
An oracle would reach 94.3% against the shipped 85.9% — **8.4 points on the table.**

### But no rule extracts it

Tried the clean architectural split — router (evidence reader) decides
accept/reject, word prior decides which class:

    baseline                 recall 85.9%   fa=1    wa=13
    prior picks class        recall 85.4%   fa=1    wa=14
    ... unconstrained        recall 84.9%   fa=1    wa=15

Worse both ways. One guard is worth recording: the first unconstrained version
produced **fa=824**, because every above-threshold *negative* had its "none"
reassigned to an IoT class. The router must decide command-vs-not before the
prior is allowed to refine anything.

**Conclusion.** 8.4 points of genuinely complementary information exists and
**no combination rule tried can reach it** — soft bias (inert), hard cue
(harmful), class delegation (harmful). What is missing is not a better blend but
a *selector*: a signal for when to trust which channel. Nothing measured so far
provides one. Recorded as the largest known headroom in the system, and as an
open problem rather than a solved one.

Shipped config unchanged. `cuemine.c`, `cue.c`, `--channels` and `--priorcls`
retained as tracked diagnostics.

## Shrinking the prior for the device: 2.13 MB -> 74 KB, bit-exact

The selector needs the word prior on the ESP32. `prior_t` is **2.13 MB** — it
stores what is needed to BUILD the prior (32768 x 16 int32 counts plus totals),
not what is needed to USE it.

**My first reduction claim was wrong.** The synthesis said "store argmax +
margin per bucket, 96 KB". That cannot work: `pr_vote` sums clamped per-class
lift deltas across the words of an utterance and takes the margin of the
**aggregate**, so per-bucket argmax cannot reconstruct it. Corrected before
implementing.

Measured the real structure instead (`--gatesize`):

    live buckets (w_tot >= 2)   2512 of 32768   (7.7%)
    nonzero deltas              3487 total, 1.39 per live bucket
    82% of live buckets hold exactly ONE nonzero delta
    delta range                 [-256, 1024]  -> int16

So the table is **sparse**, and a CSR layout collapses it:

    off[PR_HASH+1]  uint16   64.0 KB
    cls[3487]       uint8     3.4 KB
    del[3487]       int16     6.8 KB
                             74.2 KB     vs 2.13 MB = **29x**

**Verified bit-exact, not "close enough":** `gate_vote` matches `pr_vote` on
class AND margin across **12027 utterances** (1527 dev + 10500 index) — 0
mismatches of either kind. `words_of` is shared rather than reimplemented, so
the gate buckets words identically; a different hash would make it a different
model, not a smaller one.

One assumption is asserted rather than trusted: `gate_vote` treats "no pairs" as
"w_tot < 2", which holds only if every live bucket produced a nonzero delta.
Measured true, but data-dependent, so `gate_build` checks every bucket and
refuses to produce a table if it ever fails.

End to end, the compact table changes nothing:

    baseline                      recall 85.9%  fa=1  wa=13  missed=14
    selector via prior (2.13 MB)  recall 87.5%  fa=1  wa=10  missed=14
    selector via gate  (74 KB)    recall 87.5%  fa=1  wa=10  missed=14

**Device budget: 656 KB index + 74 KB gate = 730 KB of 4096 KB flash (17.8%).**
The blocker identified in the LMM synthesis is closed.

Still NOT shipped: the selector remains opt-in and the blob is byte-identical.
It has no held-out measurement, and the 126 threshold looked this good on dev
too. Next step is a pre-registered test evaluation, per selector_synth.md.

## Test evaluation #4 — PRE-REGISTERED (written before running)

**Deviation from selector_synth.md, stated up front.** The synthesis put device
integration before the budget spend, reasoning that validating an undeployable
thing is waste. That blocker is now closed (74 KB, 17.8% of flash), and
host-device parity means the held-out number is identical either way. So:
validate first, integrate only if it survives. Building firmware support for
something that may be cut is the actual waste.

**Config:** twin-ternary d=256, index 10500, threshold `RSHIP_TH`=136, plus the
selector — word prior consulted ONLY when its margin >= 8, and only to reassign
a class the router has already accepted as a command.

**Reference points**

    dev  baseline    recall 85.9%   fa=1  wa=13  missed=14
    dev  selector    recall 87.5%   fa=1  wa=10  missed=14
    TEST baseline    recall 84.1%   fa=8  wa=15  missed=20   (eval #3, th=136)
    index CV         net +48 of 183 disagreement items at prmarg>=8

**Predictions**

1. **`fa` EXACTLY 8 — unchanged.** Not "about 8". The selector fires only after
   the router accepts, so it cannot create an actuation. If `fa` moves at all,
   my account of the mechanism is wrong, regardless of what happens to `wa`.
2. **`missed` EXACTLY 20 — unchanged.** The selector never touches the
   accept/reject decision.
3. **`wa` 11–13, point 12.** Dev fell 13 → 10 (−23%); the same relative
   reduction on 15 gives ~12.
4. **Recall 85–87%, point 85.9%.** Follows from 3 if 1 and 2 hold.

**Falsifier.** If `wa` does not improve on held-out data, the gate is
dev-specific and **the selector is cut** — not re-tuned. A `wa` inside noise of
15 counts as failure to transfer, not a tie. Separately, if predictions 1 or 2
fail, the mechanism is not what I claim and the result is void even if `wa`
improves, because I would not know why.

**Note on prior transfer failures.** Eval #2's fa prediction missed by 22%
(flattering), eval #3's by 2.6x (unflattering). I have no reliable model of how
error rates transfer. Predictions 1 and 2 are exempt from that doubt only
because they are STRUCTURAL — they follow from where the code fires, not from a
rate extrapolated off dev.

## Test evaluation #4 — RESULT: the selector FAILED and is CUT

    baseline   recall 84.1%   fa=8   wa=15   missed=20
    selector   recall 82.7%   fa=8   wa=18   missed=20

**The falsifier fires.** `wa` did not improve — it got worse, 15 → 18. Per the
pre-registration the selector is **cut, not re-tuned**. Re-tuning the gate on
test would be exactly the contamination the discipline exists to prevent.

**Predictions: 2 of 4, and the two hits are the structural ones.**

| # | predicted | actual | |
|---|---|---|---|
| 1 | `fa` **exactly 8** | 8 | **hit** |
| 2 | `missed` **exactly 20** | 20 | **hit** |
| 3 | `wa` 11–13 | 18 | miss — wrong direction |
| 4 | recall 85–87% | 82.7% | miss — below baseline |

The mechanism is exactly what I claimed: the selector fires only after the
router accepts, so it cannot create an actuation or change a rejection. Both
structural predictions held precisely. **It simply makes worse decisions on
unseen data.** Knowing the mechanism was right makes the failure more
informative, not less.

### Red-team: the dev result was never significant

Paired McNemar, baseline vs selector on dev: **fixed 6, broke 3, p = 0.508.**

The selector "dominated the operating curve" — and that was a 6-vs-3 split at
p=0.51. **Curve dominance is necessary but NOT sufficient**, because a
noise-level change can dominate a curve by helping marginally at every
threshold. My acceptance criteria had a hole in them, and this is the case that
found it. Added to doc/METHOD.md as a required test.

### The index cross-validation did not predict held-out — cause unknown

`--xval` gave +48 net of 183 disagreement items, which is what convinced me the
signal was real. It did not transfer.

Tested the most likely explanation — that the 36–37% near-duplicate rate lets
the *word* prior memorise paraphrases across folds. **Refuted:**

    eval items              n     router_only  prior_only  prior net
    WITH near-dup in train  513       22           21          -1
    WITHOUT near-dup        642       64           76         +12

The prior's advantage lives in items *without* near-duplicates. So CV inflation
is not memorisation, and **I do not have a confirmed explanation.** Recorded as
an open discrepancy rather than a mechanism invented after the fact.

Practical consequence: **index cross-validation is not a substitute for held-out
measurement in this project.** It was the strongest pre-test evidence available
and it was wrong.

### Retroactive: the same test applied to the core claim

    twin-ternary vs binary (d=256), dev:  fixed 16  broke 7  p = 0.0931

**Not significant on dev at n=192.** Stated plainly rather than buried. Two
things keep this from overturning the claim, and both are caveats not defences:

- The held-out gap is much larger — 84.1% vs 75.5%, **19 net items on 220** —
  and very likely significant, but the paired test was not computed and would
  cost a budget unit.
- The size-matched curve comparison (binary d=512, same 64 B/vector) shows twin
  dominating at *every* operating point, and binary's curve is identical at
  d=256 and d=512. That is a structural argument, not a per-item one.

The honest position: the representation claim rests on the held-out gap and the
saturation evidence, **not** on dev significance, which it does not have.

### What survives

The selector is cut. `gate.c` (74 KB, bit-exact) and `cue.c` are retained as
tracked negative results. Shipped config unchanged and blob byte-identical:
**d=256, threshold 136, 84.1% ±2.5 held-out, fa 0.29%.** Test budget: 4.
