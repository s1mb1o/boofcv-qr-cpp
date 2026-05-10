#!/usr/bin/env bash
# Step 9b regression driver. Builds qr_scan in Release, runs it against
# the BoofCV qrcodes_v3 dataset, scores via tests/regression/score.py,
# and prints the per-category delta vs tests/baseline.json.
#
# Usage:  ./run_regression.sh
#         (no args; paths are pinned)
#
# CLAUDE.md mandates the parity gate is "per-category read rate within
# ~2% of the Java baseline". This script is the gate's invocation.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DATASET_ROOT="/Users/ashmelev/Projects/30_moonlighting/pricetag-vision-datasets/data/external/boofcv-qrcodes/qrcodes"
BUILD_DIR="${REPO_ROOT}/build"
CPP_OUTPUT_DIR="${REPO_ROOT}/tests/regression/baseline_cpp"
SCORE_OUT="${REPO_ROOT}/tests/regression/baseline_cpp/score.json"
BASELINE_JSON="${REPO_ROOT}/tests/baseline.json"
SCORE_PY="${REPO_ROOT}/tests/regression/score.py"

if [[ ! -d "${DATASET_ROOT}" ]]; then
    echo "Dataset not found: ${DATASET_ROOT}" >&2
    exit 2
fi

# 1. Build qr_scan in Release.
if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    echo ">>> Configuring CMake (Release)..."
    cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release > /tmp/qr_scan_cmake.log
fi
echo ">>> Building qr_scan..."
cmake --build "${BUILD_DIR}" --target qr_scan -- -j > /tmp/qr_scan_build.log

# 2. Clear prior C++ run + run qr_scan over the dataset.
rm -rf "${CPP_OUTPUT_DIR}"
mkdir -p "${CPP_OUTPUT_DIR}"
echo ">>> Running qr_scan against ${DATASET_ROOT}..."
"${BUILD_DIR}/qr_scan" "${DATASET_ROOT}" "${CPP_OUTPUT_DIR}"

# 3. Score the run.
echo ">>> Scoring against ground truth..."
python3 "${SCORE_PY}" "${CPP_OUTPUT_DIR}/summary.json" "${SCORE_OUT}" --iou 0.5

# 4. Diff per-category against the locked Java baseline.
echo ""
echo ">>> Per-category delta vs Java baseline (${BASELINE_JSON}):"
python3 - <<PY
import json
import sys
from pathlib import Path

baseline = json.loads(Path("${BASELINE_JSON}").read_text())
score = json.loads(Path("${SCORE_OUT}").read_text())

agg_b = baseline.get("aggregate", {})
agg_s = score.get("aggregate", {})

def fmt_pct(x):
    return f"{x*100:5.2f}%"

print(f"{'category':<16} {'baseline':>9} {'cpp':>9} {'delta_pp':>10} {'gt':>5} {'cpp_dec':>7}")
print("-" * 60)

cats_b = baseline.get("categories", {})
cats_s = score.get("categories", {})
out_of_band = []
for cat in sorted(cats_b):
    b = cats_b[cat].get("decode_rate", 0.0)
    s = cats_s.get(cat, {}).get("decode_rate", 0.0)
    delta_pp = (s - b) * 100.0
    gt_count = cats_b[cat].get("gt_count", 0)
    dec_cpp = cats_s.get(cat, {}).get("decode_success", 0)
    flag = ""
    if abs(delta_pp) > 2.0:
        flag = "  <<<"
        out_of_band.append((cat, delta_pp))
    print(f"{cat:<16} {fmt_pct(b):>9} {fmt_pct(s):>9} {delta_pp:+7.2f}pp {gt_count:>5} {dec_cpp:>7}{flag}")

print("-" * 60)
b_agg = agg_b.get("decode_rate", 0.0)
s_agg = agg_s.get("decode_rate", 0.0)
delta_agg = (s_agg - b_agg) * 100.0
print(f"{'AGGREGATE':<16} {fmt_pct(b_agg):>9} {fmt_pct(s_agg):>9} {delta_agg:+7.2f}pp")

print("")
if out_of_band:
    print(f"FAIL: {len(out_of_band)} categories outside +/-2pp band:")
    for cat, d in out_of_band:
        print(f"  {cat}: {d:+.2f}pp")
    sys.exit(1)
else:
    print("PASS: all categories within +/-2pp of Java baseline.")
PY
