# Oracles 1 & 2, and the rejection boundary

*Measured 2026-08-23. Dev only — no test budget spent. All numbers reproducible
from the flags named; nothing here is an estimate.*

Prompted by reading `../lcvdb/` (a ternary vector DB: 2 bits/dim coarse scan,
then a bounded shortlist rerank in MTF7 at 14 bits/dim). LCVDB solves
**retrieval**, which mogwai does not have as a problem — its argmax is exact
over 3,840 vectors. What transferred was the *shape*: coarse scan, then let a
richer signal decide the ordering. LCVDB showed where to look, not what to use.

---

## The one-line summary

The remaining error splits into **two unrelated mechanisms**, and only one of
them is about ranking:

- **ordering failure** — the right answer is in the shortlist, mis-ordered
- **abstention failure** — the right answer is *already rank 1* and the global
  threshold vetoes it

Two thirds of `missed` is the second kind. No reranker touches it.

---

## Oracle 1 — is a second tier reachable? `--rankoracle`

For every dev error, the rank of the exemplar that would give the right answer:
a correct-class exemplar for `wrong-act`/`missed`, any negative for `UNBIDDEN`.

### At the shipped threshold (th=136)

              K=1    K=2    K=4    K=8   K=16   K=32   K=64  >64/none  total
    wrong-act   0      6      8     11     11     12     12        1     13
    missed      9      9     10     10     11     12     13        1     14
    UNBIDDEN    0      4      6      6      6      6      6        0      6

- **All six false actuations have a negative at rank 2–3.**
- Eleven of thirteen `wa` have the true class within top-8.
- **Nine of fourteen `missed` are rank 1 with gap 0** — argmax already correct,
  killed by the threshold. This was not what we were looking for and is the
  larger finding.

### With the threshold removed (th=0)

              K=1    K=2    K=4    K=8   K=16   K=32   K=64  >64/none  total
    wrong-act   0      6      8     11     12     13     14        1     15
    missed      0      0      1      1      1      2      2        1      3
    UNBIDDEN    4     26     33     37     38     38     38        0     38

**All 38 false actuations have a negative within top-16; 37 within top-8; 26 at
rank 2.** (The four at "K=1" are score ties, not true rank-1 negatives — a
rank-1 negative would already be a correct reject.)

---

## What the threshold is actually buying

Removing it entirely — which *is* contrastive rejection under the coarse metric,
since a positive at rank 1 means best-positive > best-negative by definition:

    th=136    fa= 6  wa=13  missed=14  iot_ok=165   (85.9%)
    th=  0    fa=38  wa=15  missed= 3  iot_ok=174   (90.6%)

**The global threshold buys 32 false-actuation rejections at the price of 9
missed commands.** Stated that way it is obviously a trade worth re-examining
rather than a law.

### The ceiling of the combination

Contrastive rejection **plus a perfect reranker over top-8**:

| | shipped | ceiling |
|---|---|---|
| `fa` | 6 | **1** |
| `wa` | 13 | 4 |
| `missed` | 14 | 2 |
| recall | 85.9% | **96.4%** |

This **exceeds the old channel-selection oracle of 94.3%**, and correctly so:
that oracle bounded one selector architecture over existing channels. A
different decision rule is a different hypothesis space and gets its own bound.

Of the residual, at least two `wa` are corpus defects rather than model error —
see below.

---

## Oracle 2 — does the discarded magnitude do the reordering? `--rerankoracle`

`t_encode` reduces an `int16_t` count to one trit, so a dimension hit once and
hit five times encode identically. The residual it destroys is the distance from
its own decision surface:

    delta_i = acc[i]*RSCALE - centre[i]*total

Fine score: products of those residuals over dims active in both, quantised to
B bits against each vector's own max (MTF7-style adaptive scale), same Dice
denominator. **Ordering only** — accept/reject kept on the coarse score so
`missed` cannot move and any change is attributable to reordering.

    baseline (coarse argmax)        fa= 6  wa=13  missed=14  iot_ok=165
    best of 24 configs (full, K=2)  fa= 6  wa=13  missed=15  iot_ok=164
    worst (4 bits, K=16)            fa=14  wa=24  missed=15  iot_ok=153

