# Archive — the encoder path (retained for reference, not deleted)

Superseded as a *deployment* choice by the head-to-head in `compare2.py`:
on an independently-authored, contamination-verified test set with both systems
indexing the same corpus, the char n-gram router scored 12/22 (55%, 0 KB) and
the distilled encoder 7/22 (32%, 610 KB) — a −23 point delta against a
pre-committed +15 rule.

**That verdict is provisional.** It was measured on a synthetic corpus I wrote.
Real data (MASSIVE) contains implicit intent ("time to sleep" → lights off) and
ASR noise ("make lights brightener") that lexical matching cannot handle, and
where a semantic encoder should have the advantage. Re-run before trusting it.

## What is here and still valid

- `train.py` / `teacher.py` — the distillation pipeline. 11 s on an M4,
  cos-to-teacher 0.9871 on a clean held-out split. This number was never
  fitted to its own test and remains the soundest result in the project.
- `export.py` / `tinyenc.bin` — int8 blob + C header, 610 KB, validated
  on hardware at cos(dev,host) 0.999973.
- `train_hada.py` — Hadamard-MLP variant, 34x fewer FFN ops, 12/16 vs 13/16.
- `intsim.py` — int8/int16/int6/int4 activation precision sweep.

The ESP-IDF firmware in `../esp32/` still builds and runs this encoder.
Nothing is broken; it is simply not the current deployment candidate.
