# The archive — local-only, deliberately

`archive/` is in `.gitignore`. It is **not** in this repository, and a fresh
clone will not have it. That is on purpose: it holds ~880 MB of superseded work,
a full second git repository, and a Python virtualenv. None of it belongs in the
history of a zero-float C router.

Nothing in it was deleted. This file records what exists so the loss is legible
if the disk holding it goes away.

## `archive/needle_upstream/` — where this project started

A clone of **github.com/anjaustin/needle** (the Cactus Needle 2 Python package),
270 commits, moved here whole: working tree, original `.git` with its remote,
and its `.venv`.

This project began as an audit of that package. The research it prompted —
"does controlling nine IoT intents actually need a 45M-parameter model?" — became
an architecture with **no code in common** with it. The archive is provenance,
not a dependency.

**Three commits in it were never pushed.** `origin` only ever carried `main`:

    3b8cefd  feat: pack ternary weights in base-3, 1.6 bits/weight instead of 2.0
    990978b  feat: add `needle build --kv-window`
    281baec  fix: repair `needle run`, Python 3.9 import, and the JAX dependency split

So `needle_full.bundle` (+ `.sha256`) sits alongside it — every ref, verified by
cloning from it and confirming all three commits are recoverable. Restore with:

    git clone archive/needle_upstream/needle_full.bundle <dest>

## `archive/python_era/` — the encoder that lost

The distilled MiniLM sentence encoder (0.62M params, JAX/torch pipeline) that
preceded the router, plus the ESP32 firmware that ran it and the corpora it was
trained on. Superseded because the router beat it while needing no training, no
float, and no learned parameters. See its own README for the two failures there
that produced the guardrails in [METHOD.md](METHOD.md).

## `archive/superseded_tools/` — `probe.c`

Compared `t_score` against a candidate `dice()`; Dice was then adopted **as**
`t_score`, so it compares a function to itself.

## Why gitignored rather than tracked

Tracking it cost 45 MB of tracked files and most of a 122 MB `.git`, for content
that is read perhaps once. The rule remains **never delete, only archive** — this
changes where the archive lives, not whether it exists.
