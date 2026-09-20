# Runtime Choice Production Plan

This is the P0 tracker for promoting the host-only runtime-choice research into
Mogwai's production path. It is deliberately separate from
`doc/RUNTIME_CHOICE.md`: that file records what the probes measured; this file
records what must be true before any of that work actuates hardware.

## Status

Current state: **host-only research, not production policy**.

Production path today remains the fixed-class ESP32 router: character 3/4-gram
hashing, integer centre thresholding, bit-packed nearest-neighbour scoring,
polarity handling, and thresholded `NONE` abstention.

The runtime-choice stack is regression-pinned but not shipped:

- semhash direct scoring
- factor path: `pol+color+comp+loc+support`
- Holdout A/B/C evaluator cases
- 21-bit prefix floor diagnostics
- topology and NSW probes

## Production Contract

The production runtime-choice contract is:

```text
Given a user utterance and a bounded candidate set, return one candidate or NONE.
Never actuate if support, polarity, referent, composition, color, or OOD gates fail.
The direct semhash+factor scorer is the decision authority.
Topology or NSW may only generate candidates; it may not become semantic authority.
```

This contract must be implemented before any runtime-choice result can actuate.

## Non-Negotiable Gates

- **Zero wrong actuation** on the frozen red team and every production-promotion
  holdout.
- **Exact host/device parity** for selected candidate, `NONE`, score/margin where
  exposed, and refusal reason.
- **Explicit attribution** for every failure: retrieval/support miss, ranking
  inversion, polarity/factor failure, OOD/knownness failure, collision dilution,
  graph retrieval miss, or correct abstention.
- **Bounded firmware behavior**: no floats, no unbounded allocation, no hidden
  Python-owned workflow, deterministic output, bounded candidate count, bounded
  runtime.
- **Flat direct path first**: no NSW/topology promotion until the flat production
  scorer exists and matches the host evaluator.

## Work Items

### P0.1 Define the Production API

Status: **complete as an API contract** in `c/src/runtime_choice.h` and
`c/src/runtime_choice.c`. The API validates inputs, exposes stable refusal reason
names, accepts duplicate candidates, pins candidate-order invariance for
non-scoring outcomes, and fails closed with `RTC_REASON_UNSUPPORTED_SCORER` until
the flat scorer is promoted behind it. Exact score ties, once scoring lands,
resolve to the lowest candidate index in the caller's original order.
`c/test/runtime_choice_api.c` pins this boundary.

The text API remains fail-closed until text-to-query-record encoding exists.
`r_choose_runtime_precomputed()` is the production entry point for callers that
already have a bounded query semcode/factor record; it delegates to the flat
direct scorer and preserves factor-refusal attribution.

Deliverable: a C API that accepts a query plus runtime candidates and returns a
candidate index or `NONE`.

Required shape:

```c
int r_choose_runtime(
    const router_t *r,
    const char *query,
    const runtime_candidate_t *cands,
    int n_cands,
    runtime_choice_t *out);
```

Acceptance gates:

- returns selected candidate index or `NONE`
- returns refusal reason
- handles empty, one-item, maximum-size, duplicate, and malformed candidate sets
- candidate order does not change the decision except for a documented exact-tie
  rule
- no actuation-side caller needs to inspect internal diagnostic state

### P0.2 Freeze the Runtime Candidate Format

Status: **complete for the candidate data contract**.
`runtime_candidate_t` now has bounded text, a 64-bit semantic code, semantic
score, named factor bits, explicit polarity, color, composition, location id,
and support state. Unknown factor bits or out-of-range factor values are
malformed input and fail closed.

Serialized runtime-choice candidate sets use a separate `RTC1` magic
(`RTC_CAND_MAGIC`), so they cannot be mistaken for `router.bin` (`RTR2`). The
wire format is little-endian and the parser is bounded and allocation-free: exact
EOF is required, candidate count is capped by `RUNTIME_CHOICE_MAX_CANDIDATES`,
every fixed-size record contains a NUL-terminated zero-padded text field,
reserved bytes must be zero, and parsed records pass the same candidate validator
as direct API calls.
`r_runtime_write_candidates()` is the deterministic C encoder for the same
format, and round-trip tests pin writer/parser equivalence.

