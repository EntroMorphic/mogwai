#!/usr/bin/env bash
# Mutation-test the regression suite: break each guarded thing on purpose and
# record which checks notice. A check that no mutation can make fail is
# decoration — see doc/METHOD.md #13 and #15.
#
# Restores with cp snapshots, NEVER git checkout: git restores from HEAD, so it
# destroys uncommitted work. That mistake cost a broken push (#16).
#
# Output: per-mutation, the checks that fired; then the checks NOTHING fired.
set -u
cd "$(dirname "$0")/.."
SNAP=$(mktemp -d)
trap 'rm -rf "$SNAP"' EXIT

save(){ mkdir -p "$SNAP/$(dirname "$1")"; cp -R "$1" "$SNAP/$1" 2>/dev/null; }
restore(){ rm -rf "$1"; cp -R "$SNAP/$1" "$1" 2>/dev/null; }

# ONE list, used by BOTH save and restore. They were two hardcoded literals and
# drifted the instant a path was added to one of them: product.c was saved but
# never restored, so a run left the working tree mutated and two checks failing.
# METHOD #17 says a mutation harness is not trusted until its restores are
# verified - that has to be structural, not remembered.
MPATHS="c/src c/test doc README.md LICENSE scripts/regress.sh Makefile
        data/SHA256 esp32_router/main/router.bin esp32_router/README.md
        journal/README.md provenance .gitignore esp32_router/main/product.c"

for f in $MPATHS; do save "$f"; done

FIRED=results/mutation-coverage.txt; : > "$FIRED"
DETAIL=results/mutation-detail.txt; : > "$DETAIL"
run_mut(){                       # $1 label   $2 mutation
  eval "$2" >/dev/null 2>&1
  out=$(./scripts/regress.sh 2>&1)
  names=$(printf '%s\n' "$out" | grep '  FAIL  ' | sed 's/.*FAIL  //; s/  (got.*//')
  n=$(printf '%s\n' "$names" | grep -c . )
  printf '%s\n' "$names" >> "$FIRED"
  { printf '=== %s\n' "$1"; printf '%s\n' "$names" | sed 's/^/    /'; } >> "$DETAIL"
  if [ "$n" -gt 0 ]; then printf '  %-44s fired %2d\n' "$1" "$n"
  else                    printf '  %-44s ** NOTHING FIRED **\n' "$1"; fi
  for f in $MPATHS; do restore "$f"; done
  git reset -q 2>/dev/null          # index mutations leaked between runs otherwise
  make -s tools >/dev/null 2>&1
}

