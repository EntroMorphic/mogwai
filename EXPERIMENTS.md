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
