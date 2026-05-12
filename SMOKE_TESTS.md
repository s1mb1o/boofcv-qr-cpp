# Smoke Tests

Run these before publishing a release or after changing public build/CLI
behavior.

## Build and Unit Tests

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target boofcv_qr qr_scan boofcv_qr_tests boofcv_qr_python -- -j
ctest --test-dir build --output-on-failure
```

Expected: all GoogleTest cases pass.

## Single-Image CLI

```bash
build/qr_scan tests/fixtures/qr/full_v1_L_M000.png
```

Expected: JSON is printed to stdout and the process exits with status 0.

## Python PyBoof-Compatible API

```bash
PYTHONPATH=build/python python3 tests/python/test_pyboof_compat.py
```

Expected: `FactoryFiducial(np.uint8).qrcode()` detects the fixture QR code and
exposes PyBoof-style detection fields, path-like image loading, color-array
rejection, and `scan_batch()` path scanning.

## Python Timing Smoke

```bash
PYTHONPATH=build/python python3 tools/python/profile_python.py \
  tests/fixtures/qr/full_v1_L_M000.png \
  --iters 1000 \
  --batch-size 32 \
  --threads 8
```

Expected: the script prints single-image and batch timing lines with non-zero
detection counts.

## BoofCV Regression Dataset

```bash
BOOFCV_QR_DATASET_ROOT=/path/to/boofcv-qrcodes/qrcodes \
  QR_SCAN_THREADS=8 \
  bash tools/cli/run_regression.sh
```

Expected: the script prints `PASS: no new regressions` and aggregate decode
rate remains at 74.40% against the locked baseline.

The regression driver also writes an image-level taxonomy to
`tests/regression/baseline_cpp/failure_taxonomy.json`. If a category drifts out
of band, the script prints the affected images plus their likely stage
(`no_decoder_candidate`, `iou_mismatch`, `decoder_failure:*`, or
`payload_mismatch`).

To generate the taxonomy without rerunning the full dataset:

```bash
PYTHONDONTWRITEBYTECODE=1 python3 tests/regression/failure_taxonomy.py \
  tests/regression/baseline_cpp/summary.json \
  --java-summary tests/regression/baseline_java/summary.json \
  --output /tmp/qr_failure_taxonomy.json \
  --limit 12
```
