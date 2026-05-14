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

Apple Silicon preset smoke:

```bash
cmake --preset apple-arm64-release
cmake --build --preset apple-arm64-release --target qr_scan boofcv_qr_python -- -j
```

Expected: configure pins `CMAKE_OSX_ARCHITECTURES=arm64` and both targets
build.

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
rejection, polygon-only detection, typed `BatchScanConfig`, result `as_dict()`
helpers, Python batch memory controls, and `scan_batch()` path scanning.

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

## Python Wheel Smoke

```bash
python3 -m pip wheel . --no-deps -w /tmp/boofcv_qr_dist_check
python3 -m venv /tmp/boofcv_qr_smoke
/tmp/boofcv_qr_smoke/bin/python -m pip install numpy /tmp/boofcv_qr_dist_check/*.whl
BOOFCV_QR_FIXTURE_DIR=tests/fixtures/qr \
  /tmp/boofcv_qr_smoke/bin/python tests/python/test_pyboof_compat.py
```

Expected: the wheel builds, installs into the clean virtual environment, and
the PyBoof compatibility smoke exits 0.

## CLI Stage Timing

```bash
build/qr_scan --profile tests/fixtures/qr/full_v1_L_M000.png 1000
```

Expected: output includes a `Stage timings` table and `Top bottlenecks` line.

For a full dataset timing report:

```bash
QR_SCAN_THREADS=8 build/qr_scan --stage-timings \
  /path/to/boofcv-qrcodes/qrcodes \
  /tmp/qr_stage_timing
```

Expected: `/tmp/qr_stage_timing/stage_timings.json` contains `overall`,
`by_category`, `by_size_bucket`, and the top two bottleneck stages. The regular
`summary.json` schema stays the same unless callers read the timing sidecar.

## BoofCV Regression Dataset

```bash
BOOFCV_QR_DATASET_ROOT=/path/to/boofcv-qrcodes/qrcodes \
  QR_SCAN_THREADS=8 \
  bash tools/cli/run_regression.sh
```

Expected: the script prints `PASS: no new regressions` and aggregate decode
rate remains at 74.40% against the locked baseline. Batch output should also
show `OpenCV threads=1, capped for image-parallel batch`, `max in-flight
64.0 MP`, and `release scratch >= 8.0 MP` unless the corresponding environment
override is set.

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

## Benchmark Reproducibility and Memory Report

```bash
BOOFCV_QR_DATASET_ROOT=/path/to/boofcv-qrcodes/qrcodes \
  QR_BENCH_CPP_THREADS=8 \
  tools/benchmark_compare.sh /tmp/boofcv_qr_benchmark
```

Expected: `report.md` and `report.json` are written with host/tool versions,
commands, C++ serial/batch RSS, macOS memory footprint, regression score, and
Java reference timing/RSS when Java is available. Use `QR_BENCH_SKIP_JAVA=1`
for a C++-only smoke.
