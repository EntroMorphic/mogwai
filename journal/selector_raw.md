# RAW — building a selector

Everything observed. No interpretation, no filtering.

## The prize

    both correct        156 (81.2%)      router only           9
    prior only           16              neither              11
    router 165 (85.9%)   word prior 172 (89.6%)
    ORACLE per item: 181 (94.3%)

8.4 points above the shipped router. The word prior ALONE outscores the router
on IoT classification. Three attempts to combine them have failed: signature
prior (inert), word prior as soft bias (inert), hard word cue (harmful), class
delegation (harmful, wa 13->14 constrained / 13->15 not).

## Instrumented every signal available at inference time

`--seldump`, 192 dev IoT items: router top score, router margin (top minus best
different-class), prior margin, agreement, polarity fired, word count, and which
channel was actually right.

    cell         n     top      margin   prmarg   agree   pol     words
    both         162   191.6    23.5     45.1     1.00    -0.20   5.7
    router_only  10    164.4    11.1      7.8     0.00     0.40   6.2
    prior_only   10    157.0    11.6     24.7     0.00     0.10   7.7
    neither      10    152.1    12.2     21.5     0.30     0.10   4.8

NOTE: these cell counts (10/10/10) differ from the `--channels` counts
(9/16/11). `--channels` applies the threshold and the veto path; `--seldump`
evaluates class only, no threshold. Both are internally consistent; they are
measuring different things and must not be quoted interchangeably.

## The disagreement cells, item by item, sorted by prior margin

    router_only  prmarg= 0   top=140  |pol|=1  words=5
    router_only  prmarg= 0   top=149  |pol|=1  words=2
    router_only  prmarg= 0   top=159  |pol|=1  words=2
    router_only  prmarg= 0   top=189  |pol|=1  words=12
    router_only  prmarg= 2   top=168  |pol|=1  words=6
    router_only  prmarg= 3   top=178  |pol|=1  words=5
    router_only  prmarg= 5   top=125  |pol|=1  words=5
    ----------------------------------------------- no prior_only item below 8
    prior_only   prmarg= 8   top=144  |pol|=1  words=6
    prior_only   prmarg= 9   top=188  |pol|=0  words=7
    prior_only   prmarg=16   top=141  |pol|=1  words=3
    prior_only   prmarg=20   top=162  |pol|=1  words=9
    router_only  prmarg=20   top=164  |pol|=1  words=10
    prior_only   prmarg=21   top=192  |pol|=1  words=5
    router_only  prmarg=22   top=168  |pol|=1  words=9
    prior_only   prmarg=26   top=128  |pol|=1  words=13
    router_only  prmarg=26   top=204  |pol|=1  words=6
    prior_only   prmarg=29   top=170  |pol|=1  words=6
    prior_only   prmarg=31   top=177  |pol|=1  words=8
    prior_only   prmarg=37   top=145  |pol|=0  words=4
    prior_only   prmarg=50   top=123  |pol|=0  words=16

## Threshold behaviour, as measured

    trust prior when prmarg>= 8 : catches 10/10 prior_only, flips 3/10 router_only -> net +7
    trust prior when prmarg>=10 : catches  8/10,             flips 3/10            -> net +5
    trust prior when prmarg>=25 : catches  5/10,             flips 1/10            -> net +4
    trust prior when prmarg>=30 : catches  3/10,             flips 0/10            -> net +3

## Other observations, recorded without weighting

- Router `margin` does NOT separate the cells: 11.1 vs 11.6. The router's own
  confidence carries no information about whether it is right.
- `top` score separates weakly and in the counter-intuitive direction:
  router_only 164.4 vs prior_only 157.0.
- `both correct` has prmarg 45.1, far above either disagreement cell. High prior
  margin is associated with agreement, not just with prior correctness.
- 4 of 10 router_only items have prmarg **exactly 0** — the prior has no opinion.
- |pol| fired on 9/10 router_only and 7/10 prior_only.
- `neither` (10 items) has the lowest top score, 152.1.

## What has NOT been measured

- Whether any of this holds on the index itself (cross-validation).
- Whether a selector changes `fa` on the 1335 negatives — seldump skips them.
- Whether the threshold survives on held-out data. Test budget stands at 3.
