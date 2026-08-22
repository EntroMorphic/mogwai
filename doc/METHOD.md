# Guardrails — why the numbers in this repo can be trusted, and where they can't

This project has produced wrong results and caught them. The mechanisms below
exist because something specific went wrong; each one names its incident.

## 1. Assertions that abort, not warn

`c/src/invariants.c` aborts the run (exit 2) rather than printing a warning.

**Incident.** The dev split was **75.6% leaked**: dev was carved out of train but
never added to the exclusion set, so NLU-Evaluation-Data (which MASSIVE derives
from) reintroduced it. Four design decisions were made on that split and had to
be voided. The assertions later caught two further leaks nobody suspected —
MASSIVE's train repeats utterances, and train and test share utterances.

Now enforced: `inv_disjoint` (index vs dev, index vs test), `inv_superset`,
`inv_similar_rate`, `inv_bounded`. Test is loaded **first** so the index can
exclude it.

## 2. Exact-string disjointness is not sufficient

Two different strings can encode to the **same** twin-ternary code, which makes
them indistinguishable to the router — leaked, with no assertion firing.
`code_overlap()` measures it directly. Currently 0 of 1527 dev and 0 of 2974
test. Clean, but that was luck, and now it is checked.

## 3. Test budget

`make testset` (never `make test` — that is refused as a habit-typo) increments
`results/TEST_BUDGET_COUNT` and appends timestamp + full argv to
`results/TEST_BUDGET`. The threshold is tuned on **dev** and merely applied to
test; `report()` tunes on `V_*` and tallies on `T_*`.

**Incident.** Evaluation #1 is marked VOID — it measured a config chosen entirely
on the leaked split. Budget was not reset: it was spent.

**Incident.** The guard itself was broken. Counter and prose lived in one file,
so `fscanf("%d")` read 0, incremented to 1, and truncated — destroying the audit
note and resetting the count *every use*. A guard that cannot count past one is
not a guard. Counter and log are now separate files.

## 3b. The run log was silently corrupt for the whole project

`results/RESULTS.tsv` exists so that tracking is structural rather than a
discipline anyone has to remember. It was written by scraping the formatted
console output with `awk` on whitespace fields.

**Incident.** `binary (1 bit)` is **three** whitespace fields. Every binary row
therefore logged its variant as `binary (1`, shifted each later column by one,
and dropped `index_kb` entirely. The schema also changed silently mid-project
when error bars were added, so early and late rows do not mean the same thing.
Nothing ever read the file closely enough to notice.

**Fixed structurally:** `compare` now emits its own `ROW\t...` line with the
`fa`/`wa` split included, and the Makefile only stamps and appends it. Nothing
parses formatted output. The corrupt log is kept as `RESULTS.v1-corrupt.tsv` —
deleting the evidence of a tracking failure would be the wrong lesson.

**Also added `make ship`**, because nothing in the Makefile reproduced the
README table: `make compare` auto-tunes and lands on 136/138, not the shipped
126. The two now differ by design and are labelled as such.

## 4. Curve dominance, not "breaks zero"

A change is not accepted because it fixes some cases and breaks none. It must
move the **(wrong, missed) frontier** at matched operating points.

**Incident.** Naive auto-tuned rows suggested index pruning *improved* false
actuations (14 → 11). It does not — each run retunes the threshold, so those
rows sit at different operating points. At matched points the unpruned baseline
strictly dominates. The rule overturned the single-point reading.

## 5. Size-matched controls

**Incident.** The headline table compared twin-ternary d=256 (656 KB) against
binary d=256 (328 KB) — twin got twice the bytes, for a claim that is *about*
bits per dimension. The missing control (binary d=512, same 64 B/vector) was run
only when red-teaming the test evaluation. It confirmed the claim, but it should
have been there from the start.

## 6. Pre-registration

Predictions for test evaluation #2 were committed **before** the run. Scored
afterwards: **3 of 4**, not 4 — the false-actuation prediction missed by 22%, in
the flattering direction, and was initially reported as a hit.

## 7. Two-point fits are not fits

**Incident, twice.** A dual-core cost model fitted on two points implied a 3.4 ms
fixed term; the third point refuted it. Then, one section after writing that
down, the byte/vector cost model was fitted on two dimensions and overstated the
intercept by 25%; the third dimension corrected it to 59.8 ns/byte + 326 ns/vector.

## 8. Config sweeps must not trust the build cache

`idf.py` caches `-D` variables. "Restoring" a config by rebuilding without the
override silently reuses the previous iteration's `SDKCONFIG_DEFAULTS` — this
produced a bogus reading and a `PARITY FAILED` that looked like a real
regression. Pass overrides explicitly every time, or `rm -rf build`.