**Negative, and monotonically worse with K** — the signature of a metric worse
than the one it is reranking, not of missing bits.

### Why: the sign plane is 99.3% ones

    residual stats over 211,285 active dims of 3,840 index vectors
      delta > 0 (sign bit set): 209,746  (99.3%)
      |delta|  min=1  mean=13630  max=146327

`centre[d]` is the mean rate over **all** index vectors, including the ~77%
where that dimension is zero — but `t_encode` consults it only for dims that
fired, so any firing dimension clears it. The residual therefore has almost no
sign structure, and a metric built on products of near-always-positive
quantities is dominated by magnitude rather than agreement. **It does not nest
the coarse metric**, which is why more of it made things worse.

### The obvious fix is 20 points worse

Condition the mean on firing so the sign splits evenly (`--condcentre`):

    baseline     twin-ternary  85.9% ±2.5   fa=1  wa=13  missed=14  th=136
    condcentre   twin-ternary  65.6% ±3.4   fa=1  wa=19  missed=47  th=130

`t_dot` is `agree - 2*disagree`. A near-constant sign plane means `disagree`
fires only when a dimension is genuinely *below* its diluted mean — which
happens when it fires once inside a long utterance. **That length signal is
worth 20 points.** The asymmetry is load-bearing, not waste. The "structural
flaw" diagnosis was wrong and the experiment said so.

---

## What is established, and what is not

**Established**

- Candidate coverage is not the blocker. The right answer is in the coarse
  top-8 for 37/38 `fa` and 11/13 `wa`.
- Two thirds of `missed` is an abstention problem, not a ranking problem.
- The global threshold trades 9 missed commands for 32 rejections.
- Two specific uses of the discarded magnitude are worse than the status quo.
- The 99.3%-ones sign plane is load-bearing.

**Not established**

- That the magnitude is uninformative. Both negatives share one design fault:
  **neither fine metric strictly refines the coarse one.** A fine score that
  reduces exactly to `t_score` at minimum precision, with extra bits acting only
  as a tie-break, cannot lose to baseline by construction. That experiment has
  not been run and is the honest next step.