echo "=== mutations ==="
run_mut "compiler warning introduced"        "printf 'static int _m;\n' >> c/src/cascade.c"
run_mut "a tool source deleted"              "rm -f c/src/cuemine.c"
run_mut "corpus checksum altered"            "sed -i '' '1s/^0/1/' data/SHA256"
run_mut "popcount table corrupted"           "sed -i '' 's/P8(0), P8(1), P8(1), P8(2)/P8(0), P8(1), P8(1), P8(3)/' c/src/ternary.c"
run_mut "t_dot rewrite broken"               "sed -i '' 's/return agree - 2 \* disagree;/return agree - disagree;/' c/src/ternary.c"
run_mut "blobfmt stops rejecting bad blobs"  "sed -i '' 's|if (o != size) { printf(\"  FAIL: %ld bytes unaccounted|if (0) { printf(\"  FAIL: %ld bytes unaccounted|' c/test/blobfmt.c"
run_mut "blob bytes corrupted"               "dd if=/dev/zero of=esp32_router/main/router.bin bs=1 seek=40000 count=3000 conv=notrunc"
run_mut "RD changed, blob left alone"        "sed -i '' 's/^#define RD        256/#define RD        128/' c/src/router.h"
run_mut "shipped threshold changed"          "sed -i '' 's/#define RSHIP_TH  136/#define RSHIP_TH  130/' c/src/router.h"
run_mut "Dice smoothing changed"             "sed -i '' 's/#define TSMOOTH 8/#define TSMOOTH 3/' c/src/ternary.c"
run_mut "leak guard disabled"                "sed -i '' 's/if (hits)/if (0)/' c/src/invariants.c"
run_mut "code-overlap detector disabled"     "sed -i '' 's|if (!memcmp(\&TI\[j\], q, sizeof(tvec))) return 1;|if (0) return 1;|' c/src/compare.c"
run_mut "gate diverges from prior"           "sed -i '' 's|sc\[g->cls\[t\]\] += g->del\[t\];|sc[g->cls[t]] += g->del[t] + 1;|' c/src/gate.c"
run_mut "leaktest under-reports"             "sed -i '' 's|leak,100.0\*leak/D_n|leak/2,50.0*leak/D_n|' c/src/leaktest.c"
run_mut "leakchk output changed"             "sed -i '' 's/near-duplicate rate/near-dup ratio/' c/src/leakchk.c"
run_mut "cuemine finds nothing"              "sed -i '' 's/if(Wt\[i\]<minc || Wc\[i\]\[c\]==0) continue;/if(Wt[i]<minc*9999 || Wc[i][c]==0) continue;/' c/src/cuemine.c"
run_mut "--help loses --route"               "sed -i '' 's|--route=\\\\\"turn off the kitchen light\\\\\"   route one|--XX=\\\\\"x\\\\\"   route one|' c/src/compare.c"
run_mut "--help loses the negatives group"   "sed -i '' 's/RETAINED NEGATIVE RESULTS/RETAINED XX/' c/src/compare.c"
run_mut "--help returns nonzero"             "sed -i '' 's|{ usage(); return 0; }|{ usage(); return 3; }|' c/src/compare.c"
run_mut "unknown flags silently ignored"     "sed -i '' 's|else { fprintf(stderr,\"  unknown flag: %s\\\\n  try --help\\\\n\", a); return 1; }|else { }|' c/src/compare.c"
run_mut "path defaults removed"              "sed -i '' 's|if (np == 0) {|if (0) {|' c/src/compare.c"
run_mut "--route decision line broken"       "sed -i '' 's|\"decision\",|\"decisionXX\",|' c/src/compare.c"
run_mut "--route neighbours suppressed"      "sed -i '' 's|for (int k = 0; k < 5 \&\& bi\[k\] >= 0; k++)|for (int k = 0; k < 0; k++)|' c/src/compare.c"
run_mut "--route score line broken"          "sed -i '' 's|\"score\", bs\[0\], th, bs\[0\]-th|\"scoreXX\", bs[0], th, bs[0]-th|' c/src/compare.c"
run_mut "--route none-reason changed"        "sed -i '' 's|nearest match is not a command|nearest match is no command|' c/src/compare.c"
run_mut "--route leaks corpus chatter"       "sed -i '' 's|if(ROUTE1 \|\| REPL) INV_QUIET = 1;|;|' c/src/compare.c"
run_mut "LICENSE removed"                    "rm -f LICENSE"
run_mut "LICENSE changed to Apache"          "sed -i '' '1s/^MIT License/Apache License/' LICENSE"
run_mut "README drops the licence line"      "sed -i '' 's|\[MIT\](LICENSE)|MIT|' README.md"
run_mut "QUICKSTART removed"                 "rm -f doc/QUICKSTART.md"
run_mut "broken markdown link"               "printf '\n[x](doc/NOPE.md)\n' >> README.md"
run_mut "broken EXPERIMENTS anchor"          "printf '\n[x](#no-such-anchor-here)\n' >> doc/EXPERIMENTS.md"
run_mut "EXPERIMENTS section without TOC"    "printf '\n## Untocd Section\n\ntext\n' >> doc/EXPERIMENTS.md"
run_mut "EXPERIMENTS TOC duplicated entry"   "sed -i '' 's|^## Contents|## Contents\n- [dup](#look-up-a-fact)\n- [dup](#look-up-a-fact)|' doc/EXPERIMENTS.md"
run_mut "a c/src tool undocumented"          "sed -i '' 's|\`cuemine.c\`|\`XX.c\`|' doc/TOOLS.md"
run_mut "an archive subdir undocumented"     "sed -i '' 's|archive/stray|archive/XX|g' doc/ARCHIVE.md"
run_mut "a boot suite undocumented"          "sed -i '' 's|two_stage()|XX()|' esp32_router/README.md"
# Was hardcoded "7 cycles" and went INERT the moment the journal reached 8 - the
# same "mutation hardcodes a count that later changed" failure METHOD #17
# already recorded, recurring. Match the digits instead of a literal.
run_mut "journal cycle count stale"          "sed -i '' -E 's#[0-9]+ cycles#99 cycles#' journal/README.md"
run_mut "provenance bundle corrupted"        "printf 'X' >> provenance/needle-upstream.bundle"
run_mut "provenance bundle untracked"        "git rm -q --cached provenance/needle-upstream.bundle"
run_mut "doc/ARCHIVE.md untracked"           "git rm -q --cached doc/ARCHIVE.md"
run_mut "archive/ becomes tracked"           "git add -f archive/README.md"
run_mut "upstream remote re-added"           "git remote add origin2 https://github.com/anjaustin/needle.git"
# archive/ is 1 GB and cannot go in the snapshot, so these two mutate in place
# with their own targeted restore.
printf "  %-44s " "local archive bundle corrupted"
printf X >> archive/needle_upstream/needle_full.bundle 2>/dev/null
out=$(./scripts/regress.sh 2>&1); nm=$(printf '%s\n' "$out" | grep '  FAIL  ' | sed 's/.*FAIL  //; s/  (got.*//')
printf '%s\n' "$nm" >> "$FIRED"; { printf '=== local archive bundle corrupted\n'; printf '%s\n' "$nm" | sed 's/^/    /'; } >> "$DETAIL"
printf "fired %2d\n" "$(printf '%s\n' "$nm" | grep -c .)"
# truncate the appended byte back off
cp provenance/needle-upstream.bundle archive/needle_upstream/needle_full.bundle