## Where the numbers still can't be trusted

- **The held-out figure (84.1%) is at threshold 136, not the shipped 126.**
- The test set is **not pristine** — evaluation #1 observed it before being voided.
- `recall` is blind to false actuations; read the `fa` column.
- The corpus ceiling is **~98%, not 100%** — some labels are wrong ("turn out the
  lights" is labelled `lightup`) and some are unknowable from text (which physical
  device a "kitchen light" is depends on the installation).

## 9. Dev is not an easier split than test — measured, not assumed

`c/bin/leakchk data/train.json data/validation.json data/test.json` compares how
close each split sits to the index by word-overlap Dice:

    DEV    n=193   >=95%: 7    80-95%: 63   near-duplicate rate 36.3%
    TEST   n=220   >=95%: 12   80-95%: 70   near-duplicate rate 37.3%

**36% and 37% — the same.** So tuning on dev and applying to test is not
flattering itself, which is the independent explanation for why the dev-selected
config generalised (85.9 → 84.1, inside the error bars).

It also sets a caveat: **both splits are ~37% near-duplicates of index entries**,
because MASSIVE contains many closely-related utterances. Absolute accuracy is
therefore easier here than on genuinely novel phrasing. Relative comparisons
between representations are unaffected — both see the same corpus — but do not
read 88% as "88% on whatever a user says".

`c/bin/leaktest data/train.json data/nlu_home.csv` still reproduces the original
75.6% leak on demand. It is kept as a **regression test for a fixed bug**: it
demonstrates the old behaviour deliberately.

## 10. My own verification harnesses were wrong four times

Worth its own entry, because it nearly created false findings:

- `cmd | grep x | head -2 && echo FAIL` — `head` exits 0 with no input, so the
  failure branch always fired. Reported a working build command as broken.
- `cmd | grep x; rc=$?` — captures **grep's** status, not the command's.
- `echo "$(basename $b): rc=$?"` — the command substitution runs first and
  **resets `$?`**. Reported rc=0 for tools that correctly returned 2.
- `cmd | head -1` on tools whose output starts with a blank line — concluded
  three working diagnostics produced nothing.

Each looked like a defect in the thing being tested. The rule: when a check says
something is broken, **verify the check before fixing the target.**

## 11. Curve dominance is necessary but NOT sufficient — add significance

**Incident.** The selector dominated the dev operating curve at every point and
was accepted on that basis. Paired McNemar afterwards: **fixed 6, broke 3,
p = 0.508.** It then failed on held-out data, making `wa` worse.

A noise-level change can dominate a curve by helping marginally at every
threshold. The frontier test catches changes that trade one error for another;
it does not catch changes that are simply too small to be real.

**Required from now on:** a change must move the (wrong, missed) frontier AND
survive a paired significance test on the items that actually changed. Use
`--selsig` as the pattern: count discordant pairs, exact two-sided binomial.

Applied retroactively, twin-ternary vs binary is **p = 0.0931 on dev** — not
significant at n=192. The claim rests on the held-out gap (19 net items on 220)
and on binary's saturation at d=256 vs d=512, not on dev significance.

## 12. Index cross-validation does not predict held-out here

`--xval` (2-fold inside the index, rebuilding centre, vectors and prior per
fold) gave the selector +48 net of 183 disagreement items. Held-out, it was −3.

The obvious explanation — near-duplicate memorisation across folds, given the
36–37% near-duplicate rate — was tested and **refuted**: the prior's advantage
is in items *without* near-duplicates (+12) and absent in items with them (−1).
No confirmed explanation exists.

**Treat index CV as a filter for obviously-bad ideas, never as evidence a change
works.** It was the strongest pre-test evidence available for the selector and
it pointed the wrong way.

## 13. A test that cannot fail is decoration — mutation-test the suite

**Incident.** After adding 10 developer-experience checks, I mutation-tested
them: broke each behaviour on purpose and asked whether the suite noticed.
**Two of ten did not fire.**

- The `--route` checks asserted that a class name appeared *somewhere* in the
  output. Breaking the decision line still passed, because the class also
  appears in the neighbour list below it. Fixed by anchoring on the decision
  line itself (`^ +decision +iot_hue_lightoff$`).
- The rejection-reason checks were substring matches, so corrupting the message
  by appending still passed. Fixed with `grep -Fxc` against the exact line.

Both looked like passing tests. Neither tested what its name claimed.

**Required:** when adding a check, break the thing it guards and confirm the
check fails. `cp` the source, mutate, run, restore. It takes a minute and it is
the only evidence a check works.

## 14. Documentation that quotes a number will go stale

`README.md` and `doc/QUICKSTART.md` both advertised "29 checks". The suite had grown
to 41 and nothing noticed, because no check compared the documented number to
the real one.

