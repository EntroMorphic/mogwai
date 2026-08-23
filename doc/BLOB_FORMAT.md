# `router.bin` — the host↔device contract

`mkblob` writes it, the firmware maps it from flash and reads it in place. Every
field is little-endian, and the layout is fixed at compile time by `RD`.

**The host and the device must be built with the same `RD` and the same
`prune`/`threshold` settings**, or parity is measuring nothing. This is not
hypothetical: a pruned blob once shipped with the wrong threshold and `PARITY
EXACT` still passed, because the host reference queries were computed with the
same wrong value. *Parity proves host == device. It does not prove either is
correct.*

## Layout (v2, `RTR2`)

Sections run in **descending alignment order** so every one lands aligned for
any `n_index`. The v1 order put `label` and `act` before the vectors, which made
the vector offset `1556 + 3n` — 4-aligned only when `n` is a multiple of four.
It happened to be (n=3840). On Xtensa an unaligned `uint32` load faults, so that
was a latent crash waiting for an index size we had not tried.

| offset | type | count | meaning |
|---|---|---|---|
| 0 | `uint32` | 5 | header: `RMAGIC2`, `RD`, `n_index`, `n_class`, `threshold` |
| 20 | `char` | `RMAXCLS × RNAMELEN` | class names, NUL-padded (16 × 32 = 512 B) |
| 532 | `int32` | `RD` | per-dimension centre, fixed-point ×`RSCALE` |
| 1556 | `uint32` | `n_index × RWORDS` | **the active masks**, 32 B each, 4-aligned |
| … | `uint16` | `n_index + 1` | exception offsets, ascending |
| … | `uint16` | `n_index` | **precomputed** `t_active()` per entry |
| … | `uint8` | `n_index` | class label per index entry |
| … | `uint8` | `eoff[n_index]` | sign-exception dimension positions |
| … | `uint32` | 1 | `nref` — number of reference queries that follow (read with `memcpy`, so it needs no alignment) |
| … | record | `nref` | `uint8 len`, `len` bytes of text, `int32 score`, `int8 class` |

Constants live in `c/src/router.h`: `RMAGIC2=0x52545232` ('RTR2'), `RD=256`,
`RWORDS=RD/32=8`, `RMASKB=32`, `RMAXCLS=16`, `RNAMELEN=32`, `RSCALE=16384`,
`RSHIP_TH=136`, `REXMAX=65535`. A regress check derives these from the header
rather than trusting this paragraph.

## Why the sign plane is an exception stream

v1 stored two bit-planes per vector: `m[]` mask and `s[]` sign, 64 B. Measured
over the shipped index, 78.51% of dims are `0`, 21.34% are `+1`, and **0.16%
are `-1`** — 1,539 of 983,040. The sign plane was 122,880 bytes carrying about
1.6 KB of information.

So v2 makes `+1` the implicit case and stores only the exceptions. With
`s = m & ~E`, inside `both` we have `q.s = ~Eq` and `b.s = ~Eb`, so `diff` there
is `Eq ^ Eb`:

    disagree = |Eq & bm| + |Eb & qm| - 2*|Eq & Eb|

An identity for every input, so **routing is bit-identical to v1** — which is
what `PARITY EXACT` proves on the device, since the embedded reference scores
are computed host-side from the v1 bit-planes.

The `-2*|Eq & Eb|` term is not an optimisation. Omitting it double-counts a
dimension that is below centre on *both* sides, where the signs actually agree.
It fires on 54,683 of 5,863,680 dev pairs.

**The sign plane is load-bearing and must be preserved exactly, not dropped.**
`--condcentre` conditioned the centre so the signs would split evenly and lost
twenty points (65.6% vs 85.9%): with a near-constant sign plane, `disagree`
fires only when a dim sits below its diluted mean, which happens when it fires
once inside a *long* utterance. That is a length signal, and long utterances are
the negatives — which is why 83% of the exceptions are in `none`.

`REXMAX` bounds the stream because the offset table is `uint16`. `mkblob` aborts
above it rather than silently wrapping.

## Why `act[]` is stored

`t_active(b)` is fixed at build time but was being recomputed for every index
entry on every query — 8 redundant popcounts per comparison, **569 of 2335
cycles per vector (24%)** on the ESP32. Precomputing it into the blob and using
the pre-scored path cut device latency 102.1 → 78.8 ms with parity preserved.

## Why the reference queries are embedded

The last section is 64 dev queries with the score and class **the host computed
for them**. The firmware re-routes each one and compares. This is the only thing
standing between "the device runs" and "the device runs the same arithmetic the
harness measured" — integer overflow, endianness, struct padding, and a stale
blob all surface here as a parity failure rather than as a quiet accuracy loss.

`PARITY EXACT` is printed only when class **and** bit-exact score match on all
64. Anything less prints `PARITY FAILED`.

## What parity does not cover

- A prune that drops the **wrong** vectors — host and device share `prune.c`, so
  both would be wrong identically. The host curve runs catch this, not parity.
- A wrong **threshold** — see above; it is baked into both sides.
- Anything about **accuracy**. Parity is an arithmetic-identity check.