printf "  %-44s " "upstream repo commit count changed"
UPREF=$(git -C archive/needle_upstream rev-parse HEAD 2>/dev/null)
UPBR=$(git -C archive/needle_upstream rev-parse --abbrev-ref HEAD 2>/dev/null)
git -C archive/needle_upstream update-ref "refs/heads/$UPBR" "$UPREF~1" 2>/dev/null
out=$(./scripts/regress.sh 2>&1); nm=$(printf '%s\n' "$out" | grep '  FAIL  ' | sed 's/.*FAIL  //; s/  (got.*//')
printf '%s\n' "$nm" >> "$FIRED"; { printf '=== upstream repo commit count changed\n'; printf '%s\n' "$nm" | sed 's/^/    /'; } >> "$DETAIL"
printf "fired %2d\n" "$(printf '%s\n' "$nm" | grep -c .)"
git -C archive/needle_upstream update-ref "refs/heads/$UPBR" "$UPREF" 2>/dev/null

run_mut "leak guard disabled entirely"       "sed -i '' 's/    if (hits)/    if (0)/' c/src/invariants.c"
run_mut "doc check count goes stale"         "sed -i '' -E 's/[0-9]+ checks/40 checks/g' README.md"

# --- checks added later that no mutation reached ---------------------------
# Coverage is not retroactive: every check added after this file was written
# arrived UNCOVERED. These five close that gap.
run_mut "blob vector count changed"          "printf '\\377' | dd of=esp32_router/main/router.bin bs=1 seek=9 conv=notrunc"
run_mut "a stray blob gets tracked"          "git add -f archive/superseded_blobs/router-d512-1805441.bin"
run_mut "product firmware untracked"         "git rm -q --cached esp32_router/main/product.c"
run_mut "product actuates the none class"    "sed -i '' 's|if (!strcmp(R.names\\[cls\\], \"none\")) return -2;|if (0) return -2;|' esp32_router/main/product.c"
run_mut "product loses a rejection cause"    "sed -i '' 's|cls == -2|cls == -9|g' esp32_router/main/product.c"

git reset -q 2>/dev/null; git remote remove origin2 2>/dev/null


# The harness must verify its own restores. A run that leaves the tree mutated
# has silently corrupted the thing it was measuring, and the "covered N of M"
# denominator quietly drops because failing checks are neither PASS nor SKIP -
# which is exactly how the product.c leak showed up: 58 of 61, not 58 of 63.
# Compared against the snapshot, not against git, so it is correct even when the
# run starts from a dirty tree.
bad=0
for f in $MPATHS; do
  diff -r -q "$SNAP/$f" "$f" >/dev/null 2>&1 || { echo "  ** NOT RESTORED: $f"; bad=1; }
done
if [ "$bad" -ne 0 ]; then echo "  ** the tree is still mutated - fix before trusting any number above"; exit 1; fi
echo "  restore verified: every mutated path matches its pre-run snapshot"
echo
echo "=== checks NO mutation could make fail ==="
./scripts/regress.sh 2>&1 | grep -E '^  (PASS|SKIP)' | sed -E 's/^  (PASS|SKIP)  //' | sort -u > /tmp/_all
sort -u "$FIRED" | grep -v '^$' > /tmp/_fired
comm -23 /tmp/_all /tmp/_fired | sed 's/^/  UNCOVERED  /'
echo "  ---- covered $(comm -12 /tmp/_all /tmp/_fired | wc -l | tr -d ' ') of $(wc -l < /tmp/_all | tr -d ' ') ----"
