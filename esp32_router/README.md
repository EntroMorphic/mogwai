# `esp32_router` — VALIDATION firmware, not a product build

Flashing this does **not** give you a device that listens for commands. It runs
a parity check and four benchmark suites over the serial console and stops.
It exists to prove the device computes bit-identically to the host, and to
measure where the time goes.

    ===== twin-ternary router on ESP32 =====
    class agreement / score agreement       parity vs 64 host-computed queries
    rtos_tax()      what FreeRTOS costs in the hot loop      (answer: 0.05%)
    profile()       memory vs compute, full index vs cache-resident
    bench_mt()      the same scan split across both cores
    redteam()       15 repeats per config + a pure size sweep
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
free; the single-core number (43.5 ms) is the one that survives deployment.

## Recovery

The board shipped with ESP-AT. The original image and a verified restore
procedure are in [../board_backup/](../board_backup/RESTORE.md).
