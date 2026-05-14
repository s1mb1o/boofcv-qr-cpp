# boofcv-qr-cpp

[![CI](https://github.com/s1mb1o/boofcv-qr-cpp/actions/workflows/ci.yml/badge.svg)](https://github.com/s1mb1o/boofcv-qr-cpp/actions/workflows/ci.yml)
[![License: Apache-2.0](https://img.shields.io/badge/License-Apache--2.0-blue.svg)](LICENSE)
[![Python](https://img.shields.io/badge/Python-3.10%2B-blue.svg)](pyproject.toml)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](CMakeLists.txt)

Unofficial C++17/OpenCV port of BoofCV's QR code detector and decoder.

This project ports the QR pipeline from BoofCV's
`boofcv.alg.fiducial.qrcode` package and the supporting binary, polygon,
homography, Reed-Solomon, and decoder code needed to run it without the Java
runtime. The goal is algorithmic parity with BoofCV's QR behavior while exposing
OpenCV-native C++ APIs for applications that need stage-level control.

## Status

- Core QR detector/decoder pipeline is implemented as a static C++ library.
- `qr_scan` CLI can scan one image or a directory of images.
- `boofcv_qr` Python package exposes a PyBoof-compatible QR subset.
- Unit coverage mirrors the ported Java components.
- Regression scoring compares against a locked BoofCV Java baseline.
- License is Apache-2.0, matching upstream BoofCV.

This is not an official BoofCV project. BoofCV and its original QR
implementation are maintained at <https://github.com/lessthanoptimal/BoofCV>.

## Requirements

- CMake 3.16+
- C++17 compiler
- OpenCV 4.5+ with `core`, `calib3d`, `imgproc`, and `imgcodecs`
- Python 3.10+ with development headers for the Python extension
- NumPy for the Python compatibility test and package runtime

GoogleTest and pybind11 are fetched by CMake when tests/bindings are enabled
and pybind11 is not already installed.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target boofcv_qr qr_scan boofcv_qr_tests boofcv_qr_python -- -j
ctest --test-dir build --output-on-failure
```

Apple Silicon profiling presets are available when using CMake 3.21+:

```bash
cmake --preset apple-arm64-release
cmake --build --preset apple-arm64-release --target qr_scan boofcv_qr_python -- -j

cmake --preset apple-arm64-relwithdebinfo
cmake --build --preset apple-arm64-relwithdebinfo --target qr_scan -- -j
```

Disable optional targets if needed:

```bash
cmake -S . -B build \
  -DBOOFCV_QR_BUILD_TESTS=OFF \
  -DBOOFCV_QR_BUILD_CLI=OFF \
  -DBOOFCV_QR_BUILD_PYTHON=OFF
```

## CLI Usage

Scan one image and write JSON to stdout:

```bash
build/qr_scan image.png
```

Scan a directory tree and write per-image JSON plus `summary.json`:

```bash
build/qr_scan /path/to/images /path/to/output
```

Batch mode processes images in parallel. Pin the worker count for repeatable
benchmarks:

```bash
QR_SCAN_THREADS=8 build/qr_scan /path/to/images /path/to/output
```

Parallel batch scans cap OpenCV's internal worker count to 1 by default to
avoid multiplying `QR_SCAN_THREADS` by OpenCV's GCD/TBB workers. Override this
for experiments with `QR_SCAN_OPENCV_THREADS=N` or
`BOOFCV_QR_OPENCV_THREADS=N`.

Multi-worker batch scans also limit high-resolution image concurrency by
default: up to 64 megapixels may be processed at once, and worker pipelines
release large scratch buffers after images of at least 8 megapixels. Override
or disable these controls with
`QR_SCAN_MAX_IN_FLIGHT_MPIX=N` and `QR_SCAN_RESET_PIPELINE_MPIX=N` (`0`
disables each limit).

Profile one image with stage timing:

```bash
build/qr_scan --profile image.png 1000
```

Run a dataset scan with a sidecar timing report. Normal batch JSON remains
unchanged; timings are written to `stage_timings.json`.

```bash
QR_SCAN_THREADS=8 build/qr_scan --stage-timings /path/to/images /path/to/output
```

The same report can be enabled for the regression driver with
`QR_SCAN_STAGE_TIMINGS=1`.

## Python Usage

The Python package exposes a PyBoof-compatible QR subset under the
`boofcv_qr` import name. It is not a full `pyboof` replacement: the binding
currently supports BoofCV-style QR detection for GrayU8 / `numpy.uint8` images.

```python
import numpy as np
import boofcv_qr as pb

detector = pb.FactoryFiducial(np.uint8).qrcode()
image = pb.load_single_band("image.png", np.uint8)

detector.detect(image)
for qr in detector.detections:
    print(qr.message)
    print(qr.bounds.convert_tuple())
```

Batch scan image paths in parallel:

```python
batch = pb.BatchScanConfig()
batch.threads = 8
batch.max_in_flight_mpix = 64.0

results = pb.scan_batch(["frame001.png", "frame002.png"], batch)
for result in results:
    if not result.ok:
        continue
    for qr in result.detections:
        print(result.path, qr.message)
```

The legacy `scan_batch(paths, threads=8)` form remains supported.
`scan_batch()` also caps OpenCV internal threads to 1 when using multiple image
workers. Set `BatchScanConfig.opencv_threads` or
`BOOFCV_QR_OPENCV_THREADS=N` to override it. Python batch scans also expose the
CLI memory controls as `BatchScanConfig.max_in_flight_mpix` and
`BatchScanConfig.reset_pipeline_mpix`; leave them negative for the default
policy, set positive megapixel values to override, or set `0.0` to disable.

For detection-only pipelines, call the first exposed stage API:

```python
candidates = detector.detect_polygons_only(image)
for qr in candidates:
    print(qr.version, qr.bounds.as_list(), qr.position_patterns["corner"])
```

The main exposed QR types are:

| PyBoof-style type | Purpose |
|---|---|
| `FactoryFiducial(np.uint8).qrcode()` | Construct a GrayU8 QR detector |
| `ConfigQrCode` | QR decode options such as encoding and transposed-bit handling |
| `BatchScanConfig` | Typed path-batch options for worker count, QR config, OpenCV threads, and memory budget |
| `QrCodeDetector.detect(image)` | Detect QR codes in one `numpy.uint8` grayscale image |
| `QrCodeDetector.detect_polygons_only(image)` | Return finder/bounds/alignment candidates without decode |
| `scan_batch(paths, threads=0)` | Scan image paths in parallel using one pipeline per worker |
| `QrCode` | Message, geometry, QR metadata, raw codewords, and RS status |
| `Point2D` / `Polygon2D` | PyBoof-like geometry wrappers |

Install from the checkout with:

```bash
python3 -m pip install .
```

For CMake builds, the in-tree package is available at `build/python` after
building `boofcv_qr_python`:

```bash
PYTHONPATH=build/python python3 tests/python/test_pyboof_compat.py
```

See [docs/python_api.md](docs/python_api.md) and [examples/python/](examples/python/)
for more Python examples.

Packaging support and wheel policy are documented in
[docs/packaging.md](docs/packaging.md). In short: macOS release wheels are
built with `cibuildwheel` and repaired with `delocate`; Linux release wheels
are currently system-OpenCV artifacts, not manylinux wheels.

## Regression Dataset

The full regression harness expects BoofCV's `qrcodes_v3` dataset to be
available locally. Extract it, then point the script at the `qrcodes`
subdirectory:

```bash
BOOFCV_QR_DATASET_ROOT=/path/to/boofcv-qrcodes/qrcodes \
  bash tools/cli/run_regression.sh
```

The dataset itself is not vendored in this repository.

For reproducible local speed and memory reports against the C++ CLI and the
Java BoofCV reference:

```bash
BOOFCV_QR_DATASET_ROOT=/path/to/boofcv-qrcodes/qrcodes \
  QR_BENCH_CPP_THREADS=8 \
  tools/benchmark_compare.sh /tmp/boofcv_qr_benchmark
```

The report includes commands, host/tool versions, `/usr/bin/time -l` RSS and
memory footprint, C++ regression scores, and Java reference timing/RSS when a
usable JDK and cached Gradle dependencies are available. Set
`QR_BENCH_SKIP_JAVA=1` for C++-only runs.

## Project Name

Recommended GitHub repository name: `boofcv-qr-cpp`.

That name keeps the upstream lineage first, narrows the scope to QR code
detection/decoding, and makes the language/runtime clear. The library target
remains `boofcv_qr`, which is a valid CMake target and C++ namespace-style name.

## Attribution

This repository contains code and documentation derived from BoofCV. See
[NOTICE](NOTICE), [UPSTREAM_VERSION](UPSTREAM_VERSION), and the source-level
algorithm notes under `src/**.md`.
