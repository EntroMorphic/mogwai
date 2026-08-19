#!/usr/bin/env bash
# Fetch every corpus. No Python. Checksums recorded so a run is reproducible.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p data
for sp in train validation test; do
  [ -s "data/$sp.json" ] || {
    curl -sL -o "data/$sp.json.gz" \
      "https://huggingface.co/datasets/mteb/amazon_massive_intent/resolve/main/$sp/en.json.gz"
    gunzip -f "data/$sp.json.gz"; }
done
[ -s data/nlu_home.csv ] || curl -sL -o data/nlu_home.csv \
  "https://raw.githubusercontent.com/xliuhw/NLU-Evaluation-Data/master/AnnotatedData/NLU-Data-Home-Domain-Annotated-All.csv"
shasum -a 256 data/train.json data/validation.json data/test.json data/nlu_home.csv > data/SHA256
echo "corpora ready:"; wc -l data/*.json data/*.csv | sed 's/^/  /'