Red-team iteration 2026-09-20: zero-count `RTC1` blobs are explicitly valid and
round-trip without dummy candidate/output arrays, matching the direct API's
empty-set abstention semantics. Nonzero blobs still require output storage, and
parser capacity smaller than the encoded count rejects before any record walk.

Red-team iteration 2026-09-20: factor flags and factor values must now agree
exactly. A value without its flag, or a flag with the neutral value, is malformed
input. This prevents production callers from hiding policy state in fields the
scorer might not be configured to honor.

Red-team iteration 2026-09-20: empty query/candidate text is malformed,
overlong query text now reports `RTC_REASON_MALFORMED_QUERY` instead of a
candidate failure, nonzero `n_cands` with `NULL` candidates rejects, and maximum
candidate count is pinned as accepted-but-unsupported until the scorer ships.

Deliverable: a compact candidate representation that can be supplied per request
or serialized into a runtime-choice blob.

The format must decide where these live:

- candidate text
- semhash/code bits
- polarity/operator compatibility
- composition support
- referent/location binding state
- color compatibility
- support/domain/knownness metadata

Acceptance gates:

- deterministic host encoder for the format
- exact parser bounds and malformed-input rejection
- explicit maximum candidate count and maximum candidate text length
- no persisted format ambiguity between fixed-class router blobs and runtime
  choice data

### P0.3 Port Semhash to the Production Constraints

Status: **started**. `r_runtime_code_score()` is the production-safe semantic-code
comparison primitive: integer-only, bounded to `1..64` bits, no allocation, no
candidate self-fit term, and tested at 1-bit, masked-prefix, half-mismatch, and
64-bit boundaries. `r_runtime_choose_code()` is the precomputed-code selector:
it validates candidates, ranks by code score only, ignores `sem_score`, and
resolves exact ties to the lowest original candidate index. Query-code production
and host/device parity are not done.

`r_runtime_make_query()` is the deterministic text-to-query-record bridge for
explicit factors. It derives bounded polarity, color, composition, location, and
support fields from text while requiring the caller to supply the semantic code;
learned code generation remains outside production until host/device parity is
designed.

Red-team iteration 2026-09-20: one-candidate code selection no longer exposes a
sentinel runner-up; `second == score` and `margin == 0` when there is no
runner-up.

Deliverable: integer-only semhash scoring that can run in the firmware build or
be proven equivalent to a precomputed candidate-code path.

Acceptance gates:

- no float on the hot path
- no dynamic allocation on the hot path
- host/device parity test for every frozen runtime-choice case
- code-width policy documented; do not confuse prefix width with intrinsic
  semantic floor
- candidate self-fit weight either removed, retained, or replaced by an explicit
  measured production rule

### P0.4 Implement the Factor Path as Auditable Runtime Code

Status: **started**. `r_runtime_factor_score()` validates explicit query and
candidate factor records, returns an independent refusal reason for support,
polarity, color, composition, or location, and scores only matched factors. It is
not yet wired into `r_choose_runtime` or combined with code scoring.

Red-team iteration 2026-09-20: factor scoring now pins two boundary semantics:
a query with no required factors accepts with score `0`, and a query whose own
support state is OOD/unsupported refuses with `RTC_FACTOR_REASON_SUPPORT`.

Deliverable: production implementations of the five load-bearing factors.

Factors:

- `pol`: polarity/operator inversion compatibility
- `color`: crisp color compatibility
- `comp`: compositional activation/lighting support
- `loc`: runtime referent binding and conflicts
- `support`: learned support, knownness, and OOD abstention

Acceptance gates:

