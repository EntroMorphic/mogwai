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

Status: **scaffolded** in `c/src/runtime_choice.h` and
`c/src/runtime_choice.c`. The API validates inputs and fails closed with
`RTC_REASON_UNSUPPORTED_SCORER` until the flat scorer is promoted behind it.
`c/test/runtime_choice_api.c` pins this boundary.

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

Status: **started**. `runtime_candidate_t` now has bounded text, a 64-bit
semantic code, a semantic score, and named factor bits. Unknown factor bits are
malformed input and fail closed.

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
