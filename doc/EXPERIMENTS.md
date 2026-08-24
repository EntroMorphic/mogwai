# Experiment log

Every row in `results/RESULTS.tsv` is stamped with the git SHA and whether the
tree was clean. `make compare` appends automatically — tracking is structural,
not a discipline anyone has to remember.

    make fetch     # curl the corpora, record SHA256
    make compare   # build, run, append one row per variant

## Look up a fact

| question | answer | where |
|---|---|---|
| What ships? | twin-ternary, d=256, **3840 vectors, 137 KB** (v2 exception format), threshold 136 | [126 was tried and reverted](#test-evaluation-3--result-the-dev-gain-did-not-transfer-126-reverted-to-136) |
| How fast on device? | **4.3 ms**, index 100% resident in SRAM, PARITY EXACT — with WiFi up too | [Exception set](#the-sign-plane-is-an-exception-set-not-a-bit-plane) |
| Why is the index in flash at all? | It fits total free heap but no single heap region | [same](#chunked-sram-residency-the-index-does-not-need-one-allocation) |
| Can the sign plane be packed? | Densely, no — 4.8x slower. As a sparse **exception set**, yes, and it ships | [Exception set](#the-sign-plane-is-an-exception-set-not-a-bit-plane) · [the loss](#packing-the-sign-plane--measured-and-it-loses) |
| What predicts latency? | 59.8 ns/byte + 326 ns/vector; bytes are 92% | [Cost model](#what-the-failed-lever-bought-a-resolved-cost-model) |
| Is 2-bit really better than 1-bit? | Yes, at **matched bytes**, on the whole curve | [Red-team of test eval](#red-team-of-test-evaluation-2) |
| What is the held-out number? | **84.1% ±2.5, fa 12/2754 (0.44%)** — the SHIPPED index at threshold 136 | [Test eval #6 result](#test-evaluation-6--result-the-invariance-transferred-and-the-cost-is-half-what-i-predicted) |
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

**Held-out evaluations** — budget is deliberately scarce; #1 is VOID
- [#2 pre-registered](#test-evaluation-2--pre-registered-prediction-written-before-running) · [result](#test-evaluation-2--result) — 84.1% ±2.5
  - [Bug found in the budget guard while spending it](#bug-found-in-the-budget-guard-itself-while-spending-it)
  - [Red-team](#red-team-of-test-evaluation-2) — the missing size-matched control
- [#3 pre-registered](#test-evaluation-3--pre-registered-prediction-written-before-running) · [result](#test-evaluation-3--result-the-dev-gain-did-not-transfer-126-reverted-to-136) — the 126 gain did not transfer; reverted
- [#4 pre-registered](#test-evaluation-4--pre-registered-written-before-running) · [result](#test-evaluation-4--result-the-selector-failed-and-is-cut) — the selector failed and was cut
- [#5 pre-registered](#test-evaluation-5--pre-registered-is-the-core-claim-significant) · [result](#test-evaluation-5--result-the-core-claim-is-significant-on-held-out-data) — **core claim significant, p=0.0288**

**Choosing the operating point**
- [Raising the threshold does not buy precision](#raising-the-threshold-for-precision--the-knob-does-not-do-what-i-claimed)
  - [The same knob on the shipped index](#the-same-knob-on-the-shipped-index--and-where-fa-actually-comes-from) — `fa=0` costs 31 points of recall; polarity is 13/13 clean, argmax owns everything
  - [Correcting my own claim](#correcting-my-own-claim)
  - [What the residual errors actually are](#what-the-residual-errors-actually-are) — the corpus ceiling
- [Shipped threshold moved 136 → 126](#shipped-threshold-moved-136---126) — later reverted, see #3
- [The accuracy metric is blind to false actuations](#the-accuracy-metric-is-blind-to-false-actuations--read-every-number-above-with-this-in-mind) — read before quoting any number
- [Asymmetric threshold — REJECTED](#asymmetric-threshold-126-up-136-down--rejected) — real structure, no actionable policy
  - [Red-team](#red-team-of-the-asymmetric-threshold-rejection) — `wa` is threshold-immune

**The wa floor, and the selector that failed to reach it**
- [Is the discarded magnitude recoverable?](#is-the-discarded-per-dimension-magnitude-recoverable-two-oracles-both-negative) — shortlist coverage is fine; two fine metrics both lose, and the 99.3%-ones sign plane is load-bearing
- [Complementary information exists, but nothing extracts it](#the-wa-floor-complementary-information-exists-but-nothing-extracts-it) — oracle 94.3% vs router 85.9%
- [Shrinking the prior for the device](#shrinking-the-prior-for-the-device-213-mb---74-kb-bit-exact) — 2.13 MB → 74 KB, bit-exact

**Where the time goes on device**
- [A functional WiFi stack, measured](#a-functional-wifi-stack-measured-what-it-costs-and-what-survives-it) — TLS dips 46.7 KB below steady state; the shipped reserve was smaller than that
- [Hardware-offload audit](#hardware-offload-audit-what-can-move-to-silicon-and-what-cannot) — every path ends at the same 24 MB/s flash wall
- [Packing the sign plane — MEASURED, and it loses](#packing-the-sign-plane--measured-and-it-loses) — bit-exact and 1.61x smaller, but 4.8x slower; 6x over break-even
- [The sign plane is an exception set](#the-sign-plane-is-an-exception-set-not-a-bit-plane) — 0.16% of dims are `-1`; store those, not 120 KB of bit-plane. **Lossless**, 64 → 34.4 B/vector, 6.46 → 4.3 ms, and 100% resident with WiFi up
- [Chunked SRAM residency](#chunked-sram-residency-the-index-does-not-need-one-allocation) — free heap is a **sum of regions**; one malloc can never fit. 43.9 → 34.3 ms at no accuracy cost
- [How few negatives does rejection need?](#how-few-negatives-does-rejection-need) — negatives cost `fa` only, never `missed`; the knee is a function of the fa budget
- [The shipped index is pruned to 3840 vectors](#the-shipped-index-is-pruned-to-240-kb-for-full-sram-residency) — 100% resident, 34.3 → 6.3 ms in the v1 format (v2 took it to 4.3); the price is `fa` 1 → 6 and nothing else
  - [With WiFi running it is 70% resident](#with-wifi-running-it-is-70-resident-not-100) — 9.3 ms under the v1 format. **v2 removed this** — 100% resident and 4.3 ms with the radio associated
- [#7 pre-registered](#test-evaluation-7--pre-registered-does-boundary-witness-selection-transfer) · [result](#test-evaluation-7--result-no-falsifier-fired-and-the-effect-is-one-event) — direction transferred, magnitude did not: fa 12 -> 11
- [#6 pre-registered](#test-evaluation-6--pre-registered-does-the-pruning-cost-transfer) · [result](#test-evaluation-6--result-the-invariance-transferred-and-the-cost-is-half-what-i-predicted) — the invariance transferred; **4 of 4 predictions hit**, fa 8 → 12

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

> **The curve in this section is the UNPRUNED 656 KB index**, which has `fa=1`.
> The shipped index is 240 KB with `fa=6`, so its curve is different and the
> "flat at 1" observation below does not describe it. The conclusion survives on
> the shipped index for a different reason — see
> [the same knob on the shipped index](#the-same-knob-on-the-shipped-index--and-where-fa-actually-comes-from).

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

> **REVERTED. This section is history, not current state.** 126 shipped for one
> day and was reverted to 136 by
> [test evaluation #3](#test-evaluation-3--result-the-dev-gain-did-not-transfer-126-reverted-to-136),
> which found the dev gain did not transfer. **`RSHIP_TH` is 136.** Everything
> below is preserved as written, because the reasoning is the point and this
> repo archives rather than edits — but three statements in it are now false and
> are marked inline.


    th=136 (tune's choice)   85.9% +-2.5   fa=1  wa=13  missed=14
    th=126 (shipped AT THE TIME)  88.0% +-2.3   fa=3  wa=15  missed= 8

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

**Hardware verified** (of its time; the shipped blob header now reads
`threshold=136`)**:** blob header `threshold=126`, PARITY EXACT (64/64 class and
score), 1-core 43498 us / 2-core 26670 us — latency unchanged, as expected, since
the threshold is a comparison applied after the scan.

**Scope, stated plainly: 126 was chosen on DEV.** The held-out test number at
this threshold is unmeasured, as is the test fa/wa split. The last measured test
figure (84.1% +-2.5) is at th=136 and does NOT describe the shipped config.

> No longer true, twice over: 136 was restored, and
> [test evaluation #6](#test-evaluation-6--result-the-invariance-transferred-and-the-cost-is-half-what-i-predicted)
> measured the shipped 240 KB index directly at 84.1% +-2.5, fa 12, wa 15,
> missed 20.

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

## Asymmetric threshold ("126 up, 136 down") — REJECTED

<!-- flags: --dirdump -->, but it found real structure

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

Flags: `--priorcls` / `--priorcls2` (class delegation, harmful), `--cue`
(hard word cues, harmful).


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

Flags: `--gatesel --selmargin=8` reproduces it; `--selsig` shows the dev result
was never significant; `--xval` is the index cross-validation that misled.


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

## Test evaluation #5 — PRE-REGISTERED: is the core claim significant?

**Why spend a unit on this.** Twin-ternary beating binary is the central claim of
this repository, it is now stated in a public README, and it has **never had a
paired significance test on held-out data**. On dev it is `fixed 17, broke 7,
p=0.0639` — not significant at n=192. It has been resting on the held-out *gap*
(19 net items on 220) and on binary saturating at d=256 vs d=512. Both are
sound; neither is the test.

**Config:** both variants at `RSHIP_TH`=136 on the held-out split, same index,
same run, paired per item. `mcnemar()` now reports overall correctness with an
exact two-sided binomial p.

**Predictions**

1. **fixed 22–30, broke 4–10.** The net must be ~19 (84.1% vs 75.5% of 220 =
   185 vs 166). Dev's broke was 7; assuming a similar rate gives fixed ≈ 26.
2. **p < 0.01 — SIGNIFICANT.** A 19-item net on ~30 discordant pairs is far from
   a coin flip. This is the prediction that matters.
3. `fa` and `wa` for twin unchanged from eval #3 (8 and 15) — same config, same
   split. A structural check that nothing drifted since.

**Falsifier.** If **p ≥ 0.05**, the core claim is not statistically supported on
held-out data either. In that case the README must say so in the same sentence
as the claim, and the representation result is downgraded to "consistent with,
but not established by, this corpus". No re-tuning, no second look.

**Note.** Prediction 2 is the first prediction this session that I expect to
land comfortably rather than marginally — the effect is 2.7x the size of the dev
effect on a larger sample. Recording that expectation so overconfidence is
visible if it is wrong.

## Test evaluation #5 — RESULT: the core claim IS significant on held-out data

    binary (1 bit)      75.5% ±2.9   th=138
    twin-ternary (2b)   84.1% ±2.5   th=136
      paired, overall:  fixed 25   broke 11   p = 0.0288   SIGNIFICANT

**The representation claim now has held-out statistical support.** Not merely a
gap and a saturation argument — a paired test on 220 items, p=0.0288.

**Deviation from the pre-registration, stated plainly.** I wrote "both variants
at `RSHIP_TH`=136". `make testset` auto-tunes, so binary ran at **138** and twin
at 136 — each at its own dev-tuned threshold. That is arguably the fairer
comparison (each variant at its best operating point, which is how the table is
reported), but it is not what I said I would do. For reference, on dev the two
framings differ little: matched-136 gives p=0.0639, binary-at-138 gives p=0.0931.
I did not re-run to match, because re-running on test to get a framing I prefer
is exactly the behaviour the budget exists to prevent.

### Predictions scored: 2 of 4

| # | predicted | actual | |
|---|---|---|---|
| 1a | fixed 22–30 | 25 | **hit** |
| 1b | broke 4–10 | 11 | miss — by one |
| 2 | **p < 0.01** | **p = 0.0288** | **miss** — significant, but not comfortably |
| 3 | twin fa/wa unchanged (23 wrong, 20 missed) | 23, 20 | **hit** — no drift |

**The miss worth noting is #2.** I wrote in the pre-registration: *"the first
prediction this session that I expect to land comfortably rather than
marginally — recording that expectation so overconfidence is visible if it is
wrong."* It landed at p=0.029. Significant, but a third of the margin I
predicted, and one more discordant pair the wrong way would have put it near
0.05. **The effect is real and it is not large.**

### What the README should now say

The claim rests on: a **held-out paired test at p=0.0288**, a size-matched
control (binary d=512, same 64 B/vector), and binary saturating — its curve is
identical at d=256 and d=512. It is **not** significant on dev (p=0.0639), and
that remains worth stating, because it shows how close this sits to the
resolution limit of the data.

Test budget: 5.

## Hardware-offload audit: what can move to silicon, and what cannot

Enumerated every operation in the per-query path and measured it, then mapped
each against the ESP32-D0WD-V3's peripherals.

### The path is 99.8% one operation

    r_norm -> r_counts (FNV-1a 3/4-grams) -> t_encode -> t_active   ~0.05 ms
    SCAN 10500 x (t_dot + Dice + argmax)                            43.5 ms
    threshold -> r_polarity -> r_apply_polarity                     negligible

Encode, normalisation and the polarity cue scan together are **~0.1%**. Nothing
outside the scan is worth offloading, whatever hardware exists.

### The scan is memory movement, not arithmetic

    full scoring   flash-mapped 4168 ns/vec   DRAM 1617 ns/vec   2.58x
    touch only     flash-mapped 1254 ns/vec   DRAM   63 ns/vec  19.89x

**The byte cost is cache-MMU/bus overhead, not flash latency.** The same bytes
read from internal SRAM are ~20x cheaper per access. An SRAM-resident index
would run in ~17 ms instead of 43.5.

### But bulk flash bandwidth is a hard floor

    memcpy   4 KB flash->DRAM  129 MB/s     <- fits the 32 KB cache; re-reads it
    memcpy  16 KB flash->DRAM  155 MB/s     <- same artefact
    memcpy  64 KB flash->DRAM   24.8 MB/s   <- exceeds cache: the real rate
    memcpy 128 KB flash->DRAM   24.1 MB/s

**656 KB at ~24 MB/s = 27 ms just to move the index once.** No peripheral beats
that; it is the device's flash read rate at QIO/80 MHz.

### DMA double-buffering: measured, REJECTED

Copy chunk N+1 while scoring chunk N, so the CPU always reads SRAM.

    double-buffered scan (512-vector chunks): 44831 us  = **0.97x** (slower)

CPU `memcpy` is synchronous, so 2461 ns copy + 1617 ns compute is additive.
And perfect overlap only reaches `max(2461, 1617)` = 26 ms — no better than the
naive dual-core split already measured at 26.7 ms. **Both are the same 24 MB/s
wall approached from different directions.**

### Coarse-to-fine with SRAM signatures: measured, REJECTED on accuracy

Fold each 256-bit mask to 64 bits (OR groups of 4), keep 10500 x 8 B = 82 KB in
SRAM, rank by `popcount(qf & bf)` — a legitimate upper bound on mask overlap —
then read flash only for the top-K survivors.

    K>=64    18265 us/query   2.38x   but exact-match vs full scan: **17/64**
    K>=256   19655 us/query   2.21x                                 20/64

Fast, and wrong. Folding 4 dims into 1 leaves nearly every vector with a
near-full signature, so the ranking carries almost no information. The speedup
is real and the answers are not.

### The rest of the silicon, priced

| peripheral | verdict |
|---|---|
| second core | 1.69x measured — but core 1 belongs to WiFi in production |
| SHA / AES / RSA accelerators | wrong primitives, and encode is 0.1% of the work |
| PCNT + DMA + GPIO loopback | priced earlier at 268 ms — **3.4x slower** than the CPU |
| ULP coprocessor | 8-instruction FSM; cannot express this |
| QIO flash @ 80 MHz | already enabled; worth 1.47x and already counted |
| GDMA | cannot exceed the 24 MB/s flash rate it would be feeding from |

### Conclusion: the lever is fewer bytes, not faster bytes

Every hardware path terminates at the same wall — 656 KB per query at 24 MB/s.
The only remaining levers move **less data**:

- **Prune the index.** Measured: condensation gives 418 KB at a cost inside the
  noise floor. 36% fewer bytes ≈ 36% faster, on the single-core path.
- **Fit SRAM.** ~171 KB free = ~2670 vectors at 64 B. Needs aggressive pruning,
  but buys the full 2.58x.
- **Compress.** See below.

### Footnote: TRIX and the footprint

TRIX packs four ternary values per byte — **2 bits per value**. Twin-ternary is
already at exactly that density: 256 dims x 2 planes = 64 B, 2 bits/dim. The
difference is layout, not density: bit-planes make the dot product three
bitwise ops and two popcounts, which is what makes this viable on an LX6 with
no SIMD. TRIX's packed layout targets ARM NEON.

Its signature routing (`weights.sum().sign()`, score, argmax) is the mechanism
built here as `cascade.c` and **measured inert** — identical on every axis, and
cut. Different context (weight tiles vs index vectors), so this is not a
refutation of TRIX; it is a note that the same idea did not transfer here.

The transferable idea is **sparsity**. Measured:

    active dims per vector: mean 57.9 of 256 (22.6%), min 1, max 179

- Naive sparse (u8 index + sign bit) = **65 B/vector — larger** than the 64 B
  bit-plane form. Rejected.
- But the sign plane stores 256 bits of which only ~58 are meaningful. That is
  ~25 B/vector of provable waste, and the entropy bound at 22.6% density is
  ~32 B/vector — **a genuine 2x footprint reduction is theoretically available**.
- The cost is the branchless popcount: any packed form needs bit extraction to
  realign signs with mask positions. At 92% byte-bound, fewer bytes might still
  have won. **They do not** — measured at 4.8x slower for 1.61x fewer bytes,
  against a break-even of 1.62x. See
  [Packing the sign plane](#packing-the-sign-plane--measured-and-it-loses).

## Packing the sign plane — MEASURED, and it loses

The section above called this "the best-supported remaining idea in this repo."
It was, and it is wrong. Measured on host, `-O2`, same 8-bit popcount table on
both sides, three runs:

| form | ns/vector | vs shipped |
|---|---|---|
| `t_score_pre` — branchless, **the shipped path** | **12.2–13.6** | 1.00x |
| packed EXPAND — scatter signs back, then branchless | 108–116 | 8.0–9.2x |
| packed RANK — rank-index the intersection only | 69–76 | 5.2–6.1x |
| packed RANK+ — per-word prefix ranks precomputed, +8 B/vector | 63–70 | 4.8–5.6x |

RANK+ exists so the answer cannot be blamed on a weak encoder: it removes the
redundant popcounts exactly the way `t_score_pre` did. Still 4.8x.

Bit-exactness is not the problem — it is perfect. Full cross product, no
sampling: **512 dev queries x 10500 index vectors = 5,376,000 comparisons** per
form, **0 dot mismatches, 0 score mismatches, 0 pack round-trip sign errors**
over 607,676 active dims. The encoding is sound. It is the decoding that costs.

The bytes are real too: 39.67 B/vector packed vs 64.0 — **1.613x**. The problem
is that the byte saving is not worth what it costs to use:

| | byte penalty at 64 B | bytes saved by packing | compute added | net |
|---|---|---|---|---|
| flash-mapped | 2551 ns/vec | −970 ns/vec | +5836 ns/vec | **+5185 — costs** |
| SRAM-resident | 63 ns/vec | −24 ns/vec | +5836 ns/vec | **+5820 — costs** |

Break-even needs a compute ratio of **1.62x** in flash and **1.015x** in SRAM.
The cheapest measured form is 4.76x — **6.0x over budget** in the friendlier of
the two cases. And the SRAM row is the important one now (see the next section):
once the index is resident the scan is compute-bound, so trading cycles for
bytes is not a close call, it is backwards.

The mask intersection averages only **13.6 bits**, so RANK's loop runs ~14
iterations and still loses. The cost is not popcount volume. It is that eight
independent word operations were replaced by a data-dependent, serially
dependent, branchy loop.

The host is *generous* to the packed form — an M4 is out-of-order with a branch
predictor; the LX6 is in-order, 5-stage, no predictor, no popcount instruction.
The device ratio should be **worse** than 4.76x, so the direction of this
conclusion is safe without re-measuring on hardware.

Reproduce: `c/bin/compare --packbench`. The remaining byte lever is pruning, not
encoding.


### Superseded — the packing that wins does not decode at all

Everything above stands **for the encoding it tested**, which packed the signs
of active dims densely and therefore had to reconstruct them before scoring.
That is the step that cost 4.8x, and the conclusion — do not trade cycles for
bytes once the index is resident — was right.

The v2 format does not make that trade. See
[The sign plane is an exception set](#the-sign-plane-is-an-exception-set-not-a-bit-plane):
it keeps the mask intact, stores only the 0.16% of dims that are below centre as
byte positions, and absorbs them as a **correction term in the algebra** rather
than reconstructing anything. It beats the packed form on both axes at once —
34.4 B/vector against 39.67, and *fewer* popcounts rather than 4.8x more.

## The sign plane is an exception set, not a bit-plane

Twin-ternary stores two bit-planes: `m[]` mask and `s[]` sign, 64 B/vector.
Measured over the shipped 3840-vector index, all 983,040 dims:

| symbol | count | share |
|---|---:|---:|
| `0` (m=0) | 771,755 | 78.51% |
| `+1` (m=1,s=1) | 209,746 | 21.34% |
| `-1` (m=1,s=0) | **1,539** | **0.16%** |

Thirty-two of every sixty-four vector bytes encode a bit that is `+1` for 99.27%
of active dims. Conditional entropy of sign given active is ~0.062 bits: the
whole sign plane carries about **1.6 KB of information stored in 120 KB**. That
0.062 matches the figure already recorded under `--condcentre`, from a different
direction.

It is not waste, and it must not be deleted. `--condcentre` conditioned the
centre so the signs would split evenly and lost **twenty points** (65.6% ±3.4
against 85.9% ±2.5). With a near-constant sign plane, `disagree` fires only when
a dim sits below its diluted mean, which happens when it fires once inside a
*long* utterance. That is a length signal, and long utterances are the
negatives — which is why 83% of the exceptions live in `none`.

So represent it as what it is. Keep the 32-byte mask, make `+1` implicit, store
the below-centre dims as `uint8` positions. With `s = m & ~E`, inside `both` we
have `q.s = ~Eq` and `b.s = ~Eb`, so `diff` there is `Eq ^ Eb`:

    disagree = |Eq & bm| + |Eb & qm| - 2*|Eq & Eb|

An identity for every input — not an approximation, and nothing is decoded.
Eight popcounts instead of sixteen, plus bit tests on sets that are empty for
89.3% of vectors.

    DEV query x index pairs      5,863,680    dot mismatches 0
    of which |Eq & Eb| > 0          54,683
    randomised dense pairs         200,000    dot mismatches 0
    of which |Ea & Eb| > 0         177,943 (89.0%)

The second and fourth lines matter: if the exception sets never intersected, the
`-2*inter` correction would be an unexercised branch and "0 mismatches" would
prove nothing about it.

| | v1 | v2 |
|---|---:|---:|
| blob | 261,036 B | **147,377 B** |
| vector payload | 245,760 B | **132,101 B** |
| SRAM resident | 245,760 B | **139,781 B** |
| bytes/vector | 64 | **34.4** |
| device latency | 6.46 ms | **4.3 ms** |
| with WiFi up | 9.3 ms at 70% resident | **4.3 ms at 100%** |

On hardware: `PARITY EXACT`, 64/64 class and score. That is the proof — the
reference scores embedded in the blob are computed host-side from the **v1
bit-planes**, and the device reproduces all 64 bit-exactly from the exception
form. Being bit-identical, adoption cost **no test budget**: every number
recorded on test remains exactly valid.

A live TLS handshake over an associated radio returned HTTP 200 and dipped free
heap to 49,184 B — a 37,912 B peak draw against the 61,440 B reserve — with the
whole index still resident, and routing afterwards score-identical.

Full write-up, including the red-team that found two silent-failure gaps in the
parser: [doc/return-to-me/the-sign-plane-is-an-exception-set.md](return-to-me/the-sign-plane-is-an-exception-set.md).

## Chunked SRAM residency: the index does not need one allocation

Flash-mapped scanning costs 4168 ns/vector against 1617 ns from internal SRAM —
2.58x, because the cost is cache-MMU and SPI overhead, not flash bandwidth. So
"lift the index into SRAM" has been the obvious win all along, and every attempt
failed silently. The reason:

    index needs                    258,720 B
    total free heap                295,764 B   <- fits
    largest contiguous free block  163,840 B   <- a single malloc cannot fit

`esp_get_free_heap_size()` reports the **sum across heap regions**. The ESP32
splits DRAM into several non-contiguous blocks, so one large `malloc` is bounded
by the **largest block**, not the total. A flat lift can never succeed at this
size. It does not fail loudly either — `malloc` returns NULL, the fallback
engages, and the firmware runs at flash speed while reporting success.

The index is scanned strictly in order, so it never needed to be one allocation.
Lifting it as 8 KB chunks (128 vectors) uses nearly all the free heap, and a
chunk that will not fit simply stays flash-mapped — same bytes, same scores,
graceful degradation instead of all-or-nothing:

| index | vectors in SRAM | per query, flat | per query, chunked | speedup |
|---|---|---|---|---|
| pruned, 245 KB | 3840 / 3920 (**97%**) | 16,520 µs | **6,618 µs** | 2.43x |
| shipped, 656 KB | 3588 / 10500 (34%) | 43,900 µs | **34,300 µs** | 1.28x |

**The shipped row costs nothing.** Same blob, same threshold, same 84.1% ±2.5
held-out, same fa. 43.9 → 34.3 ms is free.

Both rows land on the cost model, which is the real check here — predicted time
is `T_flash x ((1−f) + f/2.58)` for resident fraction `f`:

| | predicted | measured | error |
|---|---|---|---|
| f = 0.9796 | 6.61 ms | 6.62 ms | 0.2% |
| f = 0.3417 | 34.71 ms | 34.30 ms | 1.2% |

That is an independent confirmation of the 2.58x flash/SRAM figure, from a
mechanism that did not exist when it was measured.

**The check that matters is addressing, not copying.** `memcmp`-ing a chunk
straight after `memcpy`-ing it re-reads what it just wrote and can never fail —
it was the first thing written here and it was worthless. The real hazard in
chunking is an off-by-one at a chunk boundary, which would silently score vector
`i` against another vector's bytes. So boot walks **every** index through the
exact nested accessor the scan uses and compares it to the flat flash-mapped
original. Mutation control: shortening the copy by one vector fires **30
mismatches, one per lifted chunk**. Clean tree reports 0 over all 10,500.

Reserve is 40 KB; the board boots with 39,888 B free and the last chunk is
correctly refused.

## How few negatives does rejection need?

The index is 89% negatives. Sweeping how many are needed, using graded
condensation (`--prune-negtop=N`: the leave-one-out NN pass `--prune-cnn`
already runs, but keeping the coverage count `cov[j]` instead of thresholding it
at zero, then keeping the top N). Verified as a strict generalisation — byte-
identical curve output at `N=9345` (unpruned) and `N=5529` (== `--prune-cnn`).

**Negatives cost `fa` only. They never cost `missed`.** At the shipped threshold
136, `missed` and `wa` are constant at **14 / 13 across the entire range**, from
9345 negatives down to 1. Only false actuations move:

| negatives | 9345 | 6250 | 4000 | 2845 | 2000 | 1000 | 250 | 1 |
|---|---|---|---|---|---|---|---|---|
| **fa @ th=136** | 1 | 2 | 4 | **6** | 8 | 11 | 17 | 24 |
| missed @ 136 | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 14 |
| wa @ 136 | 13 | 13 | 13 | 13 | 13 | 13 | 13 | 13 |

Negatives never sit in an IoT item's argmax at th ≥ 136. Every "missed at
matched fa" figure elsewhere is a *consequence* of raising the threshold to buy
the fa back — not a direct effect. Degradation is smooth and monotone, roughly
**one extra false actuation per 350–400 negatives removed** in the mid range.

So "where is the knee" has no single answer — **the knee is a function of the fa
budget**, bisected to the exact negative:

| fa budget | knee | negatives free below it | jump at the cliff |
|---|---|---|---|
| fa ≤ 1 | **8990 negatives / 634 KB** | 355 (3.8%, 22 KB) | missed 14 → 27 |
| fa ≤ 3 | 8990 / 634 KB | 355 | missed 8 → 12 |
| fa ≤ 8 | **6250 negatives / 463 KB** | 3095 (33%, 193 KB) | missed 5 → 8 |

At fa ≤ 1 there is essentially **no free pruning**. `--prune-cnn`'s 5529 sits
past every knee.

**Nothing reaches 250 KB while holding fa.** The smallest index keeping `missed`
within 2 of baseline at fa ≤ 1 is 634 KB — 2.5x over the SRAM budget. The honest
statement of the trade at **2845 negatives / 4000 vectors / exactly 250 KB**,
threshold 136:

    baseline  fa=1  wa=13  missed=14      656 KB
    250 KB    fa=6  wa=13  missed=14      250 KB

The IoT side is bit-for-bit the baseline. The entire price of a 2.6x byte cut is
**5 extra false actuations in 1335 dev non-commands (0.37% vs 0.07%)**. That is
a rejection cost, not a missed-command cost — and it is the one property this
project treats as non-negotiable, so it is not taken by default. It is also the
same order as the ±2.5% dev standard error, i.e. at the edge of what this dev
set resolves.

Graded condensation dominates random `--prune-neg=K` at every matched byte count
— e.g. at 218 KB, fa 6 vs 10 and missed@fa≤3 of 17 vs 30 — which extends the
earlier condensation-beats-random result rather than overturning it.

## The shipped index is pruned to 240 KB for full SRAM residency

> **Sizes below are the v1 64-byte format.** The pruning decision — 3840
> vectors, `RSHIP_NEGTOP=2685`, `fa` 1 → 6 — stands unchanged and is what still
> ships. Only the *storage* moved: the same 3840 vectors are 137 KB in the v2
> exception format, not 240 KB, and route in 4.3 ms rather than 6.3. See
> [The sign plane is an exception set](#the-sign-plane-is-an-exception-set-not-a-bit-plane).

The chunked lift put 34% of the 656 KB index in SRAM. The obvious next question
is what size would be **100%** resident, and it has an exact answer: chunks are
8 KB, the heap reserve is 40 KB, and 30 chunks is the most that fits — so
**3840 vectors (30 x 128) is the largest fully-resident index.** That is a
memory number, not an accuracy number, and it is now `RSHIP_NEGTOP = 2685` in
`router.h`, next to `RSHIP_TH`.

Measured on device, shipped firmware:

    index 3840/3840 vectors in SRAM (100%), 30 chunks of 128
    addressing verified over all 3840 vectors: 0 MISMATCHED
    needs 253440 B | largest contiguous block 163840 B | total free 40144 B

    "turn the lights on"   ACTUATED  light -> ON                  6449 us
    "start the vacuum"     ACTUATED  cleaner -> START             6308 us
    "asdf qwer zxcv"       REJECTED  score 67 <= threshold 136    6285 us

**34.3 → 6.3 ms**, and flash is now untouched on every query. The IoT scores are
*identical* to the 656 KB build (227 and 187), which is the sweep's structural
claim confirmed on hardware: at th=136 a negative is never an IoT utterance's
nearest neighbour, so removing negatives cannot move an IoT score.


### With WiFi running it is 70% resident, not 100%

> **Superseded by the v2 format.** This measured the v1 64-byte index, which at
> 240 KB could not fit alongside the WiFi stack. At 137 KB it does: the shipped
> firmware reports 3840/3840 resident (100%) with WiFi up, entirely in DRAM
> without touching the IRAM-only pool, and routes in 4.3 ms with the radio
> associated — verified through a live TLS handshake. The reasoning below about
> chunked degradation remains correct and is why this was survivable at all.

The 295 KB free-heap figure everything above rests on was measured with the WiFi
stack **never started**. `CONFIG_ESP_WIFI_ENABLED=y` is the ESP-IDF default so
WiFi is compiled in, but `product.c` never calls `esp_wifi_init`, and a stack
that is not started allocates nothing. For a device that actually talks to
anything, that is not the heap it will have.

Measured rather than estimated — a temporary build that brings up NVS, netif,
the event loop and `esp_wifi_start()` **before** the index is lifted:

    index 2688/3840 vectors in SRAM (70%), 30 chunks of 128
    needs 253440 B | largest contiguous block 110592 B | total free 40240 B

    "turn the lights on"   ACTUATED  light -> ON     9298 us
    "start the vacuum"     ACTUATED  cleaner -> START 9181 us

**6.3 → 9.3 ms.** WiFi takes ~72 KB of liftable heap (the largest block falls
163,840 → 110,592 B), which pushes 9 chunks — 1152 vectors — back to flash. The
reserve held in both cases: 40,144 B free without WiFi, 40,240 B with.

This is the chunked design behaving as designed. It **degrades, it does not
fail**: the same 30 chunks are scanned, 21 from SRAM and 9 from flash, the
addressing check still passes over all 3840, and the scores are unchanged (227
and 187, identical to every other build). A flat all-or-nothing lift would have
dropped to 43.9 ms here instead of 9.3.

It also prices the cliff exactly. 9 flash chunks x 128 vectors x 2551 ns =
2.94 ms, predicted 9.24 against 9.3 measured — **0.33 ms per unlifted chunk**.
That is the marginal cost of the index growing past 3840 vectors, and the reason
3840 is a hard ceiling rather than a soft target: chunk 31 does not cost a
little, it costs a third of a millisecond, every query, forever.

I predicted ~7 chunks and ~8.6 ms before measuring. It is 9 and 9.3 — the
estimate was optimistic, which is the direction estimates about spare memory
usually go.

### What it costs, stated plainly

| | recall | fa | wa | missed | index | resident | per query |
|---|---|---|---|---|---|---|---|
| unpruned | 85.9% ±2.5 | **1** | 13 | 14 | 656 KB | 34% | 34.3 ms |
| **SHIPPED** | 85.9% ±2.5 | **6** | 13 | 14 | 240 KB | **100%** | **6.3 ms** |

Every column is identical except `fa`. The entire price of a 2.7x smaller
footprint and a 5.4x faster scan is **5 extra false actuations in 1335 dev
non-commands** — 0.45% against 0.07%.

This is a deliberate regression on the one property
[the metric section](#the-accuracy-metric-is-blind-to-false-actuations--read-every-number-above-with-this-in-mind)
says to weigh above recall. It is recorded here as a trade that was *chosen*,
not as an improvement. Two things make it defensible and neither makes it free:
the IoT side is bit-for-bit unchanged, and 5 events is the same order as the
±2.5 dev standard error, so dev cannot resolve this finely. The held-out
question is pre-registered below.

`mkblob` now **defaults** to this configuration, so `mkblob <data> out.bin`
reproduces the shipped blob exactly and `regress.sh` can prove reproducibility
against the blob that actually ships. Building any other prune config without an
explicit `--threshold=` is refused. That guard previously listed `dup`, `cnn` and
`neg_k` by hand and so never covered `neg_top` — a `--prune-negtop` blob would
have silently taken `RSHIP_TH`, which is precisely the failure the guard exists
to prevent. It now compares the whole `prune_opt` struct against the shipped one
and cannot go stale when a mode is added.

To build the unpruned index instead:
`mkblob <data> out.bin --prune-negtop=0 --threshold=136`. The firmware lifts
whatever fits and is correct either way.

## Test evaluation #6 — PRE-REGISTERED: does the pruning cost transfer?

Written before running. Not yet run — this section is the prediction, and the
result will be appended beside it whether or not it agrees.

**Baseline**, eval #3 at th=136 on 220 IoT / 2754 non-commands:

    TEST baseline   recall 84.1%   fa=8   wa=15   missed=20

**The mechanism being tested.** On dev, `missed` and `wa` are *constant* at
14/13 across the entire negative sweep — 9345 negatives down to 1 — because a
negative is never an IoT item's argmax at th≥136. If that is structural rather
than a dev accident, the held-out IoT numbers must not move at all, and the
entire effect must appear in `fa`.

**Predictions.**

1. **`missed` ≤ 20, and most likely exactly 20.** Pruning a negative can only
   change an IoT outcome if that negative *was* the argmax — in which case the
   item was scored "none" and counted missed, so removing it can only help.
   `missed` rising at all falsifies the mechanism.
2. **`wa` = 15, unchanged.** Same argument; `wa` is decided among positives.
3. **`fa` rises to 18, range 12–26.** Dev fa went 1 → 6 on 1335 non-commands
   (+0.37 pp). Test carries 2754, so a constant *rate* increase predicts
   8 + 0.37% x 2754 ≈ +10. The interval is wide because the dev counts are tiny
   and Poisson noise at n=1..6 is large.
4. **Recall 84.1%, unchanged**, following directly from 1 and 2.

**Falsifiers, and what each would mean.**

- `missed` > 20 or `wa` ≠ 15 → the "negatives never win an IoT argmax" mechanism
  is dev-specific. The whole justification for this trade collapses and the
  240 KB index should be reverted regardless of what `fa` did.
- `fa` outside 10–30 → the rate model is wrong. Below 10 I over-predicted the
  cost; above 30 the trade is worse than dev indicated.
- **`fa` > 30 (>1.1% of non-commands) → recommend reverting to 656 KB.** Stated
  now, before the number is known, so the decision is not made after seeing it.

This is the prediction eval #3 taught: a dev gain at a changed operating point
did **not** transfer, and 126 was reverted. The difference here is that the dev
evidence is a claimed *invariance* rather than a claimed gain, which is a
stronger thing to test and an easier thing to break.

## Test evaluation #6 — RESULT: the invariance transferred, and the cost is half what I predicted

Run at `73a97fc`, clean tree, `make testset-ship` (`--test --ship`, threshold
pinned at 136 — no auto-tuning, so no repeat of eval #5's deviation). Budget
entry 8. 220 IoT, 2754 non-commands.

    TEST baseline (656 KB, eval #3)   recall 84.1% ±2.5   fa= 8   wa=15   missed=20
    TEST SHIPPED  (240 KB, this run)  recall 84.1% ±2.5   fa=12   wa=15   missed=20

### Predictions scored: 4 of 4

| # | predicted | actual | |
|---|---|---|---|
| 1 | `missed` ≤ 20, most likely exactly 20 | **20** | **hit, exactly** |
| 2 | `wa` = 15, unchanged | **15** | **hit, exactly** |
| 3 | `fa` = 18, range 12–26 | **12** | hit — at the very bottom of the interval |
| 4 | recall 84.1%, unchanged | **84.1%** | **hit** |

**The mechanism is real and it is not dev-specific.** `missed` and `wa` are
*bit-identical* to the unpruned baseline on held-out data. That was the claim
the whole trade rested on — that at threshold 136 a negative is never an IoT
utterance's nearest neighbour, so removing negatives can only change what gets
rejected — and it survived the strongest test available to this project. None of
the falsifiers fired: `missed` did not rise, `wa` did not move, `fa` landed well
inside 10–30 and nowhere near the 30 that would have meant reverting.

**The 240 KB index stands.** 2.7x smaller, 5.4x faster, four extra false
actuations in 2754 non-commands.

### Where the prediction was wrong, and it matters

Point estimate 18, actual 12. The interval saved me; the model did not.

I predicted by transferring the *absolute rate increase*: dev fa went
1/1335 → 6/1335, i.e. +0.375 pp, so 8 + 0.00375 × 2754 ≈ 18. The actual increase
was +4/2754 = **+0.145 pp**, less than half. The additive-rate model is wrong.

What did transfer is the **final rate**, almost exactly:

    dev   shipped   6/1335 = 0.449%
    test  shipped  12/2754 = 0.436%

while the two baselines were nowhere near each other (0.075% dev vs 0.29% test).
So the pruned index converges to ~0.44% false actuations on both splits, and the
*unpruned* index is what differed between them. I have one observation of this
and the counts are small enough that Poisson noise could account for a good deal
of it — recorded as something to watch, not a finding.

### The representation claim at the shipped operating point

The paired comparison also re-ran, since `--ship` now prunes both variants:

    binary (1 bit)      75.5% ±2.9   fa=10  wa=14  missed=40   120 KB  th=136
    twin-ternary (2b)   84.1% ±2.5   fa=12  wa=15  missed=20   240 KB  th=136
      paired, overall:  fixed 29   broke 12   p = 0.0115   SIGNIFICANT

Stronger than eval #5's p=0.0288, and it holds at the configuration that
actually ships rather than at an unpruned one nobody deploys. **Read with the
caveat it deserves**: this is the fifth usable read of the same held-out set, so
it is not an independent replication, and no multiple-comparison correction has
been applied to that p-value. The honest summary is that the effect keeps
appearing at the same magnitude, not that it has been independently confirmed
five times.

## A functional WiFi stack, measured: what it costs and what survives it

Every residency figure before this one was taken with `esp_wifi_start()` called
but **nothing associated** — no AP, no DHCP, no sockets, no TLS. Those are
boot-time numbers, and they are the wrong numbers to size a reserve from.

`wifiprobe.c` (`MOGWAI_PROBE=1`) reports free heap at each stage and, crucially,
`esp_get_minimum_free_heap_size()`. Measured on a real AP:

    boot                       273680
    nvs_flash_init             271972
    netif + lwIP               262460   (-9512)
    esp_wifi_init              227444   (-35016)   <- the radio driver
    esp_wifi_start             225524
    associated + DHCP          224436   min-ever 221720
    plain TCP + HTTP    200    223812   min-ever 217780
    TLS handshake       200    223292   min-ever 176804
    second TLS (warm)   200    223292   min-ever 176772

**A TLS handshake dips 46,716 B below steady state.** Steady-state free moves by
about **1.1 KB** across the same fetch, so sampling free heap before and after
would have under-reported the requirement by a factor of **45**. The warm second
handshake barely moves the low-water mark (32 bytes), so the peak is
per-handshake and does not compound.

The shipped `SRAM_RESERVE` was **40,960 B**, chosen by eye. It is smaller than
the transient. A product that lifted the index down to that reserve and then
made an HTTPS request would have died — on the first request, in the field, not
at boot. `LIFT_RESERVE_TLS` is now 61,440.

### The IRAM-only pool is free money, and the guard is the point

IRAM permits only 32-bit accesses, so `malloc` cannot use it — which is exactly
why the mbedTLS transient cannot touch it either. `tvec` is `uint32_t m[8]` +
`uint32_t s[8]`, so the scan (whole-word reads) is legal there; `ACT` is
`uint16_t` and never is. `memcpy`/`memcmp` fault on it, hence word-wise copy and
compare in `lift.c`.

The first attempt used `MALLOC_CAP_EXEC | MALLOC_CAP_32BIT` unguarded and looked
excellent — 93% resident, 7.07 ms — because `MALLOC_CAP_EXEC` **also draws from
the D/IRAM regions, which are ordinary DRAM to everything else.** It had
quietly spent the TLS budget, leaving 15,900 B free. The guard is not an
address-range test (chip-specific, easy to get subtly wrong) but a direct
question: *did this allocation consume any of the 8-bit-capable pool?* If it
did, give it back.

### The result, with the index lifted and TLS afterwards

    associated + DHCP          220316
    index lifted                65104   2816/3840 = 73%
                                        2304 DRAM + 512 IRAM-only, 0 MISMATCHED
    TLS handshake       200     64368   min-ever 17508
    second TLS (warm)   200     64368   min-ever 17508

**TLS completes with the index resident**, and the low-water mark is 17,508 B.
The reserve did its job: 61,440 held back, 46,860 consumed by the handshake,
~14.6 KB genuinely spare. Without the IRAM-only chunks it would have been 2304
vectors — **60%** — so IRAM is worth 13 points here, at zero cost to the TLS
budget. Projected latency at 73% is ~8.8 ms against 6.3 ms with no network.

**One failure, reported as observed:** the plain-HTTP fetch to `neverssl.com`
returned `ESP_ERR_HTTP_CONNECT` (`select()` timeout). That is a reachability
problem with that host, not a heap problem — the TLS fetch to a different host
succeeded immediately afterwards with *less* heap available. Recorded rather
than retried into a clean-looking run.

### End to end: one firmware that routes AND talks WiFi

The measurements above proved the memory arrangement. They did not prove a
device: `product.c` had no network and `wifiprobe.c` never routed. `MOGWAI_WIFI=1`
builds `product.c` with the stack, associating **before** the lift so it sees a
connected heap, and switching the reserve to `LIFT_RESERVE_TLS`.

    index 2816/3840 vectors in SRAM (73%), 30 chunks of 128
      2432 in DRAM, 384 in the IRAM-only pool, 0 MISMATCHED
    wifi: connected, IP 10.148.218.2   (reserve 61440 B)

    "turn the lights on"   ACTUATED  light -> ON      9186 us
    !tls                   HTTP 200, 15566 bytes      min-ever 15188
    "turn the lights off"  ACTUATED  light -> OFF     9211 us
    "make some coffee"     ACTUATED  coffee -> BREW   9079 us
    "asdf qwer zxcv"       REJECTED  score 67         9042 us

**Routing and TLS coexist, and routing is unaffected by having done a
handshake.** Every score is identical to the no-network build — 227, 207, 225,
67, 222 — which is the partial-residency claim confirmed where it matters:
chunks left in flash score exactly as chunks in SRAM.

    no network, 100% resident    6465 us
    WiFi + TLS,  73% resident    9186 us

The cost model predicts 8.82 ms at f=0.733; measured 9.19 ms, **4% over**. The
gap is not memory — it is that the scan now shares a core with the WiFi task.
That is the first time in this project a measurement has exceeded the model, and
the reason is plausible rather than proven.

Low-water mark across the whole session was **15,188 B**, with two handshakes.
The reserve is doing exactly what it was sized to do.

**Reproducibility:** the trimmed WiFi buffers live in tracked
`esp32_router/sdkconfig.wifi`, not in a `/tmp` fragment. The first run of this
measurement used an untracked overlay and was therefore not reproducible; it was
re-run from the tracked file and produced identical figures.

### A HELD-OPEN TLS session costs 34.6 KB, not 1 KB

Every TLS figure above was connect-then-**close**. That is the wrong measurement
for a device with one long-lived connection, and the difference is not small.

mbedTLS holds its record buffers for the life of a connection, sized by
`MBEDTLS_SSL_IN_CONTENT_LEN` / `OUT_CONTENT_LEN`, which IDF defaults to full
16 KB TLS records. Measured with `esp_tls`, session opened, read on, and **held**:

    MBEDTLS_SSL_IN_CONTENT_LEN=16384  OUT=4096
    free 66000 -> 31760 after handshake -> 31368 with session held
    HELD SESSION COSTS 34632 B (steady, while open)
    after 3 s still held: free 22964
    after close:          free 66012

**34,632 B held, not the ~1 KB the connect-then-close runs implied.** It is also
not static: free drifted down a further 8.4 KB over three idle seconds, so a
held session keeps allocating after it settles. Everything returns on close.

Sweeping the buffer that dominates it:

| `IN_CONTENT_LEN` | handshake | held cost | worst-case free |
|---|---|---|---|
| 16384 (IDF default) | OK | 34,632 B | 17,972 B |
| **8192** | OK | **27,056 B** | **28,776 B** |
| 4096 | **FAILS**, `mbedtls_ssl_handshake -0x7200` | — | — |

**4096 is not a tuning choice, it is a broken build** — the server sends records
larger than the buffer and every handshake is rejected, including the plain
`esp_tls` one. 8192 works and buys 7.6 KB of held memory plus 10.8 KB of
worst-case headroom, and now ships in `esp32_router/sdkconfig.wifi`.

**This is server-dependent and the caveat is load-bearing.** 8192 worked against
the endpoint measured here; a server emitting full 16 KB records will fail
exactly as 4096 did. Validate against the endpoint you actually talk to.

### What it means for the reserve

`LIFT_RESERVE_TLS` was sized for a **transient** 46.7 KB handshake that returns.
A held session does not return it, so the arithmetic changes:

- **connect-then-close**: reserve ≥ handshake transient. 61,440 covers it.
- **one held session**: reserve ≥ held cost, permanently, and the handshake that
  creates it peaks higher. Measured worst case 17,972 B free at IN=16384 and
  28,776 B at IN=8192 — both survive, the first uncomfortably.
- **a second handshake while holding** — an OTA, or a reconnect before teardown —
  needs held + transient ≈ 82 KB. **61,440 does not cover that.**

So the one-connection design is the right one on memory grounds as well as
security grounds, and OTA is the case that breaks it: tear the session down
before starting an update.

## The same knob on the shipped index — and where fa actually comes from

The curve above was measured on the unpruned 656 KB index, where `fa=1` and is
flat from 136 to 184. The shipped 240 KB index has `fa=6`, so the question
deserved re-asking: with six false actuations instead of one, several of them
sitting just above the bar, does raising the threshold now buy precision?

Measured on the shipped configuration (`compare --ship --curve`, dev, 192 IoT /
1335 non-commands). `th=136` reproduces the shipped row exactly, so the curve and
the headline numbers agree:

     th   fa   wa   missed   iot_ok
    136    6   13    14   165
    138    4   13    15   164
    140    3   13    17   162
    142    3   13    18   161
    144    3   12    20   160
    146    3   10    23   159
    148    3   10    25   157
    150    2   10    27   155
    152    2   10    29   153
    154    2   10    30   152
    156    2   10    35   147
    158    2   10    36   146
    160    2   10    37   145
    162    2    8    40   144
    ...
    186    1    2    83   107
    188    0    1    86   105
    190    0    1    91   100
    192    0    0    98    94

**`fa=0` requires `th=188`, where `iot_ok` falls from 165 to 105 — recall 85.9%
to 54.7%.** The conclusion holds, for a different reason than before: not
because `fa` is flat, but because it only reaches zero where recall collapses.
Thirty-one points of recall to remove six events in 1335. The threshold is still
the wrong knob.

**One cheap trade is visible and has not been taken.** 136 -> 140 halves `fa`
(6 -> 3) for three IoT items, 165 -> 162, about 1.6 points. That is a far better
exchange rate than anything further along the curve. It is dev-only, and
[test evaluation #3](#test-evaluation-3--result-the-dev-gain-did-not-transfer-126-reverted-to-136)
is the standing warning that a dev threshold gain need not transfer — 126 looked
better on dev too. Recorded as a candidate, not a recommendation.

### Which stage produces which error

`route()` is argmax, then three gates: threshold, label lookup with polarity,
and the `none` check. Attributing the errors:

- **`fa`** — a non-command where argmax lands on a POSITIVE and clears
  threshold. Both must happen.
- **`missed`** — an IoT item where argmax lands on a NEGATIVE (-> "none" ->
  reject), or fails threshold.
- **`wa`** — argmax lands on the wrong positive, **or** argmax was right and
  polarity flipped it.

That last one is worth separating, because polarity swaps only *within* a
sibling pair (`on`<->`off`, `up`<->`dim`). Inspecting all 13 dev `wa` at th=136:

    said lightup    truth lightchange    said lightdim  truth lightoff
    said lightchange truth lightup       said wemo_off  truth hue_lightoff
    said lightoff   truth lightup        said lighton   truth lightdim
    said lightdim   truth lightchange    said lightup   truth lighton
    said lightdim   truth lightoff       said lightdim  truth lightchange
    said lightoff   truth lightchange    said hue_lighton truth wemo_on
    said lightup    truth lightoff

**Not one is a same-pair error.** Every one is cross-family (`off` said as
`dim`) or cross-device. A cross-family error cannot be produced or repaired by
polarity, so the cue system is 13/13 clean on the errors that remain. **Argmax
decides essentially all of it.**

### `fa` is six nameable pairs, not a diffuse property

    188  iot_coffee     "make me happy"
    169  iot_cleaning   "can you please put on music"
    149  iot_cleaning   "can you put this on facebook"
    140  hue_lightoff   "i need you to put walk the dog on my list to do"
    138  iot_coffee     "i would like to talk about it"
    137  iot_cleaning   "please restart the handmaid's tale"

**Four of six are "put … on".** The encoder keys on `on` and `put` n-grams that
"put on music" shares with "turn on the light". That is a representation
collision, not a confidence failure — which is exactly why the threshold cannot
fix it, and why five of the six sit within 33 points of the bar.

The actionable form of this: negatives need no labels, so they can be mined from
any text source, and mining a *phrasing family* ("put on {media, list, social}")
covers the observed collision directly rather than hoping blind negative-count
scaling reaches it. The sweep already showed `fa` is monotone in negative count
and that negatives never cost `missed` or `wa`; this says where to spend them.

Two caveats. Mining against the dev errors themselves would be fitting to dev —
the legitimate version mines the phrasing family from an independent corpus.
And verifying any of it costs a budget unit, of which few remain.

## Is the discarded per-dimension magnitude recoverable? Two oracles, both negative

`t_encode` reduces an `int16_t` count to one trit against a per-dimension
centre. A dimension hit once and hit five times encode identically. That looked
like an information bottleneck worth attacking before touching features, and
LCVDB (a ternary vector DB whose coarse tier is 2 bits/dim and whose fine tier
is MTF7 at 14 bits/dim) suggested the shape: coarse scan, then rerank a
shortlist with the richer code.

### Oracle 1 — is a second tier even reachable? YES

For every dev error, the rank of the exemplar that would give the right answer
(`--rankoracle`), on the shipped 3840-vector index at th=136:

              K=1    K=2    K=4    K=8   K=16   K=32   K=64  >64/none  total
    wrong-act   0      6      8     11     11     12     12        1     13
    missed      9      9     10     10     11     12     13        1     14
    UNBIDDEN    0      4      6      6      6      6      6        0      6

**All six false actuations have a negative at rank 2-3.** Eleven of thirteen
`wa` have the true class within top-8. A perfect reranker over a shortlist of 8
bounds at `wa` 13 -> 2 and `fa` 6 -> 0.

**And nine of fourteen `missed` are rank 1 with gap 0** — the argmax is already
correct and the THRESHOLD rejects them. Two thirds of the missed population was
never a ranking problem, and no reranker touches it. That is an acceptance
problem, and nobody was looking there.

### Oracle 2 — does the discarded magnitude reorder them? NO

Fine score over the residual the sign bit destroys,
`delta_i = acc[i]*RSCALE - centre[i]*total`, quantised to B bits against each
vector's own max (an MTF7-style adaptive scale), same Dice denominator as the
coarse metric. Ordering only: accept/reject still uses the coarse score, so
`missed` cannot move and any change is attributable to reordering
(`--rerankoracle`).

    baseline (coarse argmax)      fa= 6  wa=13  missed=14  iot_ok=165
    best of 24 configs (full, K=2) fa= 6  wa=13  missed=15  iot_ok=164
    worst (4 bits, K=16)          fa=14  wa=24  missed=15  iot_ok=153

Nothing improves, and it degrades **monotonically with K** — the more candidates
the fine metric is allowed to reorder, the worse it does. That is the signature
of a metric that is worse than the one it is reranking, not of missing bits.

### Why, and a correction to my own diagnosis

    residual stats over 211,285 active dims of 3,840 index vectors
      delta > 0 (sign bit set): 209,746  (99.3%)
      |delta|  min=1  mean=13630  max=146327

**The sign plane is 99.3% ones.** `centre[d]` is the mean rate over ALL index
vectors including the ~77% where the dimension is zero, but `t_encode` consults
it only for dims that fired — so any firing dimension clears it. The residual
therefore has almost no sign structure, and a fine metric built on products of
near-always-positive quantities is dominated by magnitude rather than agreement.
It does not nest the coarse metric, which is why more of it made things worse.

I called that a structural flaw and proposed the obvious fix: condition the mean
on firing, so the sign splits evenly. Measured (`--condcentre`, auto-tuned):

    baseline      twin-ternary  85.9% +-2.5   fa=1  wa=13  missed=14  th=136
    condcentre    twin-ternary  65.6% +-3.4   fa=1  wa=19  missed=47  th=130

**Twenty points worse.** The asymmetry is load-bearing, not waste. `t_dot` is
`agree - 2*disagree`; with a near-constant sign plane, `disagree` fires only
when a dimension is genuinely below its diluted mean, which happens when it
fires once inside a long utterance. That is a length signal and it is evidently
carrying real weight. The 32 bytes of `s[]` hold ~0.06 bits per active dim of
entropy and are worth 20 points anyway.

### What this does and does not establish

Established: the right answer is usually present in the coarse top-8, so a
second tier is not blocked by candidate coverage; and two specific uses of the
discarded magnitude are worse than the status quo.

NOT established: that the magnitude is uninformative. Both negatives share one
design fault — neither metric **strictly refines** the coarse one. A fine score
that reduces exactly to `t_score` at minimum precision, with extra bits acting
only as a tie-break, could not do worse than baseline by construction. That is
the principled version of this experiment and it has not been run.

Also unexamined, and now the larger target: the nine rank-1 gap-0 `missed`.

## Test evaluation #7 — PRE-REGISTERED: does boundary-witness selection transfer?

Written before running. **Not yet run.** This section is the commitment; the
result will be appended beside it whether or not it agrees.

### Hypothesis

> Negatives selected for **guarding positive boundaries** transfer better than
> negatives selected for **generic negative-space coverage**.

### Treatment and control

    treatment   --prune-negbound=2685, K=4      (boundary witnesses)
    control     --prune-negtop=2685             (the shipped selector)

Identical budget, identical vector count (3840), identical blob size (240 KB),
identical scan, identical firmware. The only change is *which* negatives survive.
The control must reproduce the frozen baseline or the run aborts —
`control_or_die()`, METHOD 19.

### Baseline, from test evaluation #6 (budget entry 8)

    TEST   recall 84.1% ±2.5   fa=12   wa=15   missed=20   (220 IoT / 2754 neg)

### Predictions

Eval #6 taught that the **final rate** transfers better than the delta: dev
shipped `fa` 0.449% against test 0.436%, while the additive-delta model
over-predicted by 50%. Using the rate model on dev `negbound` (4/1335 = 0.300%):

1. **`fa` = 8, range 6–11.** 0.300% × 2754 ≈ 8.3.
2. **`wa` = 15**, tolerance ±2.
3. **`missed` = 20**, tolerance ±2.
4. **recall 84.1%**, tolerance ±1 point.

Not predictions but structural certainties, checked anyway: blob size 240 KB,
vector count 3840, per-query latency unchanged.

### Falsifiers

- **`fa` ≥ 12** — no improvement. The candidate is killed.
- **`wa` or `missed` degrades by more than 2** — the mechanism has side effects
  it structurally should not have, since negatives cannot win an IoT argmax at
  th=136. Killed, and the invariance claim needs re-examining.
- **recall drops more than 1 point** — killed.

### No adaptation after evaluation

If `K=4` underperforms, the candidate is **cut, not re-tuned**. Explicitly:

> **K=16 is not eligible for substitution.** It is known to give `fa=2` on dev,
> which is exactly what makes it dangerous after the fact. Neither is the
> budget, the witness definition, nor the selector rule eligible for adjustment
> in light of the result.

This is the discipline that cut the selector in eval #4 rather than re-tuning
its gate, and reverted threshold 126 in eval #3.

### Why this candidate and not the others

The `P - N` abstention margin buys more (+32 commands at `fa <= 1`) but only at
an operating point nobody has decided to ship. Lexical corroboration ties at the
shipped budget. `negbound` is the only candidate that improves the safety metric
at the operating point in use, at zero cost in bytes, latency, recall, `wa` or
`missed` — and its selection touches no dev data at all.

## Test evaluation #7 — RESULT: no falsifier fired, and the effect is one event

Run at `e982968`, budget entry 9, `make testset-negbound`. 220 IoT / 2754
non-commands.

    baseline  (#6, negtop)    recall 84.1% ±2.5   fa=12   wa=15   missed=20
    treatment (#7, negbound)  recall 84.1% ±2.5   fa=11   wa=15   missed=20

### Predictions scored: 4 of 4, and the one that mattered barely

| # | predicted | actual | |
|---|---|---|---|
| 1 | `fa` = 8, range 6–11 | **11** | hit — at the extreme top of the range |
| 2 | `wa` = 15 ±2 | **15** | **hit exactly** |
| 3 | `missed` = 20 ±2 | **20** | **hit exactly** |
| 4 | recall 84.1% ±1 | **84.1%** | **hit exactly** |

No falsifier fired. `fa` did not reach 12, `wa` and `missed` did not move at all,
recall did not drop. **On the letter of the pre-registration the candidate
survives.**

### On the spirit, it is one event

Dev showed `fa` 6 → 4, a 33% reduction. Held-out shows **12 → 11, an 8%
reduction — a single false actuation out of 2754**. The Poisson interval on a
count of 12 is roughly ±7; a difference of one sits deep inside it. This
measurement cannot distinguish "slightly better" from "identical".

My point estimate of 8 was 50% optimistic, and it landed at the very edge of the
interval I had drawn around it. Eval #6's point estimate was 50% pessimistic.
Two evaluations, two point estimates wrong by half in opposite directions, both
saved by wide intervals. **The rate model is not good enough to predict a
magnitude; it is only good enough to bracket one.**

### What IS established, and it is not nothing

`wa` and `missed` are **bit-identical** across the two selectors on held-out
data — 15 and 20 in both. This is the second held-out confirmation of the
structural claim that at th=136 a negative never wins an IoT argmax, so changing
which negatives are stored cannot touch command behaviour. Eval #6 confirmed it
across a change in negative *count*; eval #7 confirms it across a change in
negative *identity* at fixed count. That invariance is now well supported.

### Verdict

The hypothesis was that selection-by-function transfers better than
selection-by-appearance. **The direction transferred; the magnitude did not.**

Adopting `negbound` costs nothing measurable: identical bytes, identical vector
count, identical latency, identical recall, `wa` and `missed`. It is directionally
better on both splits and its selection touches no dev data. That supports
"harmless and probably marginally better" — not "better".

**No adaptation.** K stays 4. K=16 remains ineligible, the budget is not
adjusted, and the selector rule is not touched in light of this result, exactly
as pre-registered.

### Provenance note

The row is stamped `DIRTY`. The uncommitted change at run time was the
`testset-negbound` Makefile target itself — the invocation wrapper, added
minutes before. `c/src/compare.c` and `c/src/prune.c` were both committed at
`e982968`, so the measurement came from committed code. The correct order was to
commit the target first, and it is recorded here rather than tidied.
