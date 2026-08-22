#!/usr/bin/env bash
# Full host regression. Run after any change, and especially after anything
# structural (housekeeping, refactors, moves). Hardware checks are separate —
# see esp32_router/README.md — because they need a board attached.
#
# Written after a housekeeping pass that moved a repository and rewrote
# .gitignore: the suite existed only as ad-hoc commands, so it could not be
# re-run to prove nothing broke.
set -u
cd "$(dirname "$0")/.."
D="data/train.json data/validation.json data/test.json data/nlu_home.csv"
P=0; F=0
chk(){ if [ "$2" = "$3" ]; then printf '  PASS  %s\n' "$1"; P=$((P+1));
       else printf '  FAIL  %s  (got "%s" want "%s")\n' "$1" "$2" "$3"; F=$((F+1)); fi; }
# Some checks need the local-only archive/ tree, which a fresh clone (and CI) will
# not have. Skip them explicitly and say so — a skipped check must never look
# like a passing one.
S=0
skip(){ printf '  SKIP  %s  (%s)\n' "$1" "$2"; S=$((S+1)); }
HAVE_ARCHIVE=0; [ -d archive/needle_upstream/.git ] && HAVE_ARCHIVE=1

echo "=== BUILD ==="
rm -rf c/bin
# Capture the build output so a failure shows WHAT broke, not just how many.
# CI reported 'got 11 want 0' with the diagnostics discarded by grep -c, which
# made a real portability break (strdup hidden under strict -std=c11 on glibc,
# libm unlinked) invisible from the log.
BUILD_OUT=$(make -s tools 2>&1)
BUILD_N=$(printf '%s\n' "$BUILD_OUT" | grep -cE 'warning|error')
chk "tools+tests build with zero warnings" "$BUILD_N" "0"
if [ "$BUILD_N" != "0" ]; then
  printf '%s\n' "$BUILD_OUT" | grep -E 'warning|error' | head -20 | sed 's/^/        /'
fi
chk "binary count" "$(ls c/bin | wc -l | tr -d ' ')" "11"

echo "=== CORPUS ==="
chk "corpus checksums" "$(shasum -a 256 -c data/SHA256 2>/dev/null | grep -c OK)" "4"

echo "=== EXHAUSTIVE PROOFS ==="
o=$(c/bin/t_popcnt 2>&1)
chk "popcount table exact over all 2^32 words" "$(echo "$o" | grep -c '4294967296/4294967296')" "1"
chk "t_dot algebraic rewrite exact" "$(echo "$o" | grep -c 'exact (2M random')" "1"

echo "=== BLOB ==="
# NEGATIVE CONTROL: prove blobfmt can REJECT. Mutation testing showed that
# rigging it to skip its size check left it printing "OK" and the suite green —
# a validator that always passes validates nothing.
# Truncate RELATIVE to the real blob. This was a hardcoded 700000, which stopped
# truncating anything the moment the shipped index shrank below that - the
# negative control silently became a copy of a valid blob, so blobfmt accepted
# it and the check could only ever fail-open.
head -c "20 20 12 61 79 80 81 701 702 33 98 100 204 250 395 398 399 400( 20 20 12 61 79 80 81 701 702 33 98 100 204 250 395 398 399 400wc -c < esp32_router/main/router.bin) - 1 ))" \
     esp32_router/main/router.bin > /tmp/_trunc.bin 2>/dev/null
