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
