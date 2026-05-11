# boofcv-qr-cpp

Unofficial C++17/OpenCV port of BoofCV's QR code detector and decoder.

This project ports the QR pipeline from BoofCV's
`boofcv.alg.fiducial.qrcode` package and the supporting binary, polygon,
homography, Reed-Solomon, and decoder code needed to run it without the Java
runtime. The goal is algorithmic parity with BoofCV's QR behavior while exposing
OpenCV-native C++ APIs for applications that need stage-level control.

## Status

- Core QR detector/decoder pipeline is implemented as a static C++ library.
- `qr_scan` CLI can scan one image or a directory of images.
- Unit coverage mirrors the ported Java components.
- Regression scoring compares against a locked BoofCV Java baseline.
- License is Apache-2.0, matching upstream BoofCV.

This is not an official BoofCV project. BoofCV and its original QR
implementation are maintained at <https://github.com/lessthanoptimal/BoofCV>.

## Requirements

- CMake 3.16+
- C++17 compiler
- OpenCV 4.5+ with `core`, `calib3d`, `imgproc`, and `imgcodecs`
- Python 3 for the regression scorer

GoogleTest is fetched by CMake when tests are enabled.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target boofcv_qr qr_scan boofcv_qr_tests -- -j
ctest --test-dir build --output-on-failure
```

Disable optional targets if needed:

```bash
cmake -S . -B build -DBOOFCV_QR_BUILD_TESTS=OFF -DBOOFCV_QR_BUILD_CLI=OFF
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

## Python Status

Native Python bindings are not included yet. The public C++ API is designed to
be pybind11-friendly, but this repository currently does not build a Python
extension module, wheel, or `import boofcv_qr` package.

Python code can use the project today by invoking the `qr_scan` CLI and reading
its JSON output:

```python
import json
import subprocess

result = subprocess.run(
    ["build/qr_scan", "image.png"],
    check=True,
    text=True,
    capture_output=True,
)
record = json.loads(result.stdout)
```

For in-process Python use, the next step is to add a pybind11 module that
accepts `numpy`/`cv2` images and returns Python-native result dictionaries.

## Regression Dataset

The full regression harness expects BoofCV's `qrcodes_v3` dataset to be
available locally. Extract it, then point the script at the `qrcodes`
subdirectory:

```bash
BOOFCV_QR_DATASET_ROOT=/path/to/boofcv-qrcodes/qrcodes \
  bash tools/cli/run_regression.sh
```

The dataset itself is not vendored in this repository.

## Project Name

Recommended GitHub repository name: `boofcv-qr-cpp`.

That name keeps the upstream lineage first, narrows the scope to QR code
detection/decoding, and makes the language/runtime clear. The library target
remains `boofcv_qr`, which is a valid CMake target and C++ namespace-style name.

## Attribution

This repository contains code and documentation derived from BoofCV. See
[NOTICE](NOTICE), [UPSTREAM_VERSION](UPSTREAM_VERSION), and the source-level
algorithm notes under `src/**.md`.
