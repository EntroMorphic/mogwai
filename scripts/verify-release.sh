#!/usr/bin/env bash
# Flash a PUBLISHED release to a real board and verify it, then record that it
# happened. No Python.
#
# Why this exists: v0.1.2, v0.1.3 and v0.1.4 were published without anyone
# putting them on hardware. The reasoning was "byte-equivalent modulo metadata
# to something already verified", which is sound and is still an inference. The
# artifact a stranger downloads is the thing that has to boot.
#
# CI cannot do this -- there is no board on a runner -- so the check in
# regress.sh asserts the RECORD, exactly as results/TEST_BUDGET does for
# held-out reads. It cannot force the flash. It can make an unverified release
# impossible to ignore.
#
# usage: scripts/verify-release.sh v0.1.4 [/dev/cu.usbserial-0001]
set -euo pipefail
cd "$(dirname "$0")/.."

TAG="${1:?usage: verify-release.sh <tag> [port]}"
PORT="${2:-/dev/cu.usbserial-0001}"
LOG=results/RELEASE_VERIFIED.tsv
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT

command -v esptool.py >/dev/null || { echo "esptool.py not found — . \$IDF_PATH/export.sh" >&2; exit 1; }
[ -x c/bin/devtalk ] || make c/bin/devtalk >/dev/null

echo "== $TAG =="
gh release download "$TAG" -D "$WORK" --clobber >/dev/null 2>&1 \
  || { echo "  cannot download $TAG" >&2; exit 1; }

# The published checksum must describe the published bytes.
( cd "$WORK" && shasum -a 256 -c mogwai-esp32.bin.sha256 >/dev/null ) \
  || { echo "  CHECKSUM MISMATCH — published sha256 does not describe the asset" >&2; exit 1; }
SHA=$(shasum -a 256 "$WORK/mogwai-esp32.bin" | cut -d' ' -f1)
echo "  asset $(wc -c < "$WORK/mogwai-esp32.bin" | tr -d ' ') B, sha ${SHA:0:16}"

# Erase first: a release must boot on a chip with nothing else on it.
esptool.py --chip esp32 --port "$PORT" erase_flash >/dev/null 2>&1
esptool.py --chip esp32 --port "$PORT" --baud 460800 write_flash 0x0 "$WORK/mogwai-esp32.bin" >/dev/null 2>&1
echo "  flashed to an erased chip"

BOOT=$(c/bin/devtalk "$PORT" -r -w 20000 2>/dev/null | LC_ALL=C tr -cd '\11\12\15\40-\176')
fail=0
grep -q "3840/3840 vectors in SRAM (100%)" <<<"$BOOT" || { echo "  FAIL: index not fully resident"; fail=1; }
grep -q "0 MISMATCHED"                     <<<"$BOOT" || { echo "  FAIL: addressing not verified"; fail=1; }

ACT=$(c/bin/devtalk "$PORT" -s "turn the lights on" -w 5000 2>/dev/null | LC_ALL=C tr -cd '\11\12\15\40-\176')
grep -q "iot_hue_lighton score 227" <<<"$ACT" || { echo "  FAIL: expected iot_hue_lighton score 227"; fail=1; }
grep -q "ACTUATED"                  <<<"$ACT" || { echo "  FAIL: did not actuate"; fail=1; }

REJ=$(c/bin/devtalk "$PORT" -s "what time does the train leave" -w 5000 2>/dev/null | LC_ALL=C tr -cd '\11\12\15\40-\176')
grep -q "REJECTED" <<<"$REJ" || { echo "  FAIL: non-command was not rejected"; fail=1; }

[ "$fail" = 0 ] || { echo "  $TAG NOT VERIFIED"; exit 1; }

US=$(grep -oE '[0-9]+ us' <<<"$ACT" | tail -1 | grep -oE '[0-9]+')
echo "  100% resident, 0 MISMATCHED, score 227 actuated, non-command rejected, ${US} us"

[ -s "$LOG" ] || printf 'tag\tsha256\tdate\tus\tcommit\n' > "$LOG"
grep -q "^${TAG}	" "$LOG" && sed -i '' "/^${TAG}	/d" "$LOG"
printf '%s\t%s\t%s\t%s\t%s\n' "$TAG" "$SHA" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "${US:-?}" "$(git rev-parse --short HEAD)" >> "$LOG"
{ head -1 "$LOG"; tail -n +2 "$LOG" | sort -V; } > "$LOG.tmp" && mv "$LOG.tmp" "$LOG"
echo "  recorded in $LOG"