c/bin/blobfmt /tmp/_trunc.bin >/dev/null 2>&1
chk "blobfmt REJECTS a truncated blob" "$?" "1"
# A blob with TRAILING bytes is the only input that reaches blobfmt's
# size-accounting guard (o != size). Truncation is caught earlier by the
# record-bounds guard, so the truncated case above proves nothing about it -
# mutation testing showed "blobfmt stops rejecting bad blobs" firing NOTHING
# once the truncation became size-1. Verified: with that guard disabled this
# input returns 0 and the truncated one still returns 1.
cat esp32_router/main/router.bin > /tmp/_trail.bin; printf 'X' >> /tmp/_trail.bin
c/bin/blobfmt /tmp/_trail.bin >/dev/null 2>&1; TRAIL_RC=$?
rm -f /tmp/_trail.bin
chk "blobfmt REJECTS a blob with trailing bytes" "$TRAIL_RC" "1"
rm -f /tmp/_trunc.bin
chk "layout matches doc/BLOB_FORMAT.md" "$(c/bin/blobfmt esp32_router/main/router.bin | grep -c 'OK: layout matches')" "1"
c/bin/eval esp32_router/main/router.bin data/validation.json >/dev/null 2>&1
chk "blob verifier (parity + index integrity)" "$?" "0"
# Capture cmp's status IMMEDIATELY. This check silently passed for a while
# because a later-inserted check landed between the cmp and the `chk "$?"`,
# so $? held the intervening check's status, not cmp's. Mutation testing
# caught it; reading the file did not.
c/bin/mkblob $D /tmp/_regress.bin >/dev/null 2>&1
cmp -s /tmp/_regress.bin esp32_router/main/router.bin; BLOB_RC=$?
rm -f /tmp/_regress.bin
chk "product firmware source tracked" "$([ -f esp32_router/main/product.c ] && git ls-files esp32_router/main/product.c | wc -l | tr -d ' ')" "1"
chk "product refuses to actuate the none class" "$(grep -c 'if (!strcmp(R.names\[cls\], "none")) return -2;' esp32_router/main/product.c)" "1"
chk "product reports both rejection causes" "$(grep -cE 'cls == -1|cls == -2' esp32_router/main/product.c)" "2"
# A stale 830 KB dim=512 blob sat TRACKED at c/router.bin from the first C
# commit to now, referenced by nothing and noticed by nothing. Blobs are the one
# artefact here that is both large and easy to leave behind, so pin the set.
chk "the only tracked .bin is the shipped blob" "$(git ls-files '*.bin' | tr '\n' ',')" "esp32_router/main/router.bin,"
chk "blob dim matches router.h RD" "$(od -An -tu4 -j4 -N4 esp32_router/main/router.bin | tr -d ' ')" "$(grep -E '^#define RD\s' c/src/router.h | grep -oE '[0-9]+')"
# Assert the SHIPPED BINARY, not just the harness. Everything above measures
# what compare computes; these two read the header of the file that gets
# flashed. n_index is the fully-SRAM-resident size (30 chunks x 128) and the
# threshold must equal router.h's - if either drifts, the device runs an
# operating point nothing measured.
chk "blob vectors 3840 (30 SRAM chunks)" "$(od -An -tu4 -j8 -N4 esp32_router/main/router.bin | tr -d ' ')" "3840"
chk "blob threshold matches router.h RSHIP_TH" "$(od -An -tu4 -j16 -N4 esp32_router/main/router.bin | tr -d ' ')" "$(grep -E '^#define RSHIP_TH\s' c/src/router.h | grep -oE '[0-9]+')"
chk "blob reproducible byte-identical" "$BLOB_RC" "0"

echo "=== SHIPPED CONFIG (ROW: 1=ROW 2=split 3=variant 4=acc 5=se 6=fa 7=wa 8=missed 9=kb 10=th 11=n) ==="
row=$(c/bin/compare $D --ship 2>&1 | grep '^ROW' | grep twin)
chk "recall 85.9"   "$(echo "$row" | cut -f4)"  "85.9"
chk "fa 6"          "$(echo "$row" | cut -f6)"  "6"
chk "wa 13"         "$(echo "$row" | cut -f7)"  "13"
chk "missed 14"     "$(echo "$row" | cut -f8)"  "14"
chk "index 240 KB"  "$(echo "$row" | cut -f9)"  "240"
chk "threshold 136" "$(echo "$row" | cut -f10)" "136"

echo "=== LEAK GUARDS (abort on failure) ==="
o=$(c/bin/compare $D --ship 2>&1)
# The two greps below confirm the guard REPORTED disjoint. They cannot tell a
# working guard from a disabled one — disabling the abort still prints the
# success line (mutation testing, METHOD.md #15). So first prove it FIRES:
# --leak deliberately reintroduces the 75.6%% dev leak and must abort.
c/bin/compare $D --leak >/dev/null 2>&1; LEAK_RC=$?
chk "leak guard ABORTS on a deliberate leak" "$LEAK_RC" "2"
chk "invariant RAN and reported on DEV"  "$(echo "$o" | grep -c 'index vs DEV .*disjoint')"  "1"
chk "invariant RAN and reported on TEST" "$(echo "$o" | grep -c 'index vs TEST .*disjoint')" "1"
chk "dev  code-overlap zero" "$(echo "$o" | grep 'diag. DEV'  | grep -c 'entry: 0 ')" "1"
chk "test code-overlap zero" "$(echo "$o" | grep 'diag. TEST' | grep -c 'entry: 0 ')" "1"

echo "=== NEGATIVE RESULTS still reproduce ==="
chk "gate == prior, bit-exact"       "$(c/bin/compare $D --gatecheck 2>&1 | grep -c 'mismatches 0   margin mismatches 0   EXACT')" "1"
chk "leaktest reproduces 75.6% leak" "$(c/bin/leaktest data/train.json data/nlu_home.csv 2>&1 | grep -c '75.6%')" "1"
chk "leakchk dev/test near-dup"      "$(c/bin/leakchk data/train.json data/validation.json data/test.json 2>&1 | grep -c 'near-duplicate rate')" "2"
chk "cuemine mines colour from index" "$(c/bin/cuemine data/train.json data/test.json iot_hue_lightchange 4 2>/dev/null | grep -cE '^  colors')" "1"

