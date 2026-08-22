# `esp32_router` — two firmwares: the device, and the validation harness

Two firmwares from one component. They share `router.c`/`ternary.c`, which are
**symlinks into `../c/src`**, so the device runs the arithmetic the host
measured rather than a copy of it.

    idf.py -DPRODUCT=1 -DRD=256 -DTPOPCNT=1 build flash monitor   # the device
    idf.py            -DRD=256 -DTPOPCNT=1 build flash monitor    # validation

## `PRODUCT=1` — the device

Utterance in over UART, GPIO out. Type a sentence, a pin moves.

    > turn the lights on
      iot_hue_lighton score 227  (margin +91)
      ACTUATED     light -> ON  (duty 255/255)
      43920 us

    > is it going to rain tomorrow
      REJECTED     nearest match is not a command — no output changed
      43797 us

| intent | effect |
|---|---|
| `iot_hue_lighton` / `lightoff` | LEDC PWM on **GPIO2** to full / zero |
| `iot_hue_lightup` / `lightdim` | PWM ±64 of 255 |
| `iot_hue_lightchange` | PWM to half if dark (no RGB wired) |
| `iot_wemo_on` / `wemo_off` | **GPIO4** level |
| `iot_cleaning` | **GPIO16** pulsed 250 ms |
| `iot_coffee` | **GPIO17** pulsed 250 ms |

**Nothing actuates unless the router accepts**, and there are two distinct ways
it declines — both reported separately, because they are different faults:

- *score below threshold* — nothing in the index is close enough
- *nearest match is not a command* — the closest stored utterance is a negative

That second case was a real bug found only by running the thing: `route()`
returns the class index when the score clears the bar, and the `none` class is a
valid index. The firmware printed **ACTUATED** for "what time does the train
leave". No pin moved, because the table has no `none` case — but reporting an
actuation for a non-command is the one thing this system must never do.
Held-out, false actuations are **8 in 2754 non-commands (0.29%)**.

GPIO2 is the onboard LED on most ESP32 devkits, so the light intents are
visible without wiring anything.

## `PRODUCT=0` (default) — validation

Flashing this does **not** give you a device. It runs a parity check and several
benchmark suites over serial and stops.

    class agreement / score agreement       parity vs 64 host-computed queries
    rtos_tax()          what FreeRTOS costs in the hot loop  (answer: 0.05%%)
    profile()           memory vs compute, full index vs cache-resident
    bench_mt()          the same scan split across both cores
    redteam()           15 repeats per config + a pure size sweep
    dram_vs_flash()     same vectors from SRAM vs flash-mapped — 2.58x apart
    bulk_and_pipeline() bulk flash rate (24 MB/s) and a double-buffered scan
    two_stage()         coarse-to-fine with an SRAM signature table
Expect a few minutes of output. `PARITY EXACT` is the line that matters: it is
printed only when class **and** bit-exact score match on all 64 references.
Anything else means the device and host disagree — see
[../doc/BLOB_FORMAT.md](../doc/BLOB_FORMAT.md).

## Build

    idf.py -DRD=256 -DTPOPCNT=1 build flash monitor

**Pass both flags on every invocation.** `idf.py` caches `-D` variables, so a
later `idf.py build` silently reuses whatever was set last. This produced a
bogus timing and a false `PARITY FAILED` during the flash-mode sweep — see
`doc/METHOD.md` §8. When in doubt, `rm -rf build`.

`RD` **must** match the blob. `mkblob` built with a different `RD` produces a
blob this firmware will reject at load (dim mismatch) rather than misread.

## The sources are symlinks

`router.c`, `router.h`, `ternary.c`, `ternary.h` are symlinks into `../c/src/`.
They were hand-copies once and drifted, which makes a parity check meaningless —
it would be comparing the host against different arithmetic. Do not replace them
with copies.

## Turning this into a product firmware

Delete the `rtos_tax()`, `profile()`, `bench_mt()` and `redteam()` calls from
`app_main()`. What remains — `t_popcnt_init()`, the blob load, and `route()` —
is the whole router. `route()` is ~15 lines and needs no other scaffolding.

Also note `bench_mt()` uses core 1. On a device running WiFi, core 1 is not
free; the single-core number is the one that survives deployment. With the index
chunked into SRAM the same scan is 34.3 ms - see doc/EXPERIMENTS.md.

## Recovery

The board shipped with ESP-AT. The original image and a verified restore
procedure are in [../board_backup/](../board_backup/RESTORE.md).
