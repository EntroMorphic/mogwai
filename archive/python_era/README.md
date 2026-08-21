# The Python era — archived 2026-08-21, deleted never

Everything here predates the standing "No Python. Period." rule and the rewrite
of the system in C. It is kept because it is the record of how the current
design was arrived at, including the parts that were wrong.

## What this was

An attempt to **distil a sentence encoder small enough for an ESP32** —
`all-MiniLM-L6-v2` compressed into a 0.62M-parameter 2-layer d=64 contextual
encoder, trained in JAX, exported as an int8 blob. The old `README.md` describing
it is preserved in the repo history.

`esp32_encoder_firmware/` is the ESP-IDF project that ran that encoder on device.

## Why it was superseded

The encoder was measured against a hashed-n-gram **router** on the same task.
The router won, and it needs no training, no float, and no learned parameters
at all. The encoder path was archived rather than deleted because the comparison
is the finding.

## Why it is not merely obsolete but instructive

Two failures happened here that shaped every guardrail now in `doc/METHOD.md`:

- A lexicon was fitted to **test-set failures** and four hyperparameters swept on
  test, reporting +4.5 points. The leakage-free version gained nothing.
- The evaluation harness drifted into being the deliverable — a router that had
  never been compiled and never run on the ESP32, while the board ran an
  artifact that had already been archived.

The second is the reason the rule is "no Python, including throwaway
experiments": experiments become products.

## Do not run any of this

It is not maintained and does not reflect the current system. Read `../../README.md`.

## `tinyenc_test_firmware.bin`

The encoder-era firmware image that actually ran on the board, lifted out of
`esp32_encoder_firmware/build/` before that build tree was untracked (it was
~4400 regenerable files). SHA256 alongside. The build tree itself remains on
disk, just not in git.