echo "=== DEVELOPER EXPERIENCE ==="
chk "--help exits 0" "$(c/bin/compare --help >/dev/null 2>&1; echo $?)" "0"
chk "--help documents --route" "$(c/bin/compare --help 2>&1 | grep -c -- '--route')" "1"
chk "--help groups retained negative results" "$(c/bin/compare --help 2>&1 | grep -c 'RETAINED NEGATIVE RESULTS')" "1"
chk "paths default to data/ (no positional args)" "$(c/bin/compare --ship 2>/dev/null | grep -c '^ROW')" "2"
chk "unknown flag refused with rc=1" "$(c/bin/compare --shipp >/dev/null 2>&1; echo $?)" "1"
chk "--route decision line names the class" "$(c/bin/compare --ship --route='turn off the kitchen light' 2>/dev/null | grep -cE '^ +decision +iot_hue_lightoff$')" "1"
chk "--route shows nearest stored utterances" "$(c/bin/compare --ship --route='turn off the kitchen light' 2>/dev/null | grep -cE '^ +[0-9]+ +iot_')" "5"
chk "--route reports score vs threshold" "$(c/bin/compare --ship --route='turn off the kitchen light' 2>/dev/null | grep -cE '^ +score +[0-9]+ +.threshold 136')" "1"
# exact-line matches: a substring test still passes if the message is corrupted
# by appending, which mutation testing caught. grep -Fx pins the whole line.
NC_LINE='    decision               none  (nearest match is not a command — no action)'
BT_LINE='    decision               none  (score below threshold — no action)'
chk "--route declines a non-command" "$(c/bin/compare --ship --route='what time does the train leave' 2>/dev/null | grep -Fxc "$NC_LINE")" "1"
chk "--route declines nonsense below threshold" "$(c/bin/compare --ship --route='zzz qqq xyzzy' 2>/dev/null | grep -Fxc "$BT_LINE")" "1"
chk "--route output is clean (no corpus chatter)" "$(c/bin/compare --ship --route=x 2>&1 | grep -c '\[inv\]')" "0"
chk "LICENSE present and tracked" "$([ -f LICENSE ] && git ls-files LICENSE | wc -l | tr -d ' ')" "1"
chk "LICENSE is MIT" "$(grep -c '^MIT License$' LICENSE)" "1"
chk "README states the licence" "$(grep -c '\[MIT\](LICENSE)' README.md)" "1"
chk "QUICKSTART present and tracked" "$([ -f doc/QUICKSTART.md ] && git ls-files doc/QUICKSTART.md | wc -l | tr -d ' ')" "1"

