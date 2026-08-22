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
chk "tools+tests build with zero warnings" "$(make -s tools 2>&1 | grep -cE 'warning|error')" "0"
chk "binary count" "$(ls c/bin | wc -l | tr -d ' ')" "10"

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
head -c 700000 esp32_router/main/router.bin > /tmp/_trunc.bin 2>/dev/null
c/bin/blobfmt /tmp/_trunc.bin >/dev/null 2>&1
chk "blobfmt REJECTS a truncated blob" "$?" "1"
rm -f /tmp/_trunc.bin
chk "layout matches doc/BLOB_FORMAT.md" "$(c/bin/blobfmt esp32_router/main/router.bin | grep -c 'OK: layout matches')" "1"
c/bin/eval esp32_router/main/router.bin data/validation.json >/dev/null 2>&1
chk "blob verifier (parity + index integrity)" "$?" "0"
c/bin/mkblob $D /tmp/_regress.bin >/dev/null 2>&1
cmp -s /tmp/_regress.bin esp32_router/main/router.bin
chk "blob reproducible byte-identical" "$?" "0"; rm -f /tmp/_regress.bin

echo "=== SHIPPED CONFIG (ROW: 1=ROW 2=split 3=variant 4=acc 5=se 6=fa 7=wa 8=missed 9=kb 10=th 11=n) ==="
row=$(c/bin/compare $D --ship 2>&1 | grep '^ROW' | grep twin)
chk "recall 85.9"   "$(echo "$row" | cut -f4)"  "85.9"
chk "fa 1"          "$(echo "$row" | cut -f6)"  "1"
chk "wa 13"         "$(echo "$row" | cut -f7)"  "13"
chk "missed 14"     "$(echo "$row" | cut -f8)"  "14"
chk "index 656 KB"  "$(echo "$row" | cut -f9)"  "656"
chk "threshold 136" "$(echo "$row" | cut -f10)" "136"

echo "=== LEAK GUARDS (abort on failure) ==="
o=$(c/bin/compare $D --ship 2>&1)
chk "index vs DEV disjoint"  "$(echo "$o" | grep -c 'index vs DEV .*disjoint')"  "1"
chk "index vs TEST disjoint" "$(echo "$o" | grep -c 'index vs TEST .*disjoint')" "1"
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
chk "LICENSE present and tracked" "$(git ls-files LICENSE | wc -l | tr -d ' ')" "1"
chk "LICENSE is MIT" "$(grep -c '^MIT License$' LICENSE)" "1"
chk "README states the licence" "$(grep -c '\[MIT\](LICENSE)' README.md)" "1"
chk "QUICKSTART tracked" "$(git ls-files doc/QUICKSTART.md | wc -l | tr -d ' ')" "1"

echo "=== DOCS ==="
b=0
for f in README.md doc/*.md journal/README.md esp32_router/README.md provenance/README.md results/README.md; do
  for l in $(grep -oE '\]\([^)#][^)]*\)' "$f" 2>/dev/null | tr -d ']()'); do
    case "$l" in http*) continue;; esac
    [ -e "$(dirname "$f")/$l" ] || { echo "    broken: $f -> $l"; b=$((b+1)); }
  done
done
chk "markdown links resolve" "$b" "0"
chk "EXPERIMENTS anchors resolve" "$(comm -23 \
  <(grep -oE '\(#[a-z0-9-]+\)' doc/EXPERIMENTS.md | tr -d '()#' | sort -u) \
  <(grep '^#\{2,3\} ' doc/EXPERIMENTS.md | awk '{sub(/^#+ /,"");a=tolower($0);gsub(/[^a-z0-9 -]/,"",a);gsub(/ /,"-",a);print a}' | sort -u) \
  | wc -l | tr -d ' ')" "0"

echo "=== ARCHIVE (gitignored, must remain intact on disk) ==="
chk "archive untracked"        "$(git ls-files archive | wc -l | tr -d ' ')" "0"
chk "tracked provenance bundle checksum" "$(shasum -a 256 -c provenance/needle-upstream.bundle.sha256 2>/dev/null | grep -c OK)" "1"
chk "provenance bundle is TRACKED (off-disk backup)" "$(git ls-files provenance/needle-upstream.bundle | wc -l | tr -d ' ')" "1"
if [ "$HAVE_ARCHIVE" = 1 ]; then
  chk "local archive bundle checksum" "$(cd archive/needle_upstream && shasum -a 256 -c needle_full.bundle.sha256 2>/dev/null | grep -c OK)" "1"
else skip "local archive bundle checksum" "archive/ is local-only"; fi
if [ "$HAVE_ARCHIVE" = 1 ]; then
  chk "upstream repo 270 commits" "$(git -C archive/needle_upstream rev-list --count HEAD 2>/dev/null)" "270"
else skip "upstream repo 270 commits" "archive/ is local-only"; fi
chk "doc/ARCHIVE.md tracked"   "$(git ls-files doc/ARCHIVE.md | wc -l | tr -d ' ')" "1"
# We now have our own remote. The thing this check exists to catch is the repo
# ever pointing back at the upstream clone it was separated from.
chk "no remote points at the upstream clone" "$(git remote -v 2>/dev/null | grep -ci 'anjaustin/needle')" "0"

# The docs quote this suite's size. That number went stale the moment the
# suite grew, and nothing noticed. Check it against reality.
LIVE=$((P + F + S + 1))   # +1 for this check itself; skipped still count as checks
DOC=$(grep -ohE '[0-9]+ checks' README.md QUICKSTART.md 2>/dev/null | grep -oE '[0-9]+' | sort -u | head -1)
chk "docs quote the live check count" "$DOC" "$LIVE"

echo
if [ "$S" -gt 0 ]; then echo "  ======== $P passed, $F failed, $S skipped ========"
else echo "  ======== $P passed, $F failed ========"; fi
[ "$F" -eq 0 ] || exit 1