- That any of this transfers. **The channel-selection oracle measured 94.3% and
  the selector built to capture it failed held-out and was cut** (test
  evaluation #4). An oracle is an upper bound, not a result.

---

## Two scoreboards, not one

Two of the thirteen `wa` are corpus defects:

    "turn out the lights"    labelled iot_hue_lightUP
                             the router said lightoff, which is CORRECT
    "turn on kitchen light"  labelled iot_wemo_on, rank 19
                             which physical device that is cannot be inferred
                             from the utterance at all

So the target should not be "100% accuracy". A full-information oracle reaching
100% on dev would be **learning the annotator's mistakes**, and should be read
as a warning sign rather than a result. Keep both numbers:

- **corpus accuracy** — against the labels exactly as supplied
- **adjudicated accuracy** — against the meaning, with defects reported, never
  silently corrected

A system that scores 99.x% because it refuses to call *"turn out the lights"* a
light-*up* command is the better system.

---

## Where I would go next, in order

1. **The abstention side, not the ranking side.** It is the larger error class,
   the mechanism is simpler, and it is the same lever as the zero-`fa` goal seen
   from the other end. The measured trade (32 rejections for 9 commands) is the
   thing to attack.
2. **A strictly-refining fine metric** — one that provably reduces to `t_score`.
   Only then does a precision sweep mean anything.
3. **Only then** the flash sidecar: MTF7 at 14 bits/dim × 256 = 448 B/vector =
   1.72 MB flash for 3,840; reading K=16 per query is ~7 KB ≈ 0.3 ms at the
   measured 24 MB/s, about 5% of the current latency, with the SRAM-resident
   coarse tier untouched.

Verifying any of it on held-out data costs a budget unit, of which few remain.

## Reproducing

    c/bin/compare --ship --rankoracle              # oracle 1 at th=136
    c/bin/compare --ship --fixth=0 --rankoracle    # oracle 1 with no threshold
    c/bin/compare --ship --rerankoracle            # oracle 2 + residual stats
    c/bin/compare --condcentre                     # the 20-point negative
    c/bin/compare --ship --curve                   # the threshold curve

---

# Attacking the abstention side: a margin rule

*Added 2026-08-23, same session. `compare --ship --abstain`. Dev only.*

Oracle 1 said the threshold costs 9 rank-1-correct commands to buy 32
rejections, and that the competing negative usually sits at rank 2. So test the
relative rule directly:

    P = best score among POSITIVE (command) exemplars
    N = best score among NEGATIVE ("none") exemplars
    accept iff  P - N > margin

**The shipped rule is already the m=0 case of this.** Argmax over everything,
reject if a negative won, else reject if below threshold — "a negative won" is
exactly `P - N < 0`. So the honest baseline is `P > th AND P - N > 0`, and the
question is only whether a *positive* margin buys anything.

*(First version of this compared against `m = -infinity` — thresholding the best
positive while ignoring whether a negative outranked it. That drops the
none-check the router already has, and inflated the apparent gain from +3 to
+38 commands. Strawman baselines flatter in exactly this way.)*

## The frontier

Best `iot_ok` reachable at each false-actuation budget, sweeping th 0..220 and
margin 0..100:

      fa<=              shipped rule (m=0)             with margin (best m)
         0  th=188 ok=105 wa= 1 ms= 86   th=188 m=  0 ok=105 wa= 1 ms= 86
         1  th=170 ok=127 wa= 5 ms= 60   th=134 m= 25 ok=159 wa=12 ms= 21
         2  th=150 ok=155 wa=10 ms= 27   th=126 m= 25 ok=161 wa=14 ms= 17
         3  th=140 ok=162 wa=13 ms= 17   th=136 m= 13 ok=164 wa=13 ms= 15
         4  th=138 ok=164 wa=13 ms= 15   th=134 m= 13 ok=165 wa=13 ms= 14
         5  th=138 ok=164 wa=13 ms= 15   th=126 m= 20 ok=166 wa=14 ms= 12
         6  th=136 ok=165 wa=13 ms= 14   th=110 m= 20 ok=168 wa=14 ms= 10

The `fa<=6` baseline row reproduces the shipped config exactly, which is the
check that the comparison is fair.

**The gain is concentrated entirely in the low-fa regime.**

- `fa <= 1`: **127 -> 159 commands, 66.1% -> 82.8%.** +32.
- `fa <= 2`: +6. `fa <= 3..6`: +1 to +3, inside noise.
- `fa = 0`: **nothing.** 105 either way. The margin cannot reach the endpoint.

That the gain appears where the threshold is doing its most violent work, and
vanishes where the rules coincide, is what you would expect if the mechanism is
real: the absolute bar catches "weak evidence overall", the margin catches
"there is a negative just as good", and they are different failures.

## Cost to ship: approximately nothing

`N` already exists inside the same scan. `route()` would track the best positive
and best negative separately instead of a single argmax — two comparisons per
vector rather than one, no new storage, no new representation, no second tier,
no change to the index or the blob.

## Why this is not yet a recommendation

**Two free parameters selected on dev, at operating points defined by single-digit
event counts.** The `fa<=1` frontier cell is chosen because it produces exactly
ONE false actuation on 1335 dev negatives. The confidence interval on that is
enormous, and this project has been here twice: threshold 126 looked better on
dev and did not transfer (eval #3); the channel selector had a 94.3% oracle and
failed held-out (eval #4).

One thing in its favour: the gain is **not a single lucky cell**. Neighbouring
settings give 157-159 at `fa<=1` (e.g. th=140/m=30 -> 157), against 127 for the
best threshold-only point. A ~30-command effect stable across a neighbourhood is
better evidence than a peak.

What would make it trustworthy, in order: cross-validate the *mechanism* inside
the index rather than tuning the pair on dev; pre-register a single (th, m) with
falsifiers; then spend one budget unit. Not before.

---

# Forensics: what pins the zero-FA frontier

*`compare --ship --faprobe`. Dev only.*

The margin rule moved the `fa<=1` frontier by 32 commands and the `fa=0`
frontier by nothing, so at zero something other than weak evidence or a nearby
negative sets the boundary. A negative is a false actuation at threshold `th`
iff `P > th` and `P > N`, so the negatives with `P > N`, sorted by `P`
descending, **are** the frontier.

34 of 1527 dev negatives qualify. The top one:

    [1] P=188  N=116  gap=72  active=25  ngrams=25  -> iot_coffee
        query    "make me happy"
        nearest+ "make me happy juice"
        nearest- "make me laugh"

**That is a corpus artifact, not a representational collision.** An index entry
reading *"make me happy juice"* is labelled `iot_coffee`, and *"make me happy"*
is a literal prefix of it. A character 3/4-gram encoder must score that highly;
the router is behaving correctly given a defective exemplar. **This is a
benchmark floor.**

## The rest have a nameable structure

    [2] gap=24  "can you please put on music"        vs "can you please put the vacuum on"
    [3] gap=23  "can you put this on facebook"       vs "can you put the vacuum on"
    [5] gap= 1  "i would like to talk about it"      vs "I would like to have a cup of coffee"
    [6] gap= 3  "please restart the handmaid's tale" vs "please start the vacuum"
    [9] gap=25  "i need a taxi ride now"             vs "yo i need a coffee now"
    [11] gap=37 "twenty questions you start"         vs "Can you start the coffee?"

**The carrier phrase dominates the object.** *"can you please put ___ on"* is
five common words; the discriminative content is one rare word contributing a
handful of n-grams against dozens from the frame. The encoder is measuring the
frame, not the filling. This is the same "put ... on" family that accounts for
four of the six false actuations at the shipped threshold.

## Nothing is confidently wrong except the artifact

Gap distribution over all 34 blockers:

    <10: 20    10-24: 9    25-49: 4    50-74: 1    >=75: 0

Twenty of thirty-four sit within 10 points of their nearest negative. The
"representation has put a negative deep inside positive territory" case occurs
**once**, and it is the labelling defect.

That is the useful split: the zero-FA frontier is pinned by a corpus defect, and
everything behind it is a set of narrow margins on shared phrasing.

## The P-ladder

    188  169  149  140  138  137  136  136  135  134  133  129 ...

Removing the one defective index entry moves the `fa=0` threshold from 188 to
170 — recall roughly 105 -> 127 commands (+22), pending an index rebuild to
confirm, since removing an entry perturbs other queries' scores too.

**The lever stops there.** Blockers 2 onward have legitimate command exemplars
as their nearest positive ("can you please put the vacuum on" is a real cleaning
command). Pruning those would trade false actuations for missed commands
directly, which is the trade the threshold already makes.

## What this makes the tight problem

Not "make ranking better". Specifically:

> Separate a query from an index exemplar that shares its entire carrier phrase
> and differs only in the object word, **without disturbing the geometry that
> already works** — noting that `--condcentre`, an obvious attempt to make the
> representation more informative, cost 20 points.

And the honest caveat on the artifact: it was found *via* dev. Removing an index
entry because it causes a dev error is fitting to dev. The principled version is
index-internal — a leave-one-out pass over the index alone, finding positives
that are the nearest neighbour of many unrelated entries, which is what
`--prune-cnn` already does in the opposite direction and what `cuemine`'s
index-only discipline exists for. That has not been run.

---

# Carrier cancellation: which of four worlds are we in?

*`compare --ship --contrast`. Dev only.*

The blockers share an entire carrier phrase with their nearest command and
differ in one object word. The shared frame contributes most of the n-grams, so
whole-sentence similarity is dominated by exactly the part that cannot
distinguish them. So cancel it: given coarse winner `A` and challenger `B`, let

    D = { i : (mask,sign) of A differs from B at i }

and rescore the query against each candidate over `D` alone. The question stops
being *"which sentence does the query resemble"* and becomes *"given these two
readings, what in the query chooses between them"*.

Four worlds, and the diagnostic says which:

1. `B` wins on coarse-over-`D` — carrier dilution, nothing deeper
2. `B` loses coarse-`D` but wins on magnitude-over-`D` — magnitude is the
   missing tier, and `D` is where it may legitimately speak
3. `B` loses both — the features do not encode the distinction
4. exact n-grams separate them but the hashed vectors do not — a hash-collision
   or dimensionality problem, in which case **no metric over these vectors can
   help**

## Result, 16 ordering errors with `B` in top-8

    Dice-on-D puts the right answer first   :  2
    raw dot-on-D puts it first              :  4
    neither; exact n-grams DO separate      :  0   (hash / dimensionality)
    neither; exact n-grams do not either    : 12   (features lack it)

**World 4 is empty.** The hashed 256-dim vectors preserve what the exact
character n-grams say. Hashing and dimensionality are not the problem, and that
hypothesis is cleanly dead — worth knowing, because it would have been the
expensive one to fix.

**World 1 is 4 of 16**, and only with the right normalisation. Carrier
cancellation genuinely helps a minority.

**World 3 is 12 of 16.** For three quarters of these, the *exact* 3/4-character
n-gram overlap does not favour the right answer either:

    q "please restart the handmaid's tale"
    A "please start the vacuum"     shared n-grams = 34
    B "please start the podcast"    shared n-grams = 33

Thirty-four against thirty-three. At the feature level these are a coin flip.
**The limit is not the encoding, not the quantisation, not the scoring — it is
the features.** No second tier over these vectors can recover twelve of sixteen,
because the information is not in them to recover.

### The precise claim, and its limit

World 3 means *unweighted* 3/4-gram overlap does not separate them. It does
**not** mean no feature scheme could. A representation that weighted rare
n-grams above common ones would see "vacuum" and "podcast" against a shared
frame of "please start the" quite differently. That is the IDF-shaped idea, and
this diagnostic is the first evidence that *features* rather than *metric* is
where the remaining signal has to come from.

It remains a change to the encoding, and `--condcentre` is the standing warning
about obvious global improvements to load-bearing geometry: 20 points lost.

### A methodological note that keeps recurring

Dice-on-`D` fixes 2; raw dot-on-`D` fixes 4. **The normalisation choice doubled
the answer.** "Rescore on the disagreement dimensions" does not specify a metric,
and picking one silently would have produced either a weak positive or a
stronger one depending on a decision made in passing. Both are reported for the
same reason METHOD 19 exists.

---

# The whole-word channel, and why IDF is retired too

*`compare --ship --contrast`, word channel added. Dev only.*

World 3 said the limit is the features, so the next candidate was a lexical
channel beside the character one: no weights, no learning, no free parameters —
just whole-word token identity, Dice over normalised token sets.

    WHOLE-WORD channel puts the right answer first : 3 of 16
    ...of the cases nothing else fixed             : 2

Weak. And the reason is more useful than the number.

    q "start brewing please"
    A "please start the podcast"   word A=571
    B "please start the coffee"    word B=571     TIE

    q "turn out the lights"
    A "put out the lights"         word A=750
    B "turn the lights up"         word B=750     TIE

**The query's discriminative token is absent from both candidates.** Shared
words are `{start, please}` either way; `brewing` appears in neither. The
decision is made on carrier evidence because there *is* no object evidence on
either side — not because the object evidence is underweighted.

## This retires IDF as well

The expected mechanism was that rare object words would outvote the common
carrier: `vacuum` against `podcast` through a shared frame of `please start the`.
But that requires the query to **share** its rare word with one candidate.
Here it shares its rare word with neither. Inverse document frequency reweights
*shared* terms; it cannot manufacture a match that does not exist.

    "start brewing please"     -> needs brewing ~ coffee
    "please restart the handmaid's tale" -> needs handmaid's tale ~ media

Those are **semantic** bridges, not lexical ones. Closing them needs a synonym
resource or a learned model, and this project has neither by design.

## What the failure actually is

Not encoding (world 4 empty). Not metric (world 3, 12 of 16). Not lexical
weighting (3 of 16, and the ties explain why). What remains:

> **The index contains no exemplar that uses the query's vocabulary.**

That is a **coverage** problem, and it is the one failure mode this architecture
makes cheap to fix. Adding exemplars is appending vectors — no retraining, no
gradient, no new representation, and the sweep already showed positives cost
`fa` nothing while negatives cost only `fa`. A hundred utterances is ~6 KB.

It is also why the earlier personalisation observation matters more than any
metric work: a nearest-neighbour router's errors concentrate on "no stored
utterance talks like this", and the remedy is stored utterances that do.

## Where that leaves the four candidate directions

    richer quantisation / MTF7 sidecar   RETIRED  world 3
    hashing or larger dimension          RETIRED  world 4 empty
    lexical channel (whole-word)         RETIRED  3 of 16, ties explain it
    IDF / rarity weighting               RETIRED  no shared rare terms to weight
    ------------------------------------------------------------------
    index coverage                       the remaining live hypothesis
    abstention policy (P-N margin)       live, +32 commands at fa<=1

---

# Coverage audit: where does the failing vocabulary live?

*`compare --prune-negtop=0 --fixth=136 --coverage` and `--rankoracle`. Dev only.*

Prompted by SSTT (`../sstt/`), whose headline is the **mirror image** of this
one: *"zero retrieval failures — every correct class is retrievable from 60,000
training images; all 237 remaining errors are ranking failures."* In SSTT
retrieval is perfect and ranking is the problem. Here ranking is acquitted
(world 3) and coverage is the problem. Same architecture family, opposite
bottleneck.

## Split 1 — false actuations are an INDEX-SELECTION failure

Pruning removed **only negatives**: `10500 -> 3840 (negtop 6660), iot 1155/1155
retained`. So the positives are bit-identical pre- and post-prune, and `wa` and
`missed` cannot be index-selection failures by construction. Re-running the rank
oracle against the unpruned 10500-vector index at the same threshold:

                   shipped (3840)     unpruned (10500)
    UNBIDDEN             6                   1
    wrong-act           13                  13
    missed              14                  14

**Five of the six false actuations exist only because the negative that would
have rejected them was pruned away.** Not "we need more negatives" — we had
them, and `--prune-negtop` discarded the useful ones.

That is a sharper statement of the earlier sweep result (fa monotone in negative
count) and it indicts the *selection criterion*: `negtop` keeps negatives with
the highest index-internal NN-coverage, and those turn out not to be the ones
that do rejection work on unseen queries. **Index-internal coverage is a poor
proxy for rejection utility.**

## Split 2 — for failing commands, the vocabulary is in the corpus

For each failing command, for each token of length >= 4, how many index
exemplars of the TRUE class contain it, and how many exemplars contain it at all:

    of 27 failing commands:
      discriminative token present in the true class : 15
      word exists in corpus but never for that class : 12
      absent from the corpus entirely                :  0

**Zero open-vocabulary failures.** Every failing command's vocabulary is
somewhere in the corpus. And the strongest single case:

    brighten the lamp next to the sofa      said=none  truth=iot_hue_lightup
      brighten(20/20)  lamp(2/14)  next(0/325)  sofa(0/0)

**`brighten` occurs 20 times in the corpus and all 20 are `iot_hue_lightup`** —
perfect class purity — every occurrence of the token falls in one class — and the
router rejects the query,
because Dice dilutes that one signal across "next to the sofa".

## Why this is not a new idea, and that matters

A class-conditioned word channel is exactly what SSTT's information gain is, and
the distinction from IDF is real: IG can vote `iot_coffee` for `brewing` even
when no candidate exemplar contains `brewing`. IDF cannot.

**But this project has tried that family three times.**

- signature prior — **inert**
- word prior (lift, text-derived) — 3 tie / 2 lose / 3 win by 1-2
- the gate/selector — **failed held-out and was cut** (test evaluation #4:
  recall 84.1% -> 82.7%, `wa` 15 -> 18)

And the signal is not in doubt. The word prior measures **89.6% vs the router's
85.9%** on dev IoT — it is genuinely better at *which* class. What failed every
time was the **combiner**: deciding when to trust which channel, using a rule
fitted on dev or on index cross-validation, which did not transfer.

So the finding is not "there is a semantic signal we have not exploited". It is:

> **The semantic signal exists and is measured. Three attempts to combine it
> with the router have failed, one of them on held-out data. The open problem is
> channel combination, not channel discovery.**

One thing genuinely differs in the current proposal: the trigger. Previous
attempts gated on the *prior's* confidence (`--selmargin`). Gating instead on
the *router's* candidate gap — invoke the second channel only when the coarse
scan reports a near-tie — is a deterministic trigger derived from the router's
own uncertainty rather than a fitted parameter. That is a different mechanism.
It is also the fourth attempt in a family with a 0-for-3 record, and it deserves
index-internal cross-validation and a pre-registration before it sees the test
set.

## The error space, fully decomposed

    false actuations (6)     5 index-selection (pruned the useful negative)
                             1 corpus defect ("make me happy juice")
    failing commands (27)   15 discriminative token IS in the true class
                            12 word exists but never for that class
                             0 open vocabulary

Nothing here is a representation failure. That was the question the session
opened with, and the answer is no.

---

# Boundary-witness negative selection: `fa` 6 -> 2..4, free

*`compare --prune-negbound=N`. Index-internal selection, no dev used to choose
anything. Dev used only to measure the result.*

The coverage audit showed five of six false actuations exist because the
negative that would have rejected them was pruned. So fix the criterion.

`--prune-negtop` ranks a negative by how many **index entries** have it as their
nearest neighbour. 89% of the index is negatives, so that count is dominated by
negative-to-negative structure: it rewards a negative for representing *negative
space*. What rejection needs is negatives on the boundary **around positive
space** — the ones a command-like non-command lands on.

`--prune-negbound` counts differently: for each **positive** exemplar, find its K
nearest negatives and credit those. Same budget, same leave-one-out discipline,
index-only.

## Result, identical budget (2685 negatives, 3840 vectors, 240 KB)

    criterion         fa   wa  missed   recall
    negtop             6   13    14     85.9%   <- shipped
    negbound K=1       3   13    14     85.9%
    negbound K=2       3   13    14     85.9%
    negbound K=4       4   13    14     85.9%
    negbound K=8       3   13    14     85.9%
    negbound K=16      2   13    14     85.9%
    (unpruned, 9345)   1   13    14     85.9%

**`wa`, `missed` and recall are identical at every setting.** Negatives only
touch rejection, exactly as the sweep predicted, so the change is purely in the
mode that matters. And the improvement is present at **every** K from 1 to 16 —
this is the mechanism, not a lucky parameter.

At 29% of the negatives it recovers most of the gap to the unpruned index:
`fa` 6 -> 2..4 against an unpruned floor of 1.

## Discipline note

K was fixed at 4 **before** any result was seen, and 4 is the *worst* row in the
sweep. It is reported as the headline for that reason. K=16 gives `fa=2`, but
choosing it now would be selecting a parameter on dev, which is the exact
failure mode of test evaluations #3 and #4. The robustness across K is the
evidence; the best cell is not.

Nothing else was tuned. The criterion uses only the index — no dev queries, no
held-out data, no labels beyond the ones already in the index.

## Cost

Zero at runtime. This is a build-time selection: same vector count, same blob
size, same scan, same firmware. `mkblob` would take `--prune-negbound=2685`
instead of `--prune-negtop=2685`.

## What it is not

Not validated. Dev `fa` counts are single digits and this project has twice
watched a dev improvement fail to transfer. Before shipping: pre-register the
criterion and K with falsifiers, then spend one budget unit. The mechanism has a
clean causal story and index-internal selection, which is a better position than
either prior attempt started from — but that was also true of the selector.