- each factor has an independent refusal reason
- factor failures cannot be hidden under generic `NONE`
- every factor is load-bearing in at least one pinned production-promotion case
- all five factors enabled reproduces the host floor result; any ablation failure
  is attributed to the factor that caused it

### P0.5 Promote the Flat Direct Scorer Before Topology

Status: **started**. `r_runtime_choose_flat()` combines the production semantic
code score with `r_runtime_factor_score()` over explicit query/candidate records.
It skips factor-rejected candidates, surfaces the first factor refusal when all
candidates reject, ignores `sem_score`, preserves lowest-index tie resolution,
and is wired through `r_choose_runtime_precomputed()`. Text-based
`r_choose_runtime` remains fail-closed until query encoding exists.

Deliverable: a flat candidate scan using the production semhash+factor scorer.

Acceptance gates:

- flat production scorer equals host evaluator on frozen probe plus Holdouts A/B/C
- zero wrong actuation
- exact `NONE` preservation
- exact candidate-order invariance test
- measured candidate-count ceiling for firmware latency and memory

### P0.6 Expand the Production-Promotion Eval Set

Deliverable: a larger held-out runtime-choice promotion suite, separate from the
already-frozen research holdouts.

Required coverage:

- OOD near misses
- polarity inversions and prevention verbs
- referent ambiguity and explicit referent conflicts
- unseen aliases and paraphrases
- candidate-set distractors and near-collisions
- candidate order permutations
- empty and malformed candidate sets
- unsupported objects, unsupported locations, and metaphorical OOD commands

Acceptance gates:

- each case has expected decision and expected attribution
- additions are made before remediation when testing generalization
- failures are recorded as failures first, not patched into the same evidence
  pass

### P0.7 Add Firmware and Blob Regression Pins

Deliverable: regression checks equivalent in seriousness to the current blob and
router guardrails.

Required tests:

- runtime-choice data parser rejects truncation, trailing bytes, bad counts, bad
  offsets, and invalid enum/reason values
- host/device parity for candidate, `NONE`, score/margin if exposed, and refusal
  reason
- malformed candidate lists fail closed
- wrong-actuation count is zero on every red team
- memory and runtime remain below the documented firmware budget

### P0.8 Decide the Runtime Memory and Update Model

Deliverable: a production decision on where runtime meanings live.

Options:

- supplied per request and never persisted
- serialized into a separate runtime-choice blob
- rebuilt into `router.bin`
- stored in NVS/flash
- ephemeral RAM graph/cache

Acceptance gates:

- update model has a failure policy
- persisted data has versioning and parser validation
- ephemeral data has deterministic reset behavior
- firmware memory budget includes worst-case candidate storage

### P0.9 Revisit NSW Only as Candidate Generation

Deliverable: optional graph experiment after P0.1-P0.8 are satisfied.

Allowed role:

```text
NSW retrieves semantically nearby candidate meanings.
The direct semhash+factor scorer still decides among retrieved candidates.
```

Promotion gate:

```text
NSW top-K candidate generation + direct scorer == flat direct scorer
```

Required metrics:

- flat-winner recall at K
- correct-candidate recall at K
- graph retrieval miss count
- `NONE` preservation
- wrong-actuation delta
- candidates scored
- memory per meaning
- insertion/deletion behavior
- determinism across insertion orders

## First Implementation Order

1. Define the API and candidate format.
2. Build the flat host production scorer behind that API.
3. Add attribution categories and candidate-order tests.
4. Port or serialize semhash/factor data under firmware constraints.
5. Add blob/parser and host/device parity tests.
6. Expand the production-promotion eval set.
7. Measure candidate-count ceiling.
8. Only then prototype NSW as an optional candidate generator.

## Links

- Runtime-choice measurements: `doc/RUNTIME_CHOICE.md`
- Blob format guardrails: `doc/BLOB_FORMAT.md`
- Method guardrails: `doc/METHOD.md`
- Tool inventory: `doc/TOOLS.md`
