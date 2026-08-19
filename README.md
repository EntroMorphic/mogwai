# tinyenc — a 2-layer d=64 sentence encoder for ESP32

Distils `all-MiniLM-L6-v2` into a 0.62M-parameter contextual encoder that
produces 64-d vectors, sized for LCVDB on an ESP32 with 4 MB flash.

Unlike a static embedding table, this has **real attention** — two layers with
learned positional embeddings — so tokens interact. "not cold" is not the sum
of "not" and "cold".

## Result

| | routing (16 held-out queries) | size |
|---|---|---|
| MiniLM-L6 teacher, 384-d | 12/16 | 22.3 MB |
| **tinyenc student, 64-d int8** | **13/16** | **610 KB** |
| static LM embedding table (baseline) | 3/8 | — |

Held-out cosine to teacher: **0.9871**. int8 quantisation costs nothing
(13/16 at both f32 and int8).

Remaining failures are deep metaphor: "this room is like an oven",
"my hands are numb", "i cannot read my book in here".

## Pipeline

    corpus.py   -> corpus.jsonl     2,907 in-domain utterances (templates + carriers)
    teacher.py  -> teacher.npy      MiniLM embeddings        [system python: torch]
    train.py    -> student.npz      distillation             [venv: jax]  11 s
    eval2.py                        routing evaluation
    export.py   -> tinyenc.bin      int8 blob + C header     610 KB

Two interpreters on purpose: the teacher needs torch (system python 3.14),
the student trains in JAX (needle/.venv, python 3.11). Nothing is installed
into either that wasn't already there.

## Training

Loss is cosine alignment to the PCA'd teacher **plus** pairwise-geometry
matching within each batch — retrieval depends on relative geometry, not
absolute direction. Teacher 384-d is PCA'd to 64-d offline (keeps 86.3% of
variance); that map is applied to the teacher only, so it costs nothing at
runtime.

Tokeniser is Needle's 8192-entry sentencepiece BPE (already on disk).

## On-device notes

- 0.62M params, seq cap 24, median utterance is 9 tokens.
- Estimated ~20 ms/utterance on ESP32 LX6 scalar at 240 MHz.
- Output is 64-d L2-normalised float32 — feed to `lcvdbt_pack_f32_mtf7()`.
  64 is a multiple of 64, so it is a valid LCVDB dim.
- **Vectors are dense and near-zero-mean**, so unlike hashed n-grams they do
  not need a random projection before the trit encoder. Verify trit occupancy
  on your corpus anyway and calibrate the threshold offline — auto-calibration
  lives in lcvdb's `http_server.c`, which an embedded build drops.

## Retrain

    python3 corpus.py corpus.jsonl                # edit templates for your domain
    python3 teacher.py corpus.jsonl teacher.npy   # system python
    .../needle/.venv/bin/python train.py
    .../needle/.venv/bin/python eval2.py
    .../needle/.venv/bin/python export.py
