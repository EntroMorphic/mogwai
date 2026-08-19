# SYNTHESIZE — the clean cut

## Decision 1 — Stop building. Get one external referent. (half day)

Two artefacts, produced by a human, not by me:

- **A command specification.** What does this device control? Exact tool names,
  arguments, types, ranges. One page.
- **~200 real utterances**, written the way you would actually speak to it, by
  someone who is not me and who has not seen the templates. Held out completely
  — never used for training, threshold selection, or corpus generation.

*Justification:* every number produced so far derives from a corpus I authored.
This is the only step that converts the project from self-referential to
measurable. Nothing downstream is trustworthy without it.

*Success criterion:* a held-out file exists that no artefact in this repo has
been fitted to.

## Decision 2 — Run the comparison that gates 610KB. (hours)

Head to head on that held-out set, one protocol, one threshold rule chosen on a
validation split that is not the test set:

| | flash | latency | routing on held-out |
|---|---|---|---|
| deterministic router v2 | ~30 KB | <1 ms | ? |
| tinyenc + flat scan | ~625 KB | ~24 ms | ? |
| hybrid (router first, encoder on low confidence) | ~655 KB | 1–24 ms | ? |

*Justification:* the neural path has never been fairly compared to the free one.
It costs 610KB, a tokenizer port, and all downstream complexity.

*Decision rule, fixed in advance:* if the encoder does not beat the router by
**≥15 points** on held-out routing, delete the encoder and ship deterministic.
Pre-committing the threshold prevents me from rationalising the result.

## Decision 3 — Defer the LCVDB port. Use a flat scan. (~100 lines)

Measured against our own device MAC rate (32.2 M MAC/s, config D):

| N | int8 bytes | scan |
|---|---|---|
| 200 | 12.5 KB | 0.40 ms |
| 1,000 | 62.5 KB | 1.99 ms |
| 5,000 | 312.5 KB | 9.94 ms |

*Justification:* LCVDB's LSH does not activate below 10,000 vectors and its
benchmarks start at 65,000. At our N a flat scan is ~100 lines against ~1,100
plus a 46-symbol scalar SIMD shim, and it is trivially correct.

*Revisit trigger:* N > 5,000 vectors, or scan latency > 20 ms.

## Decision 4 — Fix the two silent failures. (hours)

- **Trit collapse.** 92.6% of trits are zero because L2-normalised 64-d
  components (~0.125) sit inside the τ=3 dead zone. Rescale before encoding or
  calibrate τ on our own vectors. *Only needed if LCVDB is adopted later —
  but record it now so it is not rediscovered.*
- **Memory barrier.** The dual-core job struct is published with `volatile` and
  no barrier before `xSemaphoreGive`. Currently benign, timing-dependent.
  Pass the job by task-notification value or insert an explicit barrier.

## Decision 5 — Tokenizer only after Decision 2. (deferred)

If the encoder survives, port Needle's BPE (~50 lines + ~100 KB piece table,
`RefTokenizer` in `export.py` is the reference). If it does not, the tokenizer
is never needed — the deterministic router works on raw strings.

*This is the main thing LMM changed:* the tokenizer was the obvious next step
and it is now correctly last, because it is downstream of a decision we have
not made.

## Explicit non-goals

- Further optimisation. 23 ms has no requirement behind it. **Frozen until a
  latency budget exists.**
- Needle on-device. No Xtensa build exists and none can be made without Cactus.
  Retain Needle host-side as a corpus generator only.
- Hadamard MLP. Real (34x fewer FFN ops) but it is an optimisation, and
  optimisation is frozen.

## What is already bedrock and needs no revisiting

Board bring-up and verified ESP-AT backup. Blob-in-flash. The C encoder matching
host to cos 0.999973. int8/int32 precision (0.999978, tested across four widths
on the full corpus). The 2.84x and its two honest negative results. The
distillation itself — cos-to-teacher 0.9871 on a clean held-out split is the
one number in this project that was never fitted to its own test.

## Success criteria for the next cycle

1. A held-out utterance file exists that nothing has been fitted to.
2. Router and encoder have one number each, same protocol, same day.
3. A go/no-go on the encoder, decided by the pre-committed 15-point rule.
4. Zero new optimisation work.
