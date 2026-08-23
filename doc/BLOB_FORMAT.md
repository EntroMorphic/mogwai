# `router.bin` — the host↔device contract

`mkblob` writes it, the firmware maps it from flash and reads it in place. Every
field is little-endian, and the layout is fixed at compile time by `RD`.

**The host and the device must be built with the same `RD` and the same
`prune`/`threshold` settings**, or parity is measuring nothing. This is not
hypothetical: a pruned blob once shipped with the wrong threshold and `PARITY
EXACT` still passed, because the host reference queries were computed with the
same wrong value. *Parity proves host == device. It does not prove either is
correct.*

## Layout

| offset | type | count | meaning |
|---|---|---|---|
| 0 | `uint32` | 5 | header: `RMAGIC`, `RD`, `n_index`, `n_class`, `threshold` |
| 20 | `char` | `RMAXCLS × RNAMELEN` | class names, NUL-padded (16 × 32 = 512 B) |
| … | `int32` | `RD` | per-dimension centre, fixed-point ×`RSCALE` |
| … | `uint8` | `n_index` | class label per index entry |
| … | `uint16` | `n_index` | **precomputed** `t_active()` per entry |
| … | `tvec` | `n_index` | the index: `m[RWORDS]` then `s[RWORDS]`, 64 B at d=256 |
| … | `uint32` | 1 | `nref` — number of reference queries that follow |
| … | record | `nref` | `uint8 len`, `len` bytes of text, `int32 score`, `int8 class` |

Constants live in `c/src/router.h`: `RMAGIC=0x52545231` ('RTR1'), `RD=256`,
`RWORDS=RD/32=8` words **per bit-plane** (a `tvec` holds two, so 16 words = 64
bytes), `RMAXCLS=16`, `RNAMELEN=32`, `RSCALE=16384`, `RSHIP_TH=136`.

## Why `act[]` is stored

`t_active(b)` is fixed at build time but was being recomputed for every index
entry on every query — 8 redundant popcounts per comparison, **569 of 2335
cycles per vector (24%)** on the ESP32. Precomputing it into the blob and using
`t_score_pre()` cut device latency 102.1 → 78.8 ms with parity preserved.

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
