# Restoring the original ESP-AT firmware

The board shipped with ESP-AT v1.1.2 (WROOM32). Full 4 MB image backed up
2026-08-19 before any flashing.

    sha256  b8979005335ad47d53f167887db14d9f8cb3f8750811a85c245c6544dbb1504c

## Restore

    source ~/Projects/esp-idf/export.sh
    esptool.py --port /dev/cu.usbserial-0001 --baud 460800 \
      write_flash 0x0 esp_at_wroom32_v1.1.2_full_4MB.bin

Verify afterwards by capturing the boot log at 115200 — you should see
`Bin version(Wroom32):1.1.2` and the wifi init lines.

## Note

Read/write the board plugged **directly** into the Mac. Through a USB hub,
sustained transfers drop data ("Corrupt data, expected 0x1000 bytes but
received 0x10") and the backup will not verify.

## Where the image lives now

The 4 MB image is **archived, not tracked**: `archive/stray/`. It is a public
Espressif ESP-AT v1.1.2 release, so it is re-obtainable upstream, and the sha256
above is what verifies any copy you get. Tracking 4 MB of redownloadable vendor
firmware in a source repository is weight without redundancy.

`nlu_home.csv` and the MASSIVE splits are handled the same way: fetched by
`make fetch`, verified against `data/SHA256`, never vendored.