`scripts/regress.sh` now ends by parsing the count out of the docs and comparing
it to its own live total. It failed on its first run — adding the check changed
the count it was checking — which is the correct behaviour and the reason to
have it.

Generalise: any number a doc quotes about the repo itself should be verified by
the repo itself.

## 15. "Detector reported clean" is not "clean" — controls, not just assertions

Mutation-testing all 43 checks found two that could never fail, and they shared
a shape: both asserted that a **detector reported nothing wrong**.

- `code_overlap` reports how many evaluation items share an encoded code with
  the index. The check asserted that count was 0 — but a detector rigged to
  always return 0 also passes. Clean and broken are indistinguishable.
- `blobfmt` validates the blob layout. The check asserted it printed `OK` — but
  a validator with its size check removed still prints `OK`.

**A check on a detector must prove the detector can fire.**

- `code_overlap` now runs a **positive control** first: an index utterance must
  collide with its own stored code, or it aborts. Any "0 overlaps" it reports is
  then meaningful.
- The suite runs a **negative control** on `blobfmt`: a deliberately truncated
  blob must be REJECTED (rc=1).

**And the control must exercise the same code.** The first version of the
positive control had its own copy of the `memcmp`, so disabling the real one
left the control green. It was validating a parallel implementation. Fixed by
factoring a single `co_collides()` that both the control and the reporting loop
call. **A control that does not share the code path is testing a copy.**

## 16. Mutation testing with `git checkout` destroys uncommitted work

**Incident.** After fixing six stale documents, I mutation-tested the new checks
that guard them. The restore step was `git checkout <file>` — which restores from
HEAD, not from a snapshot. Every fix was still uncommitted, so the restores
**reverted all six**. `git add -A` then committed the reverted state and I pushed
it: four failing checks, live.

The checks themselves were fine — they fired on all four mutations, exactly as
intended. The harness around them was not.

**Required:** commit before mutation testing, or snapshot with `cp` and restore
from the copy. Never use `git checkout` as an undo for work that is not yet in
the index.

**And re-run the suite between the last mutation and the commit.** I ran it, saw
`50 passed, 4 failed`, and committed anyway — the output was on screen and I did
not read it. A green suite is only evidence if you look at it.

## 17. Mutation-test the whole suite, and check the harness too

Three of four checks written to guard a detector could not detect anything
(#15). At that rate, spot-checking is not defensible — so `scripts/mutate.sh`
mutation-tests **every** check: 46 mutations, each breaking one guarded thing,
reporting which checks fire and, crucially, **which checks no mutation can make
fail**.

Coverage: **53 of 55**. The two uncovered are honest and named as such — see
below.

### What the first complete pass found

- **The leak guard was unguarded.** Disabling `inv_disjoint`'s abort fired
  *nothing*. Both checks grep for the SUCCESS line, which a disabled guard still
  prints. The mechanism that exists because of the 75.6% leak could have been
  silently dead. Fixed with an end-to-end control: `--leak` reintroduces the
  leak deliberately and the run must abort with rc=2.
- **`git ls-files` reads the index, not the working tree.** Deleting `LICENSE`
  left its check passing. Four checks affected; all now assert existence too.
- **A working check was silently disconnected by a later edit.** `blob
  reproducible byte-identical` reads `$?` from a `cmp`; inserting a new check
  between them meant `$?` held the *new* check's status. It had been passing
  unconditionally. Nothing about reading the file revealed this.

### The harness needed the same treatment

The first two passes produced numbers that were wrong in the *flattering*
direction:

- **BSD `sed` alternation** (`\(a\|b\)`) does not substitute and does not error,
  so the coverage comparison matched nothing and reported "covered 0 of 55".
- **Git-index mutations leaked between runs**, because the restore only handled
  the working tree. Unrelated checks appeared to fire — inflating coverage,
  which is exactly the wrong way for this to fail. Fixed with `git reset` per
  mutation; per-mutation fire counts fell from 4-6 to 1-3.
- **Two mutations were inert**: one hardcoded a check count that had since
  changed; one moved `archive/.git`, which makes the check SKIP rather than
  fail, proving nothing.
- **One restore was mangled** by an earlier substitution eating `$ARGV[0]`, so a
  run left the archived bundle corrupt and a genuinely failing checksum behind.

### The two checks nothing can make fail

`invariant RAN and reported on DEV` / `... on TEST` verify the invariant
executed and reported, not that it can detect. That is unfixable in their
current form — a disabled guard prints the same line. They are **named for what
they do** rather than left implying detection, and the real detection test sits
beside them.

**Rule:** a check is not trusted until a mutation has made it fail, and a
mutation harness is not trusted until its own restores have been verified.