echo "=== DOCS ==="
b=0
for f in README.md doc/*.md journal/README.md esp32_router/README.md provenance/README.md results/README.md; do
  for l in $(grep -oE '\]\([^)#][^)]*\)' "$f" 2>/dev/null | tr -d ']()'); do
    case "$l" in http*) continue;; esac
    # A link may carry a fragment: doc/X.md#some-anchor. Check the file exists
    # AND that the anchor resolves inside it - otherwise a cross-file anchor
    # link is either rejected outright (it was) or accepted unchecked.
    p=${l%%#*}
    if [ ! -e "$(dirname "$f")/$p" ]; then
      echo "    broken: $f -> $p"; b=$((b+1)); continue
    fi
    case "$l" in
      *"#"*) case "$p" in
               *.md) frag=${l#*#}
                     grep '^#\{2,4\} ' "$(dirname "$f")/$p" |
                       awk '{sub(/^#+ /,"");a=tolower($0);gsub(/[^a-z0-9 -]/,"",a);gsub(/ /,"-",a);print a}' |
                       grep -qx "$frag" ||
                         { echo "    broken anchor: $f -> $l"; b=$((b+1)); } ;;
             esac ;;
    esac
  done
done
chk "markdown links resolve" "$b" "0"
chk "EXPERIMENTS anchors resolve" "$(comm -23 \
  <(grep -oE '\(#[a-z0-9-]+\)' doc/EXPERIMENTS.md | tr -d '()#' | sort -u) \
  <(grep '^#\{2,3\} ' doc/EXPERIMENTS.md | awk '{sub(/^#+ /,"");a=tolower($0);gsub(/[^a-z0-9 -]/,"",a);gsub(/ /,"-",a);print a}' | sort -u) \
  | wc -l | tr -d ' ')" "0"

echo "=== DOCS MATCH REALITY ==="
# METHOD.md #14 generalised: anything a doc asserts ABOUT THE REPO should be
# verified by the repo. These all went stale silently at least once.
chk "EXPERIMENTS contents covers every section" "$(comm -13 <(grep -oE '\(#[a-z0-9-]+\)' doc/EXPERIMENTS.md | tr -d '()#' | sort -u) <(grep '^## ' doc/EXPERIMENTS.md | sed 's/^## //' | awk '{a=tolower($0);gsub(/[^a-z0-9 -]/,"",a);gsub(/ /,"-",a);print a}' | sort -u) | grep -vcE '^(contents|look-up-a-fact)$')" "0"
chk "EXPERIMENTS contents has no duplicates" "$(sed -n '/^## Contents/,/^> Sections are chron/p' doc/EXPERIMENTS.md | grep -oE '\(#[a-z0-9-]+\)' | sort | uniq -d | wc -l | tr -d ' ')" "0"
chk "every c/src tool is in TOOLS.md" "$(for f in c/src/*.c; do grep -q "$(basename $f)" doc/TOOLS.md || echo x; done | wc -l | tr -d ' ')" "0"
chk "every archive/ subdir is in ARCHIVE.md" "$(for d in $(ls archive/ 2>/dev/null | grep -v README); do grep -q "$d" doc/ARCHIVE.md || echo x; done | wc -l | tr -d ' ')" "0"
chk "every boot suite is in esp32_router/README" "$(for s in $(sed -n '/^void app_main/,/^}/p' esp32_router/main/main.c | grep -oE '^    [a-z_]+\(' | tr -d '(' | grep -vE 'printf|t_popcnt_init'); do grep -q "$s" esp32_router/README.md || echo x; done | wc -l | tr -d ' ')" "0"
chk "journal README cycle count is current" "$(grep -oE '[0-9]+ cycles' journal/README.md | grep -oE '[0-9]+')" "$(ls journal/*.md | grep -v README | sed -E 's/.*\/([a-z0-9_]+)_(raw|nodes|reflect|synth)\.md/\1/' | sort -u | wc -l | tr -d ' ')"

echo "=== ARCHIVE (gitignored, must remain intact on disk) ==="
chk "archive untracked"        "$(git ls-files archive | wc -l | tr -d ' ')" "0"
chk "tracked provenance bundle checksum" "$(shasum -a 256 -c provenance/needle-upstream.bundle.sha256 2>/dev/null | grep -c OK)" "1"
chk "provenance bundle present and TRACKED" "$([ -f provenance/needle-upstream.bundle ] && git ls-files provenance/needle-upstream.bundle | wc -l | tr -d ' ')" "1"
if [ "$HAVE_ARCHIVE" = 1 ]; then
  chk "local archive bundle checksum" "$(cd archive/needle_upstream && shasum -a 256 -c needle_full.bundle.sha256 2>/dev/null | grep -c OK)" "1"
else skip "local archive bundle checksum" "archive/ is local-only"; fi
if [ "$HAVE_ARCHIVE" = 1 ]; then
  chk "upstream repo 270 commits" "$(git -C archive/needle_upstream rev-list --count HEAD 2>/dev/null)" "270"
else skip "upstream repo 270 commits" "archive/ is local-only"; fi
chk "doc/ARCHIVE.md present and tracked" "$([ -f doc/ARCHIVE.md ] && git ls-files doc/ARCHIVE.md | wc -l | tr -d ' ')" "1"
# We now have our own remote. The thing this check exists to catch is the repo
# ever pointing back at the upstream clone it was separated from.
chk "no remote points at the upstream clone" "$(git remote -v 2>/dev/null | grep -ci 'anjaustin/needle')" "0"

# The docs quote this suite's size. That number went stale the moment the
# suite grew, and nothing noticed. Check it against reality.
LIVE=$((P + F + S + 1))   # +1 for this check itself; skipped still count as checks
# Every doc that states a LIVE check count must state the same one, and it must
# be the real one. Three bugs lived here at once: the path was `QUICKSTART.md`
# (repo root, nonexistent) silenced by 2>/dev/null so that file was never
# checked at all; the pattern missed the hyphenated "55-check" form; and
# `sort -u | head -1` took the LOWEST number rather than requiring agreement, so
# a README quoting 55, 48 and 61 in three places passed. It did exactly that.
# doc/METHOD.md is excluded on purpose - it narrates past counts in past tense.
DOCN=$(grep -ohE '[0-9]+[- ]checks?\b' README.md doc/QUICKSTART.md esp32_router/README.md \
       | grep -oE '^[0-9]+' | sort -u)
chk "docs quote the live check count" "$(echo $DOCN)" "$LIVE"

echo
if [ "$S" -gt 0 ]; then echo "  ======== $P passed, $F failed, $S skipped ========"
else echo "  ======== $P passed, $F failed ========"; fi
[ "$F" -eq 0 ] || exit 1
