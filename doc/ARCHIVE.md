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

## `archive/superseded_blobs/` — the first blob this project made

`router-d512-1805441.bin`, 830 KB, `dim=512`, from the original C rewrite. It
was TRACKED and referenced by nothing; `blobfmt` rejects it against the current
format. Untracked and moved here — see that directory's README.

## The one deliberate exception

A copy of the upstream bundle is tracked at `provenance/needle-upstream.bundle`.
Everything under `archive/` itself stays local-only.

It could not simply be force-added where it sat: `archive/needle_upstream/`
contains a nested `.git`, and git refuses to track anything inside a nested
repository whatever `-f` you pass. It also should not have lived there — a
backup inside the repository it backs up is not a backup.

The reason: three commits in that history were never pushed anywhere, and the
only two copies — the original `.git` and this bundle — sat in the same folder
on the same disk. Co-located redundancy is not redundancy. Tracking the bundle
puts those commits on a remote, which is the only backup that survives losing
this machine.

Restore with:

    git clone archive/needle_upstream/needle_full.bundle <dest>

## Why gitignored rather than tracked

Tracking it cost 45 MB of tracked files and most of a 122 MB `.git`, for content
that is read perhaps once. The rule remains **never delete, only archive** — this
changes where the archive lives, not whether it exists.

## `archive/stray/`

Material moved out of the tracked tree during housekeeping, kept because nothing
is ever deleted.

- **`data_hold_duplicate/`** — a 7.7 MB exact duplicate of the corpora, created
  by a botched restore while testing the missing-corpora error path: `data/` was
  moved aside, `make fetch` recreated it, and moving the original back landed it
  *inside* the new one.
- **`esp_at_wroom32_v1.1.2_full_4MB.bin`** — the board's original ESP-AT image.
  A public Espressif release, so re-obtainable upstream; `board_backup/RESTORE.md`
  keeps the sha256 that verifies any copy.

## `archive/encoder_path/` and `archive/structural_ranker/`

Earlier architectures, each with its own autopsy README: the distillation path
that preceded the router, and a structural ranker that was built and cut. Both
predate the C rewrite.
