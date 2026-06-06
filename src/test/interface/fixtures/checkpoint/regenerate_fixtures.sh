#!/usr/bin/env bash
# Regenerate golden CSV and checkpoint fixtures for checkpoint resume tests.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../../../.." && pwd)"
cd "$ROOT"
FIXTURES=src/test/interface/fixtures/checkpoint

patch_checkpoint_num_samples() {
  local checkpoint_path="$1"
  local num_samples="$2"
  python3 - "$checkpoint_path" "$num_samples" <<'PY'
import json
import sys
from pathlib import Path

path = Path(sys.argv[1])
num_samples = int(sys.argv[2])
text = path.read_text()
state = json.loads(text)
if state.get("iteration") != 500:
    raise SystemExit(f"expected iteration 500, got {state.get('iteration')}")
old = f'"num_samples" : {state["num_samples"]}'
new = f'"num_samples" : {num_samples}'
if old not in text:
    raise SystemExit(f"could not patch num_samples in {path}")
path.write_text(text.replace(old, new, 1))
PY
}

echo "=== diag_e eight_schools fixtures ==="
MODEL=src/test/test-models/eight_schools
DATA=src/test/test-models/eight_schools.data.json
OUT=test/checkpoint_regen_output.csv
CHECKPOINT="${OUT%.csv}_checkpoint.json"
COMMON="random seed=1234 id=1 method=sample num_warmup=200 num_samples=800 thin=1"

rm -f "$OUT" "$CHECKPOINT"
"$MODEL" $COMMON checkpoint_freq=0 data file="$DATA" output refresh=50 file="$OUT"
cp "$OUT" "$FIXTURES/golden_full.csv"
echo "Wrote $FIXTURES/golden_full.csv"

rm -f "$OUT" "$CHECKPOINT"
CMDSTAN_KEEP_CHECKPOINT=1 "$MODEL" random seed=1234 id=1 method=sample num_warmup=200 \
  num_samples=300 thin=1 checkpoint_freq=100 data file="$DATA" output refresh=0 file="$OUT"
if [ ! -f "$CHECKPOINT" ]; then
  echo "failed to capture checkpoint at iteration 500" >&2
  exit 1
fi
patch_checkpoint_num_samples "$CHECKPOINT" 800
cp "$CHECKPOINT" "$FIXTURES/checkpoint_at_500.json"
echo "Wrote $FIXTURES/checkpoint_at_500.json"
rm -f "$OUT" "$CHECKPOINT"

echo "=== dense_e test_model fixtures ==="
DENSE_DIR="$FIXTURES/dense"
mkdir -p "$DENSE_DIR"
MODEL=src/test/test-models/test_model
METRIC=src/test/test-models/test_model.dense_e_metric.json
OUT=test/checkpoint_dense_regen_output.csv
CHECKPOINT="${OUT%.csv}_checkpoint.json"
COMMON="random seed=5678 id=1 method=sample algorithm=hmc metric=dense_e"
COMMON="$COMMON metric_file=$METRIC num_warmup=200 num_samples=800 thin=1"

rm -f "$OUT" "$CHECKPOINT"
"$MODEL" $COMMON checkpoint_freq=0 output refresh=50 file="$OUT"
cp "$OUT" "$DENSE_DIR/golden_full.csv"
echo "Wrote $DENSE_DIR/golden_full.csv"

rm -f "$OUT" "$CHECKPOINT"
CMDSTAN_KEEP_CHECKPOINT=1 "$MODEL" random seed=5678 id=1 method=sample algorithm=hmc \
  metric=dense_e metric_file="$METRIC" num_warmup=200 num_samples=300 thin=1 \
  checkpoint_freq=100 output refresh=0 file="$OUT"
if [ ! -f "$CHECKPOINT" ]; then
  echo "failed to capture dense checkpoint at iteration 500" >&2
  exit 1
fi
patch_checkpoint_num_samples "$CHECKPOINT" 800
cp "$CHECKPOINT" "$DENSE_DIR/checkpoint_at_500.json"
echo "Wrote $DENSE_DIR/checkpoint_at_500.json"
rm -f "$OUT" "$CHECKPOINT"

echo "=== multi-chain eight_schools fixtures ==="
MULTI_DIR="$FIXTURES/multi_chain"
mkdir -p "$MULTI_DIR"
MODEL=src/test/test-models/eight_schools
DATA=src/test/test-models/eight_schools.data.json
OUT=test/checkpoint_multi_regen_output.csv
COMMON="random seed=9012 id=1 method=sample num_chains=2 num_warmup=200 num_samples=800 thin=1"

rm -f "$OUT" "${OUT%.csv}_checkpoint_1.json" "${OUT%.csv}_checkpoint_2.json"
"$MODEL" $COMMON checkpoint_freq=0 data file="$DATA" output refresh=50 file="$OUT"
cp "${OUT%.csv}_1.csv" "$MULTI_DIR/golden_chain_1.csv"
cp "${OUT%.csv}_2.csv" "$MULTI_DIR/golden_chain_2.csv"
echo "Wrote $MULTI_DIR/golden_chain_1.csv"
echo "Wrote $MULTI_DIR/golden_chain_2.csv"

rm -f "$OUT" "${OUT%.csv}_1.csv" "${OUT%.csv}_2.csv" \
  "${OUT%.csv}_checkpoint_1.json" "${OUT%.csv}_checkpoint_2.json"
CMDSTAN_KEEP_CHECKPOINT=1 "$MODEL" random seed=9012 id=1 method=sample num_chains=2 \
  num_warmup=200 num_samples=300 thin=1 checkpoint_freq=100 data file="$DATA" \
  output refresh=0 file="$OUT"
for chain in 1 2; do
  CHECKPOINT="${OUT%.csv}_checkpoint_${chain}.json"
  if [ ! -f "$CHECKPOINT" ]; then
    echo "failed to capture multi-chain checkpoint for chain $chain" >&2
    exit 1
  fi
  patch_checkpoint_num_samples "$CHECKPOINT" 800
  cp "$CHECKPOINT" "$MULTI_DIR/checkpoint_chain_${chain}_at_500.json"
  echo "Wrote $MULTI_DIR/checkpoint_chain_${chain}_at_500.json"
done
rm -f "$OUT" "${OUT%.csv}_1.csv" "${OUT%.csv}_2.csv" \
  "${OUT%.csv}_checkpoint_1.json" "${OUT%.csv}_checkpoint_2.json"
