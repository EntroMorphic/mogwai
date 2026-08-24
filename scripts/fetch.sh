#!/usr/bin/env bash
# Fetch every corpus. No Python.
#
# Two things this script must do, and one it must NOT.
#
# It pins upstream to IMMUTABLE revisions. Both sources were previously fetched
# from moving refs -- `resolve/main` on HuggingFace and `master` on GitHub -- so
# upstream could change the corpus under us at any time and nothing would say so.
#
# It VERIFIES against the tracked data/SHA256 rather than regenerating it. The
# previous version ended with `shasum ... > data/SHA256`, which meant a changed
# upstream was downloaded, the checksums were rewritten to match, and the
# regression suite's "corpus checksums" check compared the data against a file
# derived from that same data. It could not fail. The only signal was a git diff
# on data/SHA256 that a human had to happen to notice.
#
# It must NOT vendor the corpora. They are not this project's to redistribute;
# see the licence section of the README. What is tracked is the checksum and the
# revision, which together make a fetch reproducible without republishing anyone
# else's data.
set -euo pipefail
cd "$(dirname "$0")/.."

MASSIVE_REV=940fd47a81eaa7f2cc7b129674d945d618ac38c2   # mteb/amazon_massive_intent
NLU_REV=f6071b496b17d71e6eb43f543af0707f4ff30557       # xliuhw/NLU-Evaluation-Data

mkdir -p data
for sp in train validation test; do
  [ -s "data/$sp.json" ] || {
    curl -fsSL -o "data/$sp.json.gz" \
      "https://huggingface.co/datasets/mteb/amazon_massive_intent/resolve/$MASSIVE_REV/$sp/en.json.gz"
    gunzip -f "data/$sp.json.gz"; }
done
[ -s data/nlu_home.csv ] || curl -fsSL -o data/nlu_home.csv \
  "https://raw.githubusercontent.com/xliuhw/NLU-Evaluation-Data/$NLU_REV/AnnotatedData/NLU-Data-Home-Domain-Annotated-All.csv"

if [ "${1:-}" = "--record" ]; then
  # Deliberately re-baseline. Only for a corpus change you INTEND, and the diff
  # to data/SHA256 belongs in the commit that explains why.
  shasum -a 256 data/train.json data/validation.json data/test.json data/nlu_home.csv > data/SHA256
  echo "recorded new corpus checksums:"; cat data/SHA256 | sed 's/^/  /'
elif [ -s data/SHA256 ]; then
  if ! shasum -a 256 -c data/SHA256 >/dev/null 2>&1; then
    echo "CORPUS MISMATCH — what was fetched is not what this repo was built on." >&2
    shasum -a 256 -c data/SHA256 2>&1 | grep -v ': OK$' | sed 's/^/  /' >&2
    echo "  Upstream may have changed, or a download was truncated." >&2
    echo "  If the change is intended: scripts/fetch.sh --record" >&2
    exit 1
  fi
  echo "corpus checksums verified against data/SHA256"
else
  echo "no data/SHA256 to verify against — run scripts/fetch.sh --record" >&2
  exit 1
fi
echo "corpora ready:"; wc -l data/*.json data/*.csv | sed 's/^/  /'
