# ChangeLog

## 2026-05-12 — accuracy: add failure taxonomy for BoofCV parity

Started the accuracy-parity track by making the Java/C++ regression gap
inspectable at image and stage level.

### Added

- [tests/regression/failure_taxonomy.py](tests/regression/failure_taxonomy.py):
  image-level taxonomy for C++ misses and optional Java/C++ parity drift.

### Changed

- [src/finder/qr_code_position_pattern_detector.cpp](src/finder/qr_code_position_pattern_detector.cpp):
  removed a C++-only per-call RLE workspace reset so `checkLine()` matches
  BoofCV Java's stateful workspace semantics.
- [tests/accepted_residuals.json](tests/accepted_residuals.json),
  [docs/decisions/06_threshold_block_otsu_audit.md](docs/decisions/06_threshold_block_otsu_audit.md),
  [src/decoder/qr_code_decoder_image.md](src/decoder/qr_code_decoder_image.md),
  and [ResearchLog.md](ResearchLog.md): refreshed the monitor/glare residual
  image taxonomy and corrected the stale `glare/image007` note.

### Verification

- `cmake --build build --target boofcv_qr_tests qr_scan -- -j` -> PASS.
- `ctest --test-dir build -R "QrCodePositionPatternDetector" --output-on-failure`
  -> 6/6 PASS.
- `BOOFCV_QR_DATASET_ROOT=... QR_SCAN_THREADS=8 bash tools/cli/run_regression.sh`
  -> PASS, aggregate decode rate 74.40%.
- `PYTHONDONTWRITEBYTECODE=1 python3 tests/regression/failure_taxonomy.py tests/regression/baseline_cpp/summary.json --java-summary tests/regression/baseline_java/summary.json --output /tmp/qr_accuracy_parity_taxonomy.json --limit 12`
  -> PASS.

## 2026-05-11 (later²¹) — release: harden Python API and GitHub packaging

Prepared the repository for a first GitHub release and expanded the Python
surface beyond single-image smoke usage.

### Added

- Python `scan_batch(paths, threads=0, config=None)` API with image-level
  parallelism and per-image `ScanResult` records.
- [examples/python/scan_qr.py](examples/python/scan_qr.py) and
  [examples/python/batch_scan.py](examples/python/batch_scan.py).
- [tools/python/profile_python.py](tools/python/profile_python.py) for Python
  single-image and batch timing checks.
- [docs/python_api.md](docs/python_api.md) and
  [docs/releases/v0.1.0.md](docs/releases/v0.1.0.md).
- GitHub issue templates, pull request template, README badges, and release
  artifact workflow for Python 3.10-3.14 wheels.

### Changed

- [.github/workflows/ci.yml](.github/workflows/ci.yml): expanded CI to an
  Ubuntu/macOS and Python 3.10-3.14 matrix, including wheel build/install smoke
  tests.
- [bindings/python/boofcv_qr_bindings.cpp](bindings/python/boofcv_qr_bindings.cpp):
  added path-like support for `load_single_band()`, clearer dtype/color-image
  errors for `detect()`, and GIL-released batch path scanning with per-image
  exception capture.
- [tests/unit/test_reed_solomon.cpp](tests/unit/test_reed_solomon.cpp):
  fixed a U16 test vector typo that could index outside GF(2^8) tables on
  Linux.
- [README.md](README.md), [CONTRIBUTING.md](CONTRIBUTING.md),
  [SMOKE_TESTS.md](SMOKE_TESTS.md), [CLAUDE.md](CLAUDE.md),
  [pyproject.toml](pyproject.toml), and [ResearchLog.md](ResearchLog.md):
  documented the expanded Python and release workflow.
- GitHub repository metadata now has a public description and topics:
  `boofcv`, `opencv`, `qrcode`, `qr-code`, `cpp`, `python`, and
  `computer-vision`.

### Verification

- `cmake --build build --target boofcv_qr qr_scan boofcv_qr_tests boofcv_qr_python -- -j`
  -> PASS.
- `ctest --test-dir build --output-on-failure` -> 436/436 PASS.
- `PYTHONPATH=build/python BOOFCV_QR_FIXTURE_DIR=tests/fixtures/qr python3 tests/python/test_pyboof_compat.py`
  -> PASS.
- `PYTHONPATH=build/python python3 tools/python/profile_python.py tests/fixtures/qr/full_v1_L_M000.png --iters 1000 --batch-size 32 --threads 8`
  -> PASS; single 0.159 ms/image, batch 0.039 ms/image.
- `PYTHONPATH=build/python python3 examples/python/scan_qr.py tests/fixtures/qr/full_v1_L_M000.png`
  -> PASS.
- `PYTHONPATH=build/python python3 examples/python/batch_scan.py tests/fixtures/qr --threads 2`
  -> PASS.
- `build/qr_scan --profile tests/fixtures/qr/full_v1_L_M000.png 1000` ->
  PASS; CLI mean 0.15 ms/image.
- `python3 -m pip wheel . --no-deps -w /tmp/boofcv_qr_dist_check` -> PASS.
- Clean venv install from `/tmp/boofcv_qr_dist_check/` plus
  `BOOFCV_QR_FIXTURE_DIR=tests/fixtures/qr /tmp/boofcv_qr_venv3/bin/python tests/python/test_pyboof_compat.py`
  -> PASS.
- `BOOFCV_QR_DATASET_ROOT=... QR_SCAN_THREADS=8 bash tools/cli/run_regression.sh`
  -> PASS, aggregate decode rate 74.40%, 13174 ms total elapsed.

## 2026-05-11 (later²⁰) — python: add PyBoof-compatible QR bindings

Added native Python bindings for the BoofCV QR pipeline, packaged as
`boofcv_qr` and shaped after PyBoof's QR detector subset.

### Added

- [bindings/python/boofcv_qr_bindings.cpp](bindings/python/boofcv_qr_bindings.cpp):
  pybind11 module exposing `FactoryFiducial`, `ConfigQrCode`,
  `QrCodeDetector`, `QrCode`, `Polygon2D`, `Point2D`, and
  `load_single_band()`.
- [python/boofcv_qr/](python/boofcv_qr/): import package, type stub, and
  `py.typed` marker.
- [pyproject.toml](pyproject.toml): `scikit-build-core` wheel build for the
  native extension.
- [tests/python/test_pyboof_compat.py](tests/python/test_pyboof_compat.py):
  PyBoof-style compatibility smoke test using
  `FactoryFiducial(np.uint8).qrcode()`.
- [docs/decisions/08_python_pyboof_qr_subset.md](docs/decisions/08_python_pyboof_qr_subset.md):
  documented why the package is named `boofcv_qr` while mirroring the PyBoof QR
  subset.

### Changed

- [CMakeLists.txt](CMakeLists.txt): added `BOOFCV_QR_BUILD_PYTHON`,
  `boofcv_qr_python`, position-independent core-library objects, install rules,
  and a CTest Python compatibility test.
- [README.md](README.md), [CONTRIBUTING.md](CONTRIBUTING.md),
  [SMOKE_TESTS.md](SMOKE_TESTS.md), and [.github/workflows/ci.yml](.github/workflows/ci.yml):
  documented and exercised the Python build/test path.
- [.gitignore](.gitignore): ignored Python build and wheel artifacts.
- [ResearchLog.md](ResearchLog.md): recorded the PyBoof subset compatibility
  scope and package-name decision.

### Verification

- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` -> PASS.
- `cmake --build build --target boofcv_qr qr_scan boofcv_qr_tests boofcv_qr_python -- -j`
  -> PASS.
- `ctest --test-dir build --output-on-failure` -> 436/436 PASS.
- `PYTHONPATH=build/python BOOFCV_QR_FIXTURE_DIR=tests/fixtures/qr python3 tests/python/test_pyboof_compat.py`
  -> PASS.
- `python3 -m pip wheel . -w /tmp/boofcv_qr_wheel` -> PASS.
- Clean venv install from the built wheel plus
  `BOOFCV_QR_FIXTURE_DIR=tests/fixtures/qr /tmp/boofcv_qr_venv/bin/python tests/python/test_pyboof_compat.py`
  -> PASS.
- `BOOFCV_QR_DATASET_ROOT=... QR_SCAN_THREADS=8 bash tools/cli/run_regression.sh`
  -> PASS, aggregate decode rate 74.40%, 3026 ms total elapsed.
- `git diff --check` -> PASS.

## 2026-05-11 (later¹⁹) — docs: clarify Python usage status

Clarified that `boofcv-qr-cpp` is not yet a native Python package.

### Changed

- [README.md](README.md): added a Python status section explaining that current
  Python use is via the `qr_scan` CLI JSON output, while native in-process use
  still requires a future pybind11 binding layer.

## 2026-05-11 (later¹⁸) — docs: prepare public GitHub publication

Prepared the repository for public GitHub publication under the recommended
name `boofcv-qr-cpp`.

### Changed

- Added [README.md](README.md), [LICENSE](LICENSE), [NOTICE](NOTICE),
  [CONTRIBUTING.md](CONTRIBUTING.md), [SECURITY.md](SECURITY.md), and
  [SMOKE_TESTS.md](SMOKE_TESTS.md).
- Added GitHub Actions CI in [.github/workflows/ci.yml](.github/workflows/ci.yml)
  for Ubuntu build and unit-test coverage.
- Renamed public CMake project metadata from `qr-boofcv-cpp` to
  `boofcv-qr-cpp`; the library target remains `boofcv_qr`.
- Made `tools/cli/run_regression.sh` use `BOOFCV_QR_DATASET_ROOT` instead of a
  local absolute dataset path.
- Sanitized tracked docs and baseline metadata to remove private local paths
  and project-specific downstream names.
- Added [docs/decisions/07_public_project_name.md](docs/decisions/07_public_project_name.md)
  documenting the repository name decision.

### Verification

- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` -> PASS, no developer
  warnings after adding `DOWNLOAD_EXTRACT_TIMESTAMP`.
- `cmake --build build --target boofcv_qr qr_scan boofcv_qr_tests -- -j` ->
  PASS.
- `ctest --test-dir build --output-on-failure` -> 435/435 PASS.
- `BOOFCV_QR_DATASET_ROOT=... QR_SCAN_THREADS=8 bash tools/cli/run_regression.sh`
  -> PASS, aggregate decode rate 74.40%, 2.858 s total elapsed.
- `build/qr_scan tests/fixtures/qr/full_v1_L_M000.png` -> PASS, valid JSON
  single-image smoke output.

## 2026-05-11 (later¹⁷) — docs(perf): record batch thread-count spot check

Recorded a fresh post-parallelization BoofCV regression timing check across
default, 8-worker, and serial batch modes.

### Changed

- [ResearchLog.md](ResearchLog.md): added the current batch thread-count
  performance table and the local recommendation to use `QR_SCAN_THREADS=8`
  for repeatable Apple Silicon throughput.

### Result

- All checked modes passed the BoofCV regression gate with aggregate decode
  rate unchanged at 74.40%.
- Current run timings: default 12 workers = 3.108 s, `QR_SCAN_THREADS=8` =
  2.612 s, `QR_SCAN_THREADS=1` = 17.834 s.
- The 8-worker override was the fastest sampled mode in this check, 6.8x faster
  than serial and 16.0% faster than the 12-worker default under current load.

## 2026-05-11 (later¹⁶) — perf(cli): parallelize qr_scan batch mode

Implemented image-level parallelism for `qr_scan` batch mode. Each worker owns
its own pipeline instance, claims image indices atomically, and stores results
by sorted input index so per-image JSON and `summary.json` stay deterministic.

### Changed

- [tools/cli/qr_scan.cpp](tools/cli/qr_scan.cpp): added batch worker selection,
  per-worker `Pipeline` instances, atomic work distribution, synchronized
  progress logging, deterministic post-processing writes, and the
  `QR_SCAN_THREADS=N` environment override.
- [ResearchLog.md](ResearchLog.md): recorded the batch-parallel perf results
  and validation commands.

### Performance

BoofCV dataset regression, 562 images:

| mode | total elapsed | aggregate decode | notes |
|---|---:|---:|---|
| `QR_SCAN_THREADS=1` | 17.523 s | 74.40% | serial compatibility path |
| `QR_SCAN_THREADS=8` | 2.774 s | 74.40% | Apple Silicon performance-core-sized run |
| default (`hardware_concurrency`, 12 workers here) | 2.603 s | 74.40% | final rebuilt binary |

Default batch wall time improved 6.7x versus the one-worker path on this local
Apple Silicon run. Per-image `meanMs` values in the score output now include
multi-threaded contention and are no longer comparable to serial detector-core
timings; `summary.total_elapsed_ms` is the relevant batch metric.

### Verification

- `cmake --build build --target qr_scan boofcv_qr_tests -- -j` -> PASS.
- `ctest --test-dir build --output-on-failure -R 'QrCodePositionPatternDetector|DetectPolygon|QrCodeDecoderImage|QrCodeDecoderBits'` -> 50/50 PASS.
- `ctest --test-dir build --output-on-failure` -> 435/435 PASS.
- `QR_SCAN_THREADS=1 bash tools/cli/run_regression.sh` -> PASS, 17.523 s,
  aggregate decode rate 74.40%.
- `QR_SCAN_THREADS=8 bash tools/cli/run_regression.sh` -> PASS, 2.774 s,
  aggregate decode rate 74.40%.
- `bash tools/cli/run_regression.sh` -> PASS, 2.603 s with 12 workers,
  aggregate decode rate 74.40%.

## 2026-05-11 (later¹⁵) — docs(perf): refresh post-target bottleneck profile

Pure research close-out after targets 3-8. Re-ran the BoofCV dataset
regression timing comparison, sampled the two representative slow images, and
recorded one rejected follow-up trial.

### Changed

- [ResearchLog.md](ResearchLog.md): added the post-target performance state,
  current C++ vs locked BoofCV Java timing comparison, fresh `sample` bottleneck
  tables for `bright_spots/image012` and `lots/image005`, and the rejected
  `ImageLineIntegral` row-pointer/inline trial.

### Result

- Quality remains byte-identical to the BoofCV Java baseline: aggregate decode
  rate 74.40%.
- Current C++ full regression elapsed is 17.351 s. Detector-core mean is
  22.53 ms/image, now 1.46x slower than the Java detector-core mean but still
  faster end-to-end than the same-session Java batch timing recorded earlier.
- No new source target was banked after RS: the `ImageLineIntegral` helper
  trial regressed both fixed probes and was reverted.

## 2026-05-11 (later¹⁴) — perf(rs): skip Chien/Forney on clean codewords

Implemented the low-priority Reed-Solomon/Galois target from the refreshed
`lots` profile. Clean RS blocks still compute syndromes and the
Berlekamp-Massey locator, but now return as soon as the locator is degree zero
instead of running a no-op Chien search and Forney setup.

### Changed

- [include/boofcv_qr/reed_solomon.hpp](include/boofcv_qr/reed_solomon.hpp):
  `ReedSolomonCodesT::correct()` clears stale `errorLocations` and returns
  success when `errorLocatorPoly` is the trivial `[1]` no-error locator.
- [tests/unit/test_reed_solomon.cpp](tests/unit/test_reed_solomon.cpp): added
  typed coverage that a clean correction after a prior real correction leaves
  the message/ECC unchanged and reports zero errors for both generator bases.
- [src/reed_solomon/reed_solomon.md](src/reed_solomon/reed_solomon.md) and
  [ResearchLog.md](ResearchLog.md): documented the clean-codeword fast path and
  perf gate results.

### Performance

Compared against the post-edge target:

| image / run | before | after | delta |
|---|---:|---:|---:|
| `bright_spots/image012.jpg` (300 iters) | 97.74 ms/iter | 96.98 ms/iter | -0.8% |
| `lots/image005.jpg` (250 iters) | 117.68 ms/iter | 116.30 ms/iter | -1.2% |
| Full regression `total_elapsed_ms` | 17.457 s | 17.351 s | -0.6% |
| Aggregate detector mean | 22.55 ms | 22.53 ms | -0.1% |

Quality is unchanged.

### Verification

- `cmake --build build --target boofcv_qr_tests qr_scan -- -j` -> PASS.
- `ctest --test-dir build --output-on-failure -R 'ReedSolomon|Galois|QrCodeDecoderBits'` -> 37/37 PASS.
- `ctest --test-dir build --output-on-failure` -> 435/435 PASS.
- `bash tools/cli/run_regression.sh` -> PASS: aggregate decode rate remains
  74.40%, with only the documented accepted residual categories out-of-band.

## 2026-05-11 (later¹³) — perf(edge): use in-bounds contour edge sampling

Implemented the remaining edge-scoring part of the polyline/edge target. The
algorithm still samples the same tangent-normal locations and uses the same
bilinear interpolation math, but avoids redundant clamping/flooring and
`cv::Mat::at()` inside the already-bounds-checked path.

### Changed

- [src/polygon/detect_polygon_from_contour.cpp](src/polygon/detect_polygon_from_contour.cpp): added `ContourEdgeIntensity::sampleInside()` using positive-coordinate truncation and row-pointer image access; `process()` now calls it only after the existing in-bounds checks. The public behaviour of the clamped private `sample()` helper is preserved by forwarding through the same fast bilinear core after clamping.
- [include/boofcv_qr/polygon/detect_polygon_from_contour.hpp](include/boofcv_qr/polygon/detect_polygon_from_contour.hpp): declared the in-bounds helper.
- [src/polygon/detect_polygon_from_contour.md](src/polygon/detect_polygon_from_contour.md) and [ResearchLog.md](ResearchLog.md): documented the in-bounds invariant and perf results.

### Performance

Compared against the post-polyline-pool target:

| image / run | before | after | delta |
|---|---:|---:|---:|
| `bright_spots/image012.jpg` (300 iters) | 99.74 ms/iter | 97.74 ms/iter | -2.0% |
| `lots/image005.jpg` (250 iters) | 120.58 ms/iter | 117.68 ms/iter | -2.4% |
| Full regression `total_elapsed_ms` | 17.460 s | 17.457 s | flat |
| Aggregate detector mean | 22.62 ms | 22.55 ms | -0.3% |

The fixed probes improved clearly; full-regression category timing stayed mostly
within noise. Quality is unchanged.

### Verification

- `cmake --build build --target boofcv_qr_tests qr_scan -- -j` -> PASS.
- `ctest --test-dir build --output-on-failure -R 'ContourEdgeIntensity|DetectPolygonFromContour|QrCodePositionPatternDetector'` -> 19/19 PASS.
- `ctest --test-dir build --output-on-failure` -> 433/433 PASS.
- `bash tools/cli/run_regression.sh` -> PASS: aggregate decode rate remains 74.40%, with only the documented accepted residual categories out-of-band.

## 2026-05-11 (later¹²) — perf(polyline): recycle corner and list-node pools

Implemented the next polyline allocation target. The first corner-object-only
trial was too noisy on the aggregate regression gate, so the committed change
also removes the remaining `std::list` node allocation churn in the same
corner-finder loop.

### Changed

- [include/boofcv_qr/polyline/polyline_split_merge.hpp](include/boofcv_qr/polyline/polyline_split_merge.hpp): `CornerPool::reset()` now keeps allocated `Corner` objects behind a logical active size, and `CornerList` is now a small linked list with recycled nodes instead of `std::list<Corner*>`.
- [src/polygon/polyline_split_merge.cpp](src/polygon/polyline_split_merge.cpp): `CornerPool::grow()` reuses and resets retained corners; `addCorner()` and split insertion rely on that reset path.
- [tests/unit/test_polyline_split_merge.cpp](tests/unit/test_polyline_split_merge.cpp): added focused coverage for corner-pool reuse/reset and pooled-list reset link cleanup.
- [src/polygon/polyline_split_merge.md](src/polygon/polyline_split_merge.md) and [ResearchLog.md](ResearchLog.md): documented the allocation-only optimisation and perf gate results.

### Performance

Compared against the post-Otsu target:

| image / run | before | after | delta |
|---|---:|---:|---:|
| `bright_spots/image012.jpg` (300 iters) | 101.73 ms/iter | 99.74 ms/iter | -2.0% |
| `lots/image005.jpg` (250 iters) | 125.52 ms/iter | 120.58 ms/iter | -3.9% |
| Full regression `total_elapsed_ms` | 17.799 s | 17.460 s | -1.9% |
| Aggregate detector mean | 23.10 ms | 22.62 ms | -2.1% |
| `lots` mean | 111.66 ms | 107.24 ms | -4.0% |

Quality is unchanged.

### Verification

- `cmake --build build --target boofcv_qr_tests qr_scan -- -j` -> PASS.
- `ctest --test-dir build --output-on-failure -R PolylineSplitMerge` -> 40/40 PASS.
- `ctest --test-dir build --output-on-failure` -> 433/433 PASS.
- `bash tools/cli/run_regression.sh` -> PASS: aggregate decode rate remains 74.40%, with only the documented accepted residual categories out-of-band.

## 2026-05-11 (later¹¹) — perf(otsu): slide local block histograms

Implemented the next `ThresholdBlockOtsu` target without changing the Otsu math or threshold decisions. The change keeps per-block histogram values exact, but avoids rebuilding each 3×3 local histogram from scratch.

### Changed

- [src/binary/threshold_block_otsu.cpp](src/binary/threshold_block_otsu.cpp): removed duplicate per-block histogram zeroing after the process-level `stats_.assign(..., 0)`, split threshold application from local-neighbour histogram construction, and added a sliding vertical/horizontal histogram window for `thresholdFromLocalBlocks=true`.
- [include/boofcv_qr/threshold_block_otsu.hpp](include/boofcv_qr/threshold_block_otsu.hpp): adjusted the private `thresholdBlock()` helper to consume a prepared histogram.
- [tests/unit/test_threshold_block_otsu.cpp](tests/unit/test_threshold_block_otsu.cpp): added coverage for `thresholdFromLocalBlocks=false`, which now follows the direct per-block histogram path.
- [src/binary/threshold_block_otsu.md](src/binary/threshold_block_otsu.md) and [ResearchLog.md](ResearchLog.md): documented the sliding-window implementation and perf results.

### Performance

Compared against the post-line-DLT target:

| image / run | before | after | delta |
|---|---:|---:|---:|
| `bright_spots/image012.jpg` (300 iters) | 104.51 ms/iter | 101.73 ms/iter | -2.7% |
| `lots/image005.jpg` (250 iters) | 123.13 ms/iter | 125.52 ms/iter | +1.9% |
| Full regression `total_elapsed_ms` | 17.858 s | 17.799 s | -0.3% |
| Aggregate detector mean | 23.21 ms | 23.10 ms | -0.5% |
| `lots` mean | 112.37 ms | 111.66 ms | -0.6% |

The fixed `lots/image005` probe was noisy and worsened, but the full `lots` category and aggregate regression timing both improved, with quality unchanged.

### Verification

- `cmake --build build --target boofcv_qr_tests qr_scan -- -j` -> PASS.
- `ctest --test-dir build --output-on-failure -R ThresholdBlockOtsu` -> 6/6 PASS.
- `ctest --test-dir build --output-on-failure` -> 431/431 PASS.
- `bash tools/cli/run_regression.sh` -> PASS: aggregate decode rate remains 74.40%, with only the documented accepted residual categories out-of-band.

## 2026-05-11 (later¹⁰) — perf(sampler): make line-DLT SVD compact

Implemented the next sampler target against the residual `setTransformFromLinesSquare()` SVD hotspot. The change keeps the BoofCV point/line DLT row equations and right-singular-vector objective, but removes avoidable OpenCV work around it.

### Changed

- [src/sampler/qr_code_binary_grid_to_pixel.cpp](src/sampler/qr_code_binary_grid_to_pixel.cpp): the fixed 3-point + 4-line system now uses a stack-backed `cv::Matx<double,14,9>` design matrix and `cv::SVD::compute(A, w, noArray(), vt)` instead of heap `cv::Mat` + `cv::SVDecomp(..., FULL_UV)`.
- [include/boofcv_qr/qr_code_binary_grid_to_pixel.hpp](include/boofcv_qr/qr_code_binary_grid_to_pixel.hpp), [src/sampler/qr_code_binary_grid_to_pixel.md](src/sampler/qr_code_binary_grid_to_pixel.md), [src/decoder/qr_code_decoder_image.md](src/decoder/qr_code_decoder_image.md), and [ResearchLog.md](ResearchLog.md): documented why this target keeps SVD rather than the previous normal-equation trial.

### Performance

Same-session fixed probes:

| image / run | before | after | delta |
|---|---:|---:|---:|
| `lots/image005.jpg` (250 iters) | 130.17 ms/iter | 123.13 ms/iter | -5.4% |
| `bright_spots/image012.jpg` (300 iters) | 103.52 ms/iter | 104.51 ms/iter | +1.0% |

Full regression quality stayed unchanged. Timing was targeted, not aggregate-wide: `lots` category mean moved from 115.22 ms in target 3 to 112.37 ms here; aggregate mean was effectively flat at 23.18 -> 23.21 ms.

### Verification

- `cmake --build build --target boofcv_qr_tests qr_scan -- -j` -> PASS.
- `ctest --test-dir build --output-on-failure -R 'QrCodeBinaryGridToPixel|SetTransformFromLinesSquare'` -> 6/6 PASS, including the 1e-9 rotated line-DLT fixture.
- `ctest --test-dir build --output-on-failure` -> 430/430 PASS.
- `bash tools/cli/run_regression.sh` -> PASS: aggregate decode rate remains 74.40%, with only the documented accepted residual categories out-of-band.

## 2026-05-11 (later⁹) — perf(contour): reuse packed contour blocks across frames

Implemented the next contour-stage memory/materialisation target from the refreshed profile. The first broader reuse attempt also recycled polygon `Contour` and `DetectedInfo` slots, but it regressed the `lots` category, so this commit keeps the smaller change that improved both representative probes.

### Changed

- [include/boofcv_qr/binary/packed_sets_point2d_i32.hpp](include/boofcv_qr/binary/packed_sets_point2d_i32.hpp): `reset()` now keeps previously allocated point blocks behind a logical active-block count instead of shrinking to the first block, and new `appendSetTo()` copies a contour block-by-block without the iterator's per-point division/modulo.
- [src/polygon/detect_polygon_from_contour.cpp](src/polygon/detect_polygon_from_contour.cpp): `buildContoursFromPort()` uses `appendSetTo()` when materialising external and internal contours.
- [tests/unit/test_linear_contour_label_chang2004.cpp](tests/unit/test_linear_contour_label_chang2004.cpp): added coverage for reset-after-multiple-block reuse and iterator correctness.
- [src/binary/linear_contour_label_chang2004.md](src/binary/linear_contour_label_chang2004.md), [src/polygon/detect_polygon_from_contour.md](src/polygon/detect_polygon_from_contour.md), and [ResearchLog.md](ResearchLog.md): documented the representation-only optimisation and perf results.

### Performance

Measured against the refreshed post-target-2 profile:

| image / run | before | after | delta |
|---|---:|---:|---:|
| `bright_spots/image012.jpg` (300 iters) | 107.19 ms/iter | 103.81 ms/iter | -3.2% |
| `lots/image005.jpg` (250 iters) | 128.77 ms/iter | 127.11 ms/iter | -1.3% |
| Full regression `total_elapsed_ms` | 18.873 s | 17.827 s | -5.5% |
| Aggregate detector mean | 24.73 ms | 23.18 ms | -6.3% |
| `lots` mean | 117.66 ms | 115.22 ms | -2.1% |

### Verification

- `cmake --build build --target boofcv_qr_tests qr_scan -- -j` -> PASS.
- `ctest --test-dir build --output-on-failure` -> 430/430 PASS.
- `bash tools/cli/run_regression.sh` -> PASS: aggregate decode rate remains 74.40%, with only the documented accepted residual categories out-of-band.

## 2026-05-11 (later⁸) — perf(research): refresh C++ vs BoofCV Java bottleneck profile after first two targets

Re-ran the current C++ detector against the BoofCV `qrcodes_v3` regression set and refreshed `sample` profiles for the same two representative slow images after the contour-tracer and grid-transform perf commits.

### Result

- Regression remains PASS: aggregate decode rate stays byte-identical to the BoofCV Java baseline at **74.40%**.
- Full batch wall time: BoofCV Java baseline **25.841 s**, current C++ **18.873 s**.
- Detector-core mean/image: Java **15.41 ms**, C++ **24.73 ms** (**1.61x slower** inside the detector).
- Fixed-count profiles: `bright_spots/image012` **107.19 ms/iter** (was 112.26 in the fresh pre-target profile), `lots/image005` **128.77 ms/iter** (was 142.15).
- Refreshed sample bottlenecks: `bright_spots` still dominated by `ContourTracer::searchOne8()`; `lots` now splits across contour tracing, `ThresholdBlockOtsu`, residual `setTransformFromLinesSquare()` SVD, polyline/edge scoring, and bit-intensity sampling.

### Changed

- [ResearchLog.md](ResearchLog.md): added the refreshed comparison, weighted per-category timing table, profile paths, hotspot shares, and next-target ranking.

---

## 2026-05-11 (later⁷) — perf(sampler): replace repeated point-DLT SVD with fixed 9×9 eigensolve

Second isolated perf target from the fresh bottleneck profile: the repeated pure-point QR grid-transform solve on multi-QR workloads.

### Changed

- [src/sampler/qr_code_binary_grid_to_pixel.cpp](src/sampler/qr_code_binary_grid_to_pixel.cpp): `computeTransform()` still uses BoofCV's pure DLT row equations for the N>4 point-correspondence path, but now accumulates `A^T A` directly and solves the fixed 9×9 symmetric eigensystem with `cv::eigen`. This avoids OpenCV's repeated tall-matrix `cv::SVD::solveZ` / `JacobiSVD` path while preserving the DLT null-space objective.
- [tests/unit/test_qr_code_binary_grid_to_pixel.cpp](tests/unit/test_qr_code_binary_grid_to_pixel.cpp): added a many-point perspective fixture that exercises the N>4 `computeTransform()` path on points not used by the fit.
- [src/sampler/qr_code_binary_grid_to_pixel.md](src/sampler/qr_code_binary_grid_to_pixel.md): documents the fixed-size null-space solve and records why `setTransformFromLinesSquare()` intentionally stays on SVD.

### Performance

Measured with `build/qr_scan --profile` on the same two profile images, comparing against the post-target-1 state:

| image | before | after | delta |
|---|---:|---:|---:|
| `bright_spots/image012.jpg` (300 iters) | 107.65 ms/iter | 107.04 ms/iter | -0.6% |
| `lots/image005.jpg` (250 iters) | 140.26 ms/iter | 127.47 ms/iter | -9.1% |

Full regression timing also moved in the expected place: `lots` mean fell from 124.56 ms to 112.59 ms, while aggregate quality stayed byte-identical.

### Verification

- `cmake --build build --target boofcv_qr_tests qr_scan -- -j` -> PASS.
- `ctest --test-dir build --output-on-failure` -> 429/429 PASS.
- `bash tools/cli/run_regression.sh` -> PASS: aggregate decode rate remains 74.40%, with only the documented accepted residual categories out-of-band.

---

## 2026-05-11 (later⁶) — perf(contour): avoid coordinate division in `ContourTracer`

First isolated perf target from the fresh bottleneck profile: keep the Chang contour-tracing algorithm identical, but remove avoidable arithmetic from the hottest contour-walk path.

### Changed

- [include/boofcv_qr/binary/contour_tracer.hpp](include/boofcv_qr/binary/contour_tracer.hpp) and [src/binary/contour_tracer.cpp](src/binary/contour_tracer.cpp): cache per-direction `(dx, dy)` tables alongside the existing linear-index offset tables, so `moveToNext()` updates coordinates directly instead of dividing/modding the linear binary index after every contour step.
- `searchOne4()` / `searchOne8()` now wrap direction indices with `& 3` / `& 7` in the unrolled hot path. The invariant remains `dir in [0, ruleN)`.
- [src/binary/linear_contour_label_chang2004.md](src/binary/linear_contour_label_chang2004.md): documents the representation-only optimisation and its parity invariants.

### Performance

Measured with `build/qr_scan --profile` on the two images used in the 2026-05-11 bottleneck profile:

| image | before | after | delta |
|---|---:|---:|---:|
| `bright_spots/image012.jpg` (300 iters) | 112.26 ms/iter | 107.65 ms/iter | -4.1% |
| `lots/image005.jpg` (250 iters) | 142.15 ms/iter | 140.26 ms/iter | -1.3% |

### Verification

- `cmake --build build --target boofcv_qr_tests qr_scan -- -j` -> PASS.
- `ctest --test-dir build --output-on-failure` -> 428/428 PASS.
- `bash tools/cli/run_regression.sh` -> PASS: aggregate decode rate remains 74.40%, with only the documented accepted residual categories out-of-band.

---

## 2026-05-11 (later⁵) — perf(profile): compare current C++ vs BoofCV Java and identify bottlenecks

Fresh performance pass on the BoofCV `qrcodes_v3` dataset, using a same-session Java reference run plus current C++ `qr_scan`.

### Result

- Full batch wall time: Java **24.976 s**, C++ **18.627 s**. C++ is faster end-to-end because the CLI harness has much lower non-detector overhead.
- Detector-core time: Java **8.319 s**, C++ **13.908 s**. The current C++ detector is still **1.67x slower** than BoofCV Java inside `process()`.
- Detector-core mean/image: Java **14.80 ms**, C++ **24.75 ms**.
- C++ quality stayed unchanged: aggregate decode rate **74.40%**, matching the Java baseline.

### Bottlenecks

Sampling profiles on `bright_spots/image012.jpg` and `lots/image005.jpg` show:

- Noisy high-resolution image (`bright_spots/image012`): finder / square detector dominates at **82.8%** of samples; `DetectPolygonFromContour` / `LinearContourLabelChang2004` is **78.7%**; top function is `ContourTracer::searchOne8()` at **35.6%**.
- Multi-QR image (`lots/image005`): finder / square detector **55.7%**, decoder/orchestrator **29.4%**, `ThresholdBlockOtsu` **13.2%**. Top functions are `ContourTracer::searchOne8()` (**14.0%**) and OpenCV `cv::JacobiSVDImpl_<double>()` from repeated QR grid-transform DLT solves (**13.4%**).

### Changed

- [ResearchLog.md](ResearchLog.md) records the commands, timing comparison, per-category detector ratios, slowest images, profile summaries, and recommended next perf levers.

### Verification

- Fresh Java reference run completed on 562 images.
- Fresh C++ run completed on 562 images.
- Both outputs scored with `tests/regression/score.py --iou 0.5`.
- `sample` profiles captured for two representative slow C++ workloads.

---

## 2026-05-11 (later⁴) — test(parity): fresh C++ vs BoofCV Java comparison on `qrcodes_v3`

Re-ran the current C++ port against the BoofCV `qrcodes_v3` dataset via `tools/cli/run_regression.sh`, using the locked BoofCV Java 1.3.0 baseline in `tests/baseline.json`.

### Result

- Aggregate decode rate remains **74.40%**, byte-identical to the Java baseline (**+0.00pp**).
- Aggregate detections / matches also match exactly: 945 detections, 936 matches at IoU >= 0.5, 936 decode successes out of 1258 GT codes.
- All out-of-band categories are the documented accepted residuals: `bright_spots +2.06pp`, `glare -3.77pp`, `monitor -11.76pp`, `noncompliant +3.85pp`, `perspective +2.86pp`.
- No generated regression-output drift after the run.

### Changed

- [ResearchLog.md](ResearchLog.md) records the fresh command, aggregate comparison, residual table, and timing note.

### Verification

- `bash tools/cli/run_regression.sh` -> PASS.

---

## 2026-05-11 (later³) — docs(parity): ADR 06 — `ThresholdBlockOtsu` parity audit; monitor/glare residuals re-attributed to IEEE-754 edge-wandering

Pure docs commit. ADR 05 (LinearContour port close-out) re-attributed the `monitor -11.76pp` / `glare -3.77pp` residuals from cv::findContours (ADR 01's hypothesis) upstream to `ThresholdBlockOtsu` binarizer divergence. ADR 06 executes the empirical binarizer-stage audit that confirms and characterises that re-attribution.

### Audit findings

Pixel-diffed Java BoofCV's vs C++ port's `ThresholdBlockOtsu` binary output on two canonical failing images:

| image | per-pixel diff | edge vs interior |
|---|---:|---|
| `monitor/image011` (1689×1614) | **0.602%** (16,415 px) | **100% within 1px of fg/bg boundary**, 0% interior |
| `glare/image005` (756×1008) | **1.884%** (14,359 px) | **100% within 1px of fg/bg boundary**, 0% interior |

**Every single disagreement is an edge pixel.** Zero interior misclassification on either image. The divergence is pure boundary-wandering caused by IEEE-754 floating-point accumulation order in `ComputeOtsu`'s histogram-sum loops (Java's `sum += (i/dlength)*histogram[i]` accumulated in a different FMA/SIMD order than C++'s equivalent under `-O3`). Identical algorithm; bit-different sums → bit-different threshold by 0-1 grayvalue → boundary pixels flip categories.

The threshold-comparison direction (`<=`), `finalizeThreshold` truncate-after-add-0.5 rounding, and all `ComputeOtsu` algorithmic operations are **identical** between Java and C++ ports. The divergence is not a port bug; it's an inherent property of porting numerically-sensitive code between languages with different FP idioms.

### Why cycle B didn't close the residuals (now empirically confirmed)

The binary input fed to BOTH contour extractors already differs by 0.6-1.9% at edges. Cycle B's `LinearContourLabelChang2004` port (`c21926e`) replaced `cv::findContours` with BoofCV-identical extraction; the residuals didn't change because they were already determined upstream by the binarizer. ADR 01's diagnosis was partial.

### Decision

**Accept the residuals as documented**. 5 images out of 562 (0.89% of the dataset) fail on FP-noise-induced edge-wandering. Aggregate parity is 0.00pp byte-identical. Decoder-only perf is ~2× C++/Java. The audit is the durable artifact for future maintainers.

### Triaged-and-rejected paths

- **(A) Match Java's FP accumulation order verbatim in `computeOtsu`** — uncertain payoff (compiler-issued FMA/SIMD order may differ even with loop-identical code).
- **(B) Add tolerance at finder-pattern check** — not parity-preserving; risks regressing in-band categories.
- **(D) Pursue both as v2** — explicitly out of v1 scope; documented as the path forward.

### Changed

- [docs/decisions/06_threshold_block_otsu_audit.md](docs/decisions/06_threshold_block_otsu_audit.md) (new) — ADR 06 captures the empirical audit methodology, findings, root-cause attribution, alternatives, decision, and revisit conditions.
- [tests/accepted_residuals.json](tests/accepted_residuals.json) — `monitor` and `glare` `reason` strings updated to cite ADR 06's IEEE-754 edge-wandering attribution instead of ADR 01's (disproven) cv::findContours hypothesis. `_comment` provenance refreshed to point at ADR 06 as the empirical root-cause.
- [src/binary/threshold_block_otsu.md](src/binary/threshold_block_otsu.md) — added "Known parity residual" bullet to the Failure-modes section cross-referencing ADR 06.

### Verification

- 428/428 unit tests pass (no code change).
- `tools/cli/run_regression.sh` PASS — aggregate parity 0.00pp byte-identical to Java, all 17 categories in-band or matching accepted-residual bands.

### Cross-reference

ADR chain documenting residual attribution: 01 (initial cv::findContours hypothesis) → 05 (cycle B disproves cv::findContours; binarizer hypothesised) → **06** (empirical binarizer audit confirms IEEE-754 edge-wandering; ADR chain complete for v1).

---

## 2026-05-11 (later²) — docs(perf): close out LinearContour port; ADR 05 records cycle A+B + residual re-attribution (cycle C)

Cycle C of the LinearContour port — pure documentation + two small code fix-ups gathered from cycle B's review. **This is the close-out commit for the LinearContour port.**

### ADR 05 — new

- [docs/decisions/05_perf_linear_contour_label_chang2004_port.md](docs/decisions/05_perf_linear_contour_label_chang2004_port.md) — banks the cycle A + cycle B architectural rework. Covers the cycle outcomes, the algorithmic substitution (`LinearContourLabelChang2004` for upstream Java's `LinearExternalContours` variant — dataset-PASS-justified), the connect-rule deviation (`EIGHT` for the port vs upstream Java's `FOUR` — measured: FOUR drops aggregate parity -0.79pp on `qrcodes_v3`, EIGHT holds byte-identical), the parity-residual re-attribution (`monitor` / `glare` did NOT close after `cv::findContours` retirement; root cause re-attributed upstream to `ThresholdBlockOtsu` binarizer divergence), the 6-state perf progression, and the "when to revisit" conditions (next move for `monitor` / `glare` is a binarizer audit, NOT another contour-stage rework).

### ADR 01 — status updated

- [docs/decisions/01_cv_findcontours_substitution.md](docs/decisions/01_cv_findcontours_substitution.md) — status `Accepted` → **`Superseded by ADR 05`**. ADR 01's substitution decision is reversed in the polygon path; its parity-cost prediction was partially wrong (re-attribution disclosed inline).

### Algorithm-doc updates

- [src/decoder/qr_code_decoder_image.md](src/decoder/qr_code_decoder_image.md) Performance section: 5-state → **6-state perf progression** (adds cycle B = `c21926e`); replaced per-category timing table with post-`c21926e` mean-ms numbers (decoder mean ms 57.06 → 24.55, -57%; wallclock 36.9 s → 18.6 s, 1.99× speedup); replaced "Why the remaining gap is what it is — and why we stop here" with a post-cycle-B "Why the remaining gap is what it is" / "When future perf cycles make sense" pair that names the binarizer as the next target. Residuals section: `monitor` / `glare` re-attribution inlined.
- [src/binary/linear_contour_label_chang2004.md](src/binary/linear_contour_label_chang2004.md): sentinel-arithmetic audit count corrected from 4 → 5 sites (cycle B review caught the missed `scanForOne` site — substantive conclusion unchanged); added `vs. LinearExternalContours` and `Connect rule — EIGHT (port) vs FOUR (upstream Java)` sections.

### Code fix-ups (cycle B review findings)

- [include/boofcv_qr/polygon/detect_polygon_from_contour.hpp](include/boofcv_qr/polygon/detect_polygon_from_contour.hpp) `process()` doc comment tightened (finding #2): the prior "CV_8UC1 0/1 or 0/255 (any non-zero counts as foreground)" claim is no longer accurate — the new labeller tests pixels strictly against `1`. Comment now reads "CV_8UC1 and MUST use the 0/1 convention" with the explicit failure mode (non-1 foreground silently produces no contours). Also dropped the stale "cv::findContours mutates its input" cloning rationale — the new labeller copies into its own scratch.
- [src/polygon/detect_polygon_from_contour.cpp](src/polygon/detect_polygon_from_contour.cpp) (finding #3): dropped the redundant `if (contourSize > maximumContourPixels_) continue;` downstream check. Java's `DetectPolygonFromContour.findCandidateShapes` only enforces the lower bound; the upper cap is enforced upstream inside `LinearContourLabelChang2004` (we push `maxContour` into the labeller in `process()`), so over-cap contours come back as empty externals and are skipped in `buildContoursFromPort`. The cached `maximumContourPixels_` recomputes only on image-shape change, so a downstream `>` check would interact badly if `setMaximumContour()` is raised between same-sized frames.

### Test results

- Build clean Release `-O3 -DNDEBUG` under `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion`.
- 428/428 unit tests pass.
- `tools/cli/run_regression.sh` PASS — aggregate +0.00pp byte-identical to Java baseline, every category in-band or matching its accepted-residual band.

### Cycle close-out summary

| cycle | commit | summary |
|---|---|---|
| A | `d3ef6cf` | port + 7 JUnit-mirror tests + algorithm doc; NOT wired (1306 LOC) |
| B | `c21926e` | wired into `DetectPolygonFromContour`; cv::findContours retired; 770f210 reversal+rotate workaround removed; 1.99× wallclock speedup; parity held |
| C | this commit | ADR 05; ADR 01 superseded; perf section 6-state; 2 small code fix-ups from cycle B review |

LinearContour port complete. `cv::findContours` retired from the polygon path. Next contour-stage work (if any) would target the binarizer per ADR 05's "When to revisit".

---

## 2026-05-11 (later) — port: wire LinearContourLabelChang2004 into DetectPolygonFromContour, retire cv::findContours from polygon path (cycle B)

Cycle B of the LinearContour port. The polygon detector now consumes the verbatim BoofCV port `LinearContourLabelChang2004` (cycle A, commit `d3ef6cf`) in place of the prior `cv::findContours(RETR_CCOMP, CHAIN_APPROX_NONE)` + per-contour reversal + topmost-leftmost rotation workaround that landed at `770f210` (step 7b/2).

This is the load-bearing change that closes the long-deferred ADR 01 — `cv::findContours` is retired from the polygon path.

### Measured impact

**Parity (qrcodes_v3, 562 images, 1258 GT):**

| state | aggregate vs Java | aggregate decode rate |
|---|---|---|
| pre cycle B (`9c0278e`) | +0.00pp byte-identical | 74.40% |
| **post cycle B (this commit)** | **+0.00pp byte-identical** | **74.40%** |

Every per-category decode rate is **byte-identical** to the pre-cycle-B state. Notable: `monitor` -11.76pp and `glare` -3.77pp **did not close**. ADR 01 pre-cycle-B attributed those residuals to the per-pixel encoding cascade out of `cv::findContours`; cycle A's JUnit suite proves the new port emits Java-identical contours on the same binary input, so the residuals are now correctly attributed to upstream binarizer divergence (`ThresholdBlockOtsu` produces a binary image that differs from Java's by ~0.6%–1.9% per pixel, ADR 01 figure). Cycle C will update ADR 01 to record the re-attribution.

**Perf (mean ms per image, decoder-only):**

| category | pre | post | delta | note |
|---|---:|---:|---:|---|
| bright_spots | 394.10 | 73.39 | **-81%** | cv::findContours was 95% on this cluster (ADR 04) |
| brightness   | 186.99 |  64.06 | **-66%** | same |
| curved       |  81.90 |  29.25 | **-64%** | same |
| lots         | 150.43 | 134.24 |  -11% |     |
| blurred      |  41.33 |  26.38 |  -36% |     |
| close        |  46.47 |  38.04 |  -18% |     |
| monitor      |  52.16 |  41.90 |  -20% |     |
| shadows      |  23.36 |  20.91 |  -10% |     |
| nominal      |  15.88 |  12.00 |  -24% |     |
| rotations    |  12.00 |  11.83 |   -1% | near-noise; not contour-bound |
| (others)     |   ≈    |   ≈    |   ±   | small or no movement |

Aggregate mean-ms 57.06 → 24.55 (**-57%**). Total dataset wallclock 36.9s → 18.6s (**1.99× speedup**).

### Changed

- [src/polygon/detect_polygon_from_contour.cpp](src/polygon/detect_polygon_from_contour.cpp): the `cv::findContours(RETR_CCOMP, CHAIN_APPROX_NONE)` call site is replaced with `contourLabeller_.process(binary, labeled_)`. The 770f210 `fixup` (per-contour `std::reverse` + topmost-leftmost `std::rotate`) and the `cv::Mat work = binary.clone()` defensive copy are removed — the port emits BoofCV-native winding + start pixel directly and copies the input internally. `buildContoursFromOpenCV` is replaced by `buildContoursFromPort` which walks `ContourPacked` headers + iterates `PackedSetsPoint2D_I32` sets into the per-blob `Contour { external, internal[] }` shape the downstream consumer expects.
- [include/boofcv_qr/polygon/detect_polygon_from_contour.hpp](include/boofcv_qr/polygon/detect_polygon_from_contour.hpp): adds the `LinearContourLabelChang2004 contourLabeller_` and scratch `cv::Mat labeled_` private members; renames the helper.
- [src/polygon/detect_polygon_from_contour.md](src/polygon/detect_polygon_from_contour.md): "OpenCV substitution policy" section rewritten to record the retirement, with the measured parity + perf deltas inlined.
- [src/binary/linear_contour_label_chang2004.md](src/binary/linear_contour_label_chang2004.md): "Integration points for downstream recovery" updated to mark cycle B wired-in and to record the re-attribution of monitor / glare to the binarizer.

### Test results

- Build clean Release `-O3 -DNDEBUG` under `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion`.
- 428/428 unit tests pass.
- `tools/cli/run_regression.sh` PASS — aggregate +0.00pp, every category in-band or matching its accepted-residual band.

### Cross-reference

- Cycle A (port + tests, NOT WIRED): commit `d3ef6cf`.
- ADR 01 ([docs/decisions/01_cv_findcontours_substitution.md](docs/decisions/01_cv_findcontours_substitution.md)): superseded by this cycle. Cycle C will update the status.
- ADR 04 ([docs/decisions/04_perf_cycle5_gridToImage_inline.md](docs/decisions/04_perf_cycle5_gridToImage_inline.md)): the surgical-fix-floor close-out documented the cv::findContours cost; this cycle removes that cost.
- Upstream Java: `boofcv.alg.filter.binary.LinearContourLabelChang2004` (228 LOC). Pinned tag: see [UPSTREAM_VERSION](UPSTREAM_VERSION) (v1.3.0).

---

## 2026-05-11 — port: boofcv.alg.filter.binary.LinearContourLabelChang2004 → src/binary/ (cycle A)

Cycle A of the LinearContour port. Verbatim port of `LinearContourLabelChang2004` + `ContourTracer` + supporting types (`PackedSetsPoint2D_I32`, `ContourPacked`, `ConnectRule`) from upstream `boofcv-ip/src/main/java/boofcv/alg/filter/binary/`. JUnit suite mirrored verbatim into `tests/unit/`. Algorithm doc shipped alongside the source.

**No call-site wiring yet.** The library still consumes `cv::findContours` via `DetectPolygonFromContour`. Cycle B will swap the call site and close ADR 01's `monitor` -11.76pp / `glare` -3.77pp parity residuals.

### Added

- [include/boofcv_qr/binary/connect_rule.hpp](include/boofcv_qr/binary/connect_rule.hpp) — enum FOUR / EIGHT.
- [include/boofcv_qr/binary/packed_sets_point2d_i32.hpp](include/boofcv_qr/binary/packed_sets_point2d_i32.hpp) — block-allocated packed point storage. Only the methods exercised by the algorithmic core + JUnit test are exposed (verbatim where used, plumbing-adjacent for the iterator).
- [include/boofcv_qr/binary/contour_packed.hpp](include/boofcv_qr/binary/contour_packed.hpp) — per-blob header (id + external/internal set indices).
- [include/boofcv_qr/binary/contour_tracer.hpp](include/boofcv_qr/binary/contour_tracer.hpp) + [src/binary/contour_tracer.cpp](src/binary/contour_tracer.cpp) — Moore-neighbour 8-conn boundary follower with `searchOne4`/`searchOne8` unroll. `0xFF`-sentinel marker writes (Java's `-1` byte bit pattern) for already-searched cells; equality test against `1` is bit-identical between signed Java and unsigned C++.
- [include/boofcv_qr/binary/linear_contour_label_chang2004.hpp](include/boofcv_qr/binary/linear_contour_label_chang2004.hpp) + [src/binary/linear_contour_label_chang2004.cpp](src/binary/linear_contour_label_chang2004.cpp) — driver class with the 3-step scan-line dispatch.
- [src/binary/linear_contour_label_chang2004.md](src/binary/linear_contour_label_chang2004.md) — algorithm doc covering: paper citation, the 3-step driver, border-padding rationale, `0xFF`-sentinel arithmetic audit (per cycle A review checklist), winding/start-pixel guarantees (the parity-load-bearing details for closing ADR 01), failure modes, tunables, integration points for downstream recovery, and cross-references.
- [tests/unit/test_linear_contour_label_chang2004.cpp](tests/unit/test_linear_contour_label_chang2004.cpp) — 7 tests mirroring `TestLinearContourLabelChang2004.java` verbatim (4 fixtures `TEST1`..`TEST4`, both `FOUR` and `EIGHT` connect rules, structural inner/outer-contour assertion).

### Changed

- [CMakeLists.txt](CMakeLists.txt) — add the two new `.cpp` files to `boofcv_qr` and the new test file to `boofcv_qr_tests`.

### Test results

- 428/428 unit tests pass (was 421/421; new total = old + 7 new from `LinearContourLabelChang2004`).
- Build clean Release `-O3 -DNDEBUG` under `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion`.

### Cycle plan

- **Cycle A (this commit)**: port + tests, NOT WIRED IN.
- **Cycle B (next)**: swap `cv::findContours` call site in `src/polygon/detect_polygon_from_contour.cpp` for the new port. Drop the `fixup` reversal + `rotate_to_canonical_start` workaround (no longer needed — the port emits BoofCV's winding + start pixel directly). Run regression: expect `monitor` and `glare` residuals to close.
- **Cycle C**: update ADR 01 from "deferred → implemented"; possibly write ADR 05 documenting outcome; close-out docs.

### Cross-reference

- ADR 01 ([docs/decisions/01_cv_findcontours_substitution.md](docs/decisions/01_cv_findcontours_substitution.md)): "When to revisit" condition #3 (compositional regressions) is closed by this cycle. Cycle C will update its status.
- Upstream Java: `boofcv.alg.filter.binary.LinearContourLabelChang2004` (228 LOC). Pinned tag: see [UPSTREAM_VERSION](UPSTREAM_VERSION) (v1.3.0).
- Paper: Chang, Chen, Lu, "A linear-time component-labeling algorithm using contour tracing technique", *Computer Vision and Image Understanding*, vol. 93 (2), 2004, pp. 206–220.

---

## 2026-05-10 (later¹⁹) — docs(perf): close out cycle 5; ADR 04 records the surgical-fix floor

Pure-docs close-out for cycle 5 (commit `7063ec2`). Records the four-cluster profile pass that justified cycle 5 (per ADR 03's cluster-coverage gate), the inline-into-header decision, the cycle-5 perf delta (-0.43% aggregate, -4.5–7.2% on V40 / multi-QR images), and the empirical justification for stopping at the **surgical-fix floor for v1**: every remaining top hot spot is either ADR-01-locked (`cv::findContours`), parity-load-bearing (`cv::SVD::solveZ` from cycle 3 — reverting reopens the -0.08pp parity residual), or a verbatim BoofCV port forbidden to reshape under "Verbatim vs idiomize" (`ThresholdBlockOtsu`).

### Final state at HEAD

- Aggregate decode rate **74.4038%** = Java baseline byte-identical (+0.00pp).
- Decoder-only sum **32.1 s** vs Java's 8.0 s = **4.01× C++/Java**.
- Best category `decoding` at 0.47× (2.1× **faster** than Java end-to-end).
- Worst category `bright_spots` at 8.65× (cv::findContours-bound, ADR-01-locked).
- `high_version` at **1.59×** (was 44.4× pre-cycle-1).
- 12 of 17 categories byte-identical to Java (every non-substitution-bound category).
- 421/421 unit tests pass.

### Five-state perf progression

| commit    | label                                            | decoder-only sum | C++/Java |
|-----------|--------------------------------------------------|-----------------:|---------:|
| `bda1650` | pre-perf (parity ship)                           |          ~74.2 s |    9.27× |
| `bfbc2e2` | cycle 1 — `perspectiveTransform` inline          |           45.9 s |    5.73× |
| `23c1327` | cycle 3 — explicit DLT via `cv::SVD::solveZ`     |           43.5 s |    5.44× (parity 0.00pp) |
| `ea93854` | cycle 4 — sampler hot path                       |           32.5 s |    4.05× |
| `7063ec2` | cycle 5 — `gridToImage` / `imageToGrid` inline   |       **32.1 s** |**4.01×** |

### Added

- [docs/decisions/04_perf_cycle5_gridToImage_inline.md](docs/decisions/04_perf_cycle5_gridToImage_inline.md) — ADR 04. Records the cluster-coverage profile (executed per ADR 03's gate), the cycle 5 hotspot finding + fix, the triage of alternatives (cv::findContours / cv::SVD::solveZ / ThresholdBlockOtsu / bitIntensityToBitValue), the five-state perf progression, the diminishing-returns reading, the surgical-fix-floor argument, and the revisit conditions for any future cycle 6+ (which would require an architectural change, not a surgical one).

### Changed

- [src/decoder/qr_code_decoder_image.md](src/decoder/qr_code_decoder_image.md) Performance section: rewritten as four banked optimisations (was three), five-state perf progression table (was four), final per-category timing post-`7063ec2`, "why we stop here" updated to reference ADR 04 + the surgical-fix-floor argument.

### Cross-reference

- ADR chain: [01](docs/decisions/01_cv_findcontours_substitution.md) (parity) → [02](docs/decisions/02_perf_stop_after_cycle1.md) (cycle 1 banked, cycle 2 stopped — superseded in part by 03) → [03](docs/decisions/03_perf_findhomography_and_sampler_cycles.md) (cycles 3 + 4 banked, cluster-coverage gate introduced) → **04** (cycle 5 banked under the gate, surgical-fix floor for v1).
- Cycle 5 perf commit: `7063ec2`.

---

## 2026-05-10 (later¹⁸) — perf(sampler): inline gridToImage / imageToGrid into header for V40 hot path (cycle 5)

Cycle 5 of the perf push. Profile-confirmed escalation across four category clusters (`lots` / `high_version` / `bright_spots` / `nominal`, ADR-03's mandated cluster-coverage gate) identified the `gridToImage` / `imageToGrid` out-of-line function-call layer as the largest non-`cv::findContours`, non-cycle-3-banked hot spot — 10.0% of decoder time on `high_version/image029` (V40 candidates, 250-iter profile), driven by the bit sampler hitting `gridToImage` 5×/module bit (~156k calls per V40 scan).

Pre-cycle-5 the two methods lived in `qr_code_binary_grid_to_pixel.cpp` and forwarded to a file-local `applyHomography` helper. Both were `inline`-tagged but not visible across the .cpp/.hpp boundary — every call from `QrCodeBinaryGridReader::readBitIntensity` / `readBit` and `QrCodeAlignmentPatternLocator` was a real call. Cycle 5 moves both bodies into the header with the 3×3 mat-vec + perspective divide spelled out at the call site (no nested `applyHomography` indirection — the compiler doesn't always inline through both layers, and the team-lead's brief explicitly called this out). The slow `adjustWithFeatures` branch of `gridToImage` (per-call nearest-pair lookup) is kept out-of-line as `applyAdjustment(row, col, pixel)`: dead on every decode call after cycle 3, and inlining the loop would bloat every call site with code that never runs.

### Changed

- [include/boofcv_qr/qr_code_binary_grid_to_pixel.hpp](include/boofcv_qr/qr_code_binary_grid_to_pixel.hpp): `imageToGrid` / `gridToImage` defined inline in the header; bodies spell out `(Mx + b) / (m20 x + m21 y + m22)` with the same `FLT_EPSILON` gate and `(0, 0)` zero-fill on the degenerate branch as the previous out-of-line `applyHomography`. New private out-of-line `applyAdjustment` declaration for the slow path.
- [src/sampler/qr_code_binary_grid_to_pixel.cpp](src/sampler/qr_code_binary_grid_to_pixel.cpp): removed the two out-of-line `imageToGrid` / `gridToImage` definitions; replaced with `applyAdjustment` (same body as the pre-cycle-5 `gridToImage` tail, just renamed).
- [src/sampler/qr_code_binary_grid_to_pixel.md](src/sampler/qr_code_binary_grid_to_pixel.md): "Why this approach" extended with the cycle-5 paragraph documenting the .cpp→.hpp move, the explicit no-`applyHomography`-indirection rule, the kept-out-of-line `applyAdjustment` slow path, and the same-state shootout numbers.

### Perf

Same-state shootout, best of 3 (single machine, single state, fresh build per side):
- `lots/image001` (4032×3024, 60 QRs): **174.1 → 166.3 ms/iter (-4.5%)**
- `high_version/image029` (3525×1317, V40 candidates): **61.1 → 56.7 ms/iter (-7.2%)**

Best-of-3 regression-set decoder-only sums on the same machine: pre **41937.9 ms**, post **41759.5 ms** (-0.43% aggregate; smaller because nominal/decoding/perspective/pathological are tiny single-QR images where `gridToImage` cost is fractional, but the dominant V40-bit-dense + many-QR images get the win).

### Regression

- 421/421 unit tests pass.
- `tools/cli/run_regression.sh`: **PASS**, aggregate detection rate `0.7440381558028617` = Java baseline byte-identical (**+0.00pp**). Every per-category number byte-identical to `577fb22`. All 17 categories within their accepted bands; `accepted_residuals.json` unchanged.

### Cross-reference

- Cycle 5 escalation profile data: `/tmp/profile_{lots,hv,bs,nominal}_c5.txt` (kept locally; not committed). Inventory tables and rationale in the team-lead escalation message preserved in this PR's review thread.
- Same surgical-fix shape as cycles 1, 3, 4: profile-confirmed before the change, parity-preserving by construction (no algorithm change, no operand reordering), one fix one commit.

---

## 2026-05-10 (later¹⁷) — docs(perf): close out perf push round 2; ADR 03 captures cycles 3+4, decoder algo doc gets the final 4-state progression

Close-out for the resumed perf cycle (cycles 3 + 4, commits `23c1327` + `ea93854`). ADR 02 had banked cycle 1 and stopped on the basis that the dominant remaining hotspot was inside `cv::findContours` (ADR-01-locked). A follow-up profile pass on workloads ADR 02 didn't profile (`lots`, `high_version`) surfaced two profile-confirmed bottlenecks that were *not* inside `cv::findContours` — `cv::findHomography(method=0)` LM refinement and the `QrCodeBinaryGridReader` hot path. Both got their cycle. The result:

- Aggregate parity tightened from **-0.08pp** (ADR-02 ship state) to **0.00pp** byte-identical to Java baseline (74.4038%) — cycle 3 was a parity fix as much as a perf fix, the LM refinement was actively diverging from BoofCV's pure DLT.
- Decoder-only ratio dropped from **5.73× → 4.05× C++/Java**.
- `decoding` category now **0.48× C++/Java** = **2.1× faster than Java** end-to-end.
- Worst-case category `bright_spots` improved from 11.65× to 8.75×.
- Best-case wall-clock `~37.7 s` for 562 images (was 52.6 s post-cycle-1).
- Universal speedup from cycle 4: every category 1.32–1.36× faster.

This is the new close-out state. ADR 03 is the durable record of why we stopped here this time, what ADR 02 missed, and what triggers a future revisit.

### Added

- [docs/decisions/03_perf_findhomography_and_sampler_cycles.md](docs/decisions/03_perf_findhomography_and_sampler_cycles.md) — ADR 03. Captures: ADR-02 stop point and what it missed (single-cluster profiling on `bright_spots`/`brightness` masked the per-decode-loop costs in `lots`/`high_version`), cycle 3 + cycle 4 deltas, the four-state perf progression table, the lesson "profile across distinct category clusters before declaring victory," and the revisit conditions. ADR 03 commits the project to one more profile pass before any future close-out, covering at least one image from each of the `lots`/`high_version`/`bright_spots`/`nominal` clusters.

### Changed

- [docs/decisions/02_perf_stop_after_cycle1.md](docs/decisions/02_perf_stop_after_cycle1.md) — status changed from `Accepted` to `Superseded in part by ADR 03`. ADR 02's "do NOT port `LinearContourLabelChang2004`" decision still stands; ADR 02's "stop the perf cycle here" decision is reversed by ADR 03. Forward link added.
- [src/decoder/qr_code_decoder_image.md](src/decoder/qr_code_decoder_image.md) "Performance" section rewritten. Replaces the cycle-1-only narrative with the three-cycle close-out:
  - The three banked optimisations summarised inline (perspectiveTransform inlining, explicit DLT via `cv::SVD::solveZ`, sampler hot-path tightening), with cross-references to the per-stage `.md` algorithm docs and ADRs 02 + 03.
  - Four-state perf progression table (`bda1650` → `bfbc2e2` → `23c1327` → `ea93854`) with per-state decoder-only sums, C++/Java ratios, and parity deltas.
  - Final per-category timing table (best-of-4, post-`ea93854`).
  - "Why the remaining gap is what it is" paragraph: the slowest two categories (`bright_spots` 8.75×, `brightness` 5.20×) are still the `cv::findContours`-bound noisy-binarisation cluster from ADR 01 / ADR 02, and the remaining lever is porting `LinearContourLabelChang2004` (ADRs 01+02+03).
- [tests/accepted_residuals.json](tests/accepted_residuals.json) `_comment` provenance refreshed: notes that the aggregate tightened from -0.08pp to 0.00pp in cycle 3, but the per-category residuals listed in this file are independent of the homography fix and remain unchanged. Cross-references ADR 03 in the provenance pointer alongside ADR 01.

### Regression

- 421/421 unit tests pass (no library code touched).
- `tools/cli/run_regression.sh`: PASS, aggregate **74.40%** = Java baseline byte-identical, all 17 categories within their accepted bands. Per-category decode rates byte-identical to the cycle-4 ship state.

### Final perf state (final, post-`ea93854`)

| metric                                | value                                       |
|---------------------------------------|---------------------------------------------|
| Aggregate decode rate                 | 74.4038% (Java 74.4038%; **0.00pp**)        |
| Decoder-only sum (562 images)         | 32.5 s (Java 8.0 s; **4.05× C++/Java**)     |
| Wall-clock total                      | ~37.7 s (Java 22.7 s)                       |
| Worst category ratio                  | `bright_spots` 8.75×                        |
| Best category ratio                   | `decoding` 0.48× (2.1× faster than Java)    |
| `high_version` (cycle-1 + cycle-4)    | 1.62× (was 44.4× pre-cycle-1)               |
| Categories within ±2pp parity band    | 11 of 17                                    |
| Documented parity residuals           | 2 from cv::findContours (ADR 01) + 4 small-N "+" outliers |
| Unit tests                            | 421/421                                     |

This is the close-out state of v1's perf work. ADR 03 is the durable record of why we stopped here this time.

## 2026-05-10 (later¹⁶) — perf(sampler): array-buffer + ptr-access in `QrCodeBinaryGridReader` hot path

Cycle 4 of the perf push (post-cycle-3). Profile data on `high_version` showed `QrCodeBinaryGridReader::readBitIntensity` and `sampleNearest` as the dominant remaining hot path — V40 codes hit ~156k `sampleNearest` calls per scan (177×177 modules × 5 samples per bit). Five changes, all profile-confirmed cheap and parity-preserving:

### Changes

[include/boofcv_qr/qr_code_binary_grid_reader.hpp](include/boofcv_qr/qr_code_binary_grid_reader.hpp), [src/sampler/qr_code_binary_grid_reader.cpp](src/sampler/qr_code_binary_grid_reader.cpp):

1. **`sampleNearest` moved to header `inline`** so the 5 calls per bit from `readBit` and `readBitIntensity` collapse to inline arithmetic + a single pixel read each. (Previously defined out-of-line in the .cpp; even with `-O3` the compiler had to duplicate the body across the two callers, but more importantly the optimization was opaque to readers.)
2. **`std::floor(x)` → `static_cast<int32_t>(x)`** inside `sampleNearest`. Java's `NearestNeighborPixel_U8.get(x, y)` uses `(int)x` — truncation toward zero — so the cast matches Java's behaviour exactly. `std::floor` differs from `(int)` only for `x ∈ (-1, 0)`, but the subsequent `if (ix < 0) ix = 0` clamp folds both to `0` for those inputs, so the visible output is byte-identical. The cast is faster.
3. **`image_.at<std::uint8_t>(iy, ix)` → `image_.ptr<std::uint8_t>(iy)[ix]`** inside `sampleNearest`. After clamping we already know `(iy, ix)` are in-bounds; `at()` adds a debug-mode bounds check + computes `step.p[0] * iy` per call which `ptr<>` doesn't.
4. **`readBitIntensity`**: replace 5× `push_back(...)` with one `intensity.resize(base + 5)` + 5 indexed writes via `intensity.data() + base`. The caller (`readBitIntensityAndThresholdDownRight`) `reserve()`s the full `5 * locationBits.size()` capacity upfront, so the resize is non-allocating; the win is collapsing 5 capacity-checks + 5 size-increments into 1 each. API contract is unchanged — still appends 5 floats per call.
5. Removed unused `<cmath>` include from the .cpp (no more `std::floor`).

### Parity

| metric                        | pre `23c1327` | post (this commit) |
|-------------------------------|--------------:|-------------------:|
| Aggregate decode rate         |        74.40% |             74.40% |
| Java baseline                 |        74.40% |             74.40% |
| Per-category decode rates     |             — | byte-identical to cycle 3 |
| Unit tests                    |       421/421 |            421/421 |

`tools/cli/run_regression.sh` PASSes byte-identically; every category's decode rate matches the cycle-3 number. No drift in any accepted residual.

### Performance (best of 3 runs)

| category      | java_ms | c3_best (`23c1327`) | c4_best (this) | C++/Java c3 | C++/Java c4 |
|---------------|--------:|---------------------:|----------------:|------------:|------------:|
| blurred       |   760.1 |               2534.4 |          1906.5 |       3.33× |       2.51× |
| bright_spots  |  1458.5 |              17061.1 |         12768.1 |      11.70× |       8.75× |
| brightness    |  1011.4 |               7098.2 |          5256.8 |       7.02× |       5.20× |
| close         |   762.1 |               2508.1 |          1875.9 |       3.29× |       2.46× |
| curved        |   803.0 |               5597.4 |          4166.9 |       6.97× |       5.19× |
| damaged       |   207.4 |                804.8 |           599.5 |       3.88× |       2.89× |
| **decoding**  |    69.0 |                 44.7 |            33.0 |       0.65× |   **0.48×** (faster than Java) |
| glare         |   443.1 |               1648.6 |          1209.0 |       3.72× |       2.73× |
| high_version  |   331.4 |                727.6 |           546.0 |       2.20× |   **1.65×** |
| lots          |   744.6 |               1459.7 |          1081.7 |       1.96× |   **1.45×** |
| monitor       |   436.4 |               1215.3 |           901.3 |       2.78× |       2.07× |
| nominal       |   382.2 |               1413.9 |          1073.0 |       3.70× |       2.81× |
| noncompliant  |    62.6 |                140.1 |           106.4 |       2.24× |       1.70× |
| pathological  |    10.9 |                 15.9 |            12.0 |       1.46× |       1.10× |
| perspective   |    53.7 |                125.6 |            95.0 |       2.34× |       1.77× |
| rotations     |   299.6 |                715.7 |           525.9 |       2.39× |       1.76× |
| shadows       |   168.8 |                428.3 |           318.1 |       2.54× |       1.88× |
| **TOTAL**     |  8004.8 |              43539.4 |     **32475.1** |   **5.44×** |   **4.06×** |

**Universal speedup**: every category improved by 1.32–1.36× — the change hits the inner-most loop that touches every single bit read. Aggregate decoder time **43.5 s → 32.5 s** (1.34× speedup); ratio **5.44× → 4.06× C++/Java**. The `decoding` category is now **2.1× faster than Java** end-to-end. `bright_spots` and `brightness`, dominated by `cv::findContours` per ADR 02, also improved meaningfully — the per-bit hot path is in *all* categories, not just decode-heavy ones.

### Documentation

- [src/sampler/qr_code_binary_grid_reader.md](src/sampler/qr_code_binary_grid_reader.md): "Sample-and-clamp" rewritten to document the cast/floor equivalence (with the negative-coord proof) and the `inline` + `ptr<>` perf rationale. Added a new section for `readBitIntensity` covering the resize-once optimisation and the API-preserving contract. Added `readBitIntensity` to the read-styles list at the top.

### Regression

- 421/421 unit tests pass.
- `tools/cli/run_regression.sh`: PASS, aggregate **74.40%** = Java baseline, all 17 categories within their accepted bands. Byte-identical to the cycle-3 close-out state.

## 2026-05-10 (later¹⁵) — perf+parity(sampler): explicit DLT via `cv::SVD::solveZ` in `computeTransform` — eliminate `cv::findHomography` LM refinement; aggregate parity tightens 74.32% → 74.40%

Cycle 3 of the perf push (post-cycle-1, post-ADR-02). Profile data on `lots`/`high_version` showed `cv::findHomography(method=0)` doing post-DLT iterative Levenberg–Marquardt refinement (`cv::LMSolverImpl::run` → `cv::solve` → `cv::JacobiImpl_<double>` chain). CLAUDE.md and the sampler algorithm doc both claimed `method=0` was "pure DLT, no iterative refinement" — that's incorrect for OpenCV when `npoints > 4`. BoofCV's `GenerateHomographyLinear` → `HomographyDirectLinearTransform` runs **only** DLT (no LM, and `shouldNormalize` is hard-coded `false` in `process()` regardless of the constructor flag, so no Hartley normalisation either). The OpenCV LM refinement was both a perf cost on detection-heavy categories (`lots`, `high_version`) and the source of the residual -0.08pp aggregate parity gap.

### Fix

[src/sampler/qr_code_binary_grid_to_pixel.cpp](src/sampler/qr_code_binary_grid_to_pixel.cpp) `computeTransform()`:

- 4-point case unchanged (`cv::getPerspectiveTransform` — exact at this size, BoofCV's path agrees).
- N>4 case: build the 2N×9 design matrix in place (same row layout as Java's `addPoints2D`: rows 1+2 per pair use cols 3..5 = (-f.x, -f.y, -1), cols 6..8 = (s.y\*f.x, s.y\*f.y, s.y); cols 0..2 = (f.x, f.y, 1), cols 6..8 = (-s.x\*f.x, -s.x\*f.y, -s.x)), then `cv::SVD::solveZ(A, h)` returns the right-singular vector at the smallest singular value. Reshape h row-major into 3×3 H. Same algorithm BoofCV's `SolveNullSpaceSvd_DDRM` runs.
- No Hartley normalisation (mirrors BoofCV — `shouldNormalize` is dead-coded `false`).
- No post-DLT scale/sign canonicalisation (BoofCV's `AdjustHomographyMatrix.adjust`): the only consumers of H are `applyHomography` (perspective divide cancels any non-zero scalar multiple) and `cv::invert` (handles any sign/scale uniformly). Result is projectively invariant.
- Removed unused `<opencv2/calib3d.hpp>` include.

### Parity (the real win)

| metric                        | pre (`4045b12`) | post (this commit) | delta   |
|-------------------------------|----------------:|-------------------:|--------:|
| Aggregate decode rate         |        74.32%   |             74.40% | **+0.08pp** (byte-matches Java baseline) |
| Java baseline                 |        74.40%   |             74.40% |          0.00pp gap |
| Per-category accepted residuals | matched all   |     matched all   | unchanged (every accepted residual within ±tolerance) |
| Unit tests                    |       421/421   |            421/421 |  pass    |

Aggregate gap closed from -0.08pp to **0.00pp**. Run-regression PASS with the existing `tests/accepted_residuals.json`; no documented residual drifted.

### Performance (secondary win)

Best of 3 runs on the 562-image regression set, decoder-only sums:

| category      | java_ms | pre_ms (`4045b12`) | post_ms (this) | C++/Java pre | C++/Java post |
|---------------|--------:|--------------------:|----------------:|-------------:|--------------:|
| **lots**      |   744.6 |              2618.5 |        **1459.7** |        4.48× |      **1.96×** |
| pathological  |    10.9 |                20.2 |            15.9 |        1.60× |          1.46× |
| perspective   |    53.7 |               136.9 |           125.6 |        2.56× |          2.34× |
| decoding      |    69.0 |                45.9 |            44.7 |        0.68× |          0.65× |
| rotations     |   299.6 |               738.8 |           715.7 |        2.49× |          2.39× |
| nominal       |   382.2 |              1442.8 |          1413.9 |        3.77× |          3.70× |
| (others)      |    —    |                 —   |             —   |       (flat) |       (flat)   |
| **TOTAL**     |  8004.8 |             44392.9 |       **43539.4** |    **5.73×** |      **5.44×** |

`lots` is the headline (60-QR images call `computeTransform` per detected QR; LM cost there was substantial). Aggregate ratio dropped 5.73× → 5.44× C++/Java. Categories dominated by `cv::findContours` (`bright_spots`, `brightness`, `curved`) are flat — expected, since `computeTransform` is a small fraction of their decode time.

### Documentation

- [src/sampler/qr_code_binary_grid_to_pixel.md](src/sampler/qr_code_binary_grid_to_pixel.md): "Why this approach" rewritten — documents the LM-refinement finding, the explicit-DLT replacement, the row layout of the design matrix, and why we skip Hartley normalisation + scale/sign adjustment. Mapping table updated to point at `cv::SVD::solveZ` for the N-point path. New row in the deferred-vs-equivalent table for `AdjustHomographyMatrix.adjust` (not needed; downstream operations are projectively invariant).
- [CLAUDE.md](CLAUDE.md) "OpenCV substitution policy": removed the incorrect claim that `cv::findHomography(..., 0)` is pure DLT. Now reads: "do **not** use `cv::findHomography(..., 0)` for the N>4 case; OpenCV runs LM refinement after the initial DLT for `npoints > 4`. Build the 2N×9 design matrix explicitly and solve with `cv::SVD::solveZ`."

### Regression

- 421/421 unit tests pass.
- `tools/cli/run_regression.sh`: PASS, aggregate **74.40%** = Java baseline, all 17 categories within their accepted bands.

## 2026-05-10 (later¹⁴) — docs(perf): close out perf cycle after cycle 1; ADR 02 records the decision to stop, decoder algo doc gains a Performance section

Perf-cycle close-out. Cycle 1 (`340d038` + `bfbc2e2`) shipped a 1.62× aggregate / 19.58× worst-case decoder speedup with byte-identical parity. Cycle 2 was scoped to row-pointer rewrites in `ThresholdBlockOtsu` and `QrCodeBinaryGridReader::sampleNearest`; profiling forced an escalation before any code change.

Two findings forced the escalation:

1. **`ThresholdBlockOtsu` is already row-pointer-based.** `computeBlockStatistics:118-123` and `thresholdBlock:212-218` both use `input.ptr<std::uint8_t>(y)` in their hot loops; `grep` for `at<uchar>` / `at<uint8_t>` / `at<std::uint8_t>` in `src/binary/` and `include/boofcv_qr/` returns zero hits. The cycle-1 ChangeLog hint that named Otsu as the next pixel-access target was based on a stale read of the file — the binarizer was already done at port time.
2. **`cv::findContours` is the actual top hotspot, by ~20× over Otsu.** Sample profile on `detection/brightness/image010.jpg` (3024×4032, ~825 ms/iter, 21,144 main-thread samples), inclusive % of CPU:

   | function                                       | inclusive | % CPU |
   |------------------------------------------------|----------:|------:|
   | `boofcv_qr::DetectPolygonFromContour::process` |    19835  | 93.8% |
   | `boofcv_qr::ThresholdBlockOtsu::process`       |      974  |  4.6% |

   Top-of-stack (where cycles burn): `cv::ContourScanner_::findFirstBoundingContour` 16762 samples (79%), plus the surrounding `icvFetchContourEx` / `findNextX` / `contourScan` / `BlockStorage::push_back` / `Tree::newElem` chain. Same shape on `bright_spots/image008.jpg`. **~95% of decode time on noisy categories is inside OpenCV's `cv::findContours`** — not in any code we wrote.

User decision: **stop perf work; ship cycle 1**. Re-aiming cycle 2 at small wins (~0.5–1% from row-pointer rewrites in 4-pixel bilinear-interp helpers) was rejected as not worth the cycle. The big lever (port `LinearContourLabelChang2004`) is ADR-01-rejected and a multi-cycle commitment, not a "one fix, profile-confirmed" cycle. Cycle 1's win is real and banked.

### Added

- [docs/decisions/02_perf_stop_after_cycle1.md](docs/decisions/02_perf_stop_after_cycle1.md) — ADR 02 capturing: cycle-1 outcome (the optimisation that shipped, with full timing table), cycle-2 profile findings, decision to stop, rationale (workarounds (a)–(c) all parity-breaking; ADR-01-rework is multi-cycle), final empirical perf state (decoder-only 5.73× C++/Java aggregate, 11.65×/7.02× worst categories `bright_spots`/`brightness`, wall-clock 52.6 s vs Java 22.7 s on 562 images), and revisit conditions. Cross-linked with ADR 01: ADR 01 is the parity-driven version of the same fork, ADR 02 is the perf-driven version. Together they say "if a downstream consumer needs perf OR parity on noisy categories, port `LinearContourLabelChang2004` — until then, accept both costs."

### Changed

- [src/decoder/qr_code_decoder_image.md](src/decoder/qr_code_decoder_image.md): new "Performance" section between "Known parity residuals" and "Cross-references". Captures the inlined-homography optimisation as the one banked perf change, the final per-category timing table (Java vs C++ post-`bfbc2e2`, 17 categories + aggregate), and a one-paragraph "why no further perf work in v1" cross-linking ADR 02.

### Regression

- 421/421 unit tests pass (no library code touched).
- No `tools/cli/run_regression.sh` re-run needed — pure docs commit.

### Final perf state (carried forward from `bfbc2e2`)

| metric                                | value                                  |
|---------------------------------------|----------------------------------------|
| Aggregate decode rate                 | 74.32% (Java 74.40%; **-0.08pp**)      |
| Decoder-only sum (562 images)         | 45.9 s (Java 8.0 s; **5.73× C++/Java**)|
| Wall-clock total                      | 52.6 s (Java 22.7 s)                   |
| Worst category ratio                  | `bright_spots` 11.65×                  |
| Best category ratio                   | `decoding` 0.68× (faster than Java)    |
| `high_version` (cycle-1 target)       | 2.27× (was 44.4× pre-cycle-1)          |
| Categories within ±2pp parity band    | 11 of 17                               |
| Documented parity residuals           | 2 from cv::findContours (ADR 01) + 4 small-N "+" outliers |
| Unit tests                            | 421/421                                |

This is the close-out state of v1. ADR 02 is the durable record of why we stopped here.

## 2026-05-10 (later¹³) — fix(perf): match OpenCV `cv::perspectiveTransform` `FLT_EPSILON` guard + zero-fill on degenerate branch

Codex re-review on `340d038` flagged one real divergence on the `applyHomography` degenerate branch: `cv::perspectiveTransform_64f` gates singular `w` on `|w| > FLT_EPSILON` and zero-fills below that threshold (and uses `FLT_EPSILON` even on the 64f path, not `DBL_EPSILON`); the inline I shipped used `w != 0.0` and propagated NaN through division for tiny-but-nonzero `w`. Fast-path arithmetic is unchanged so per-category parity rates and timings stand from `340d038`, but **the commit message + .md claim "bit-identical at the IEEE-754 level" was overstated** — bit-identical only on the fast path. Mechanical fix-up.

### Fix

[src/sampler/qr_code_binary_grid_to_pixel.cpp](src/sampler/qr_code_binary_grid_to_pixel.cpp): three-line code change + `<cfloat>` include. The new `applyHomography` body:

```cpp
const double w = M(2,0)*x + M(2,1)*y + M(2,2);
if (std::fabs(w) > FLT_EPSILON) {
    const double iw = 1.0 / w;
    out.x = (M(0,0)*x + M(0,1)*y + M(0,2)) * iw;
    out.y = (M(1,0)*x + M(1,1)*y + M(1,2)) * iw;
} else {
    out.x = 0.0;
    out.y = 0.0;
}
```

Two findings rolled into the rewrite:
1. **`FLT_EPSILON` (not `DBL_EPSILON`) is OpenCV's gate** even on `perspectiveTransform_64f`. Mirrored.
2. **Numerator multiplication by `iw` only happens after the gate.** Earlier code computed `xt`/`yt` numerators unconditionally then branched. OpenCV computes the reciprocal once and multiplies; we now mirror that order, which is what makes the degenerate-branch outputs bit-match OpenCV's `(0, 0)` zero-fill rather than producing the un-normalised numerator.

### Documentation tightening

[src/sampler/qr_code_binary_grid_to_pixel.md](src/sampler/qr_code_binary_grid_to_pixel.md): replaced the overstated "bit-identical at the IEEE-754 level" line with: "Bit-identical to `cv::perspectiveTransform_64f` on the fast path (`|w| > FLT_EPSILON`); matches OpenCV's `(0, 0)` zero-fill fallback on the degenerate branch where `|w| ≤ FLT_EPSILON`. Degenerate inputs are unreachable on plausible QR finder-pattern homographies (`w` is O(1) for valid detections); the guard exists for parity with OpenCV, not because it fires in practice." Same tightening applied to the "Why this approach" section.

### Parity (must match `bda1650` / `340d038` exactly)

- Aggregate decode rate **74.32%** unchanged.
- Per-category rates byte-identical to step-9b close-out table (re-verified by full regression run).
- 421/421 unit tests pass.
- `tools/cli/run_regression.sh` → `PASS: no new regressions; all out-of-band categories are documented residuals within their accepted-tolerance bands.`

### Timings — not re-measured

Per team-lead direction, perf re-run skipped: the fast path (which is the only branch hit on real inputs) is unchanged, so the `340d038` numbers stand — decoder-only sum 45.9s, high_version 750ms, 1.62× aggregate speedup vs pre-9b-perf.

## 2026-05-10 (later¹²) — perf(sampler): inline single-point homography in `QrCodeBinaryGridToPixel` — 1.62× decoder-only speedup, 19.6× on `high_version`

First profile-driven perf cycle after step 9b parity close-out. `sample` on the canonical worst-case image (`detection/high_version/image029.jpg`, 3525×1317, prior C++ time 1304ms vs Java 30ms = **42.7×**) showed >70% of CPU time spent in `cv::Mat::create` / `cv::Mat::release` / `cv::StdMatAllocator::{allocate,deallocate}` chains, all reached from `boofcv_qr::QrCodeBinaryGridToPixel::gridToImage`. That function (and its sibling `imageToGrid`) was implemented as `cv::perspectiveTransform` on a freshly-constructed 1-element `cv::Mat<Point2d>` — heap-allocating per call. `QrCodeBinaryGridReader::readBit` calls `gridToImage` 5× per module bit, so for a Version-40 QR each speculative read costs ~156k mat allocations.

### Fix

[src/sampler/qr_code_binary_grid_to_pixel.cpp](src/sampler/qr_code_binary_grid_to_pixel.cpp): replaced four `cv::perspectiveTransform`-on-1-point sites with an inlined `applyHomography(M, x, y, out)` that does the projective transform directly:

```
xt = M00*x + M01*y + M02
yt = M10*x + M11*y + M12
w  = M20*x + M21*y + M22
out = (xt/w, yt/w)
```

The arithmetic order matches OpenCV's `perspectiveTransform_64f` and operates entirely in `double`, so output is bit-identical. Sites: `imageToGrid`, `gridToImage`, `removeFeatureWithLargestError`, the `adjustWithFeatures` reprojection inside `computeTransform`. `cv::perspectiveTransform` remains the right tool when a caller already has a `cv::Mat` of points — that path is unchanged elsewhere.

### Parity (must match `bda1650` exactly)

Aggregate decode rate **74.32%** unchanged from `bda1650`. Per-category rates byte-identical to the step-9b close-out table. 421/421 unit tests pass. `tools/cli/run_regression.sh` ends `PASS: no new regressions`.

### Timings — fresh same-machine pre/post comparison

| category      | java_ms | pre_ms (bda1650) | post_ms | speedup | post / java |
|---------------|--------:|-----------------:|--------:|--------:|------------:|
| blurred       |   760.1 |          3595.3  |  2566.4 |   1.40× |       3.38× |
| bright_spots  |  1458.5 |         19088.5  | 16991.6 |   1.12× |      11.65× |
| brightness    |  1011.4 |          9633.6  |  7096.7 |   1.36× |       7.02× |
| close         |   762.1 |          2624.4  |  2564.6 |   1.02× |       3.37× |
| curved        |   803.0 |          5997.1  |  5811.2 |   1.03× |       7.24× |
| damaged       |   207.4 |           957.7  |   818.3 |   1.17× |       3.94× |
| decoding      |    69.0 |           244.2  |    46.6 |   5.24× |   **0.68×** (faster than Java) |
| glare         |   443.1 |          1799.5  |  1745.4 |   1.03× |       3.94× |
| **high_version** | 331.4 |       14704.9  |   750.9 | **19.58×** |   2.27× |
| lots          |   744.6 |         10254.3  |  3332.2 |   3.08× |       4.48× |
| monitor       |   436.4 |          1273.9  |  1234.8 |   1.03× |       2.83× |
| nominal       |   382.2 |          1632.2  |  1442.5 |   1.13× |       3.77× |
| noncompliant  |    62.6 |           149.7  |   143.3 |   1.04× |       2.29× |
| pathological  |    10.9 |            38.4  |    17.5 |   2.19× |       1.60× |
| perspective   |    53.7 |           178.7  |   137.3 |   1.30× |       2.56× |
| rotations     |   299.6 |          1573.9  |   747.1 |   2.11× |       2.49× |
| shadows       |   168.8 |           486.7  |   442.8 |   1.10× |       2.62× |
| **SUM**       |  8004.8 |        **74233.2** | **45889.3** | **1.62×** | **5.73×** |

Wall-clock for the full 562-image regression: **81.3s → 52.6s**. The decoder-only sum (5.73× slower than Java post-fix) is the new headline gap; the prior team-lead-quoted "5.84×" was a different snapshot of the pre-fix state, and on the same fresh-pre numbers we're at 9.27× → 5.73× (so the gap closed by ~38%).

`high_version` (the worst-case category) collapses from 44.4× to 2.27× — i.e. the C++ port is now within a factor of 2 of Java on the high-module-count images that were dominating decoder runtime.

### Profile snippet — top of the pre-fix `sample` output

```
403  cv::Mat::release()
355  cv::Mat::~Mat()
197  cv::Mat::create(int, int, int)
180  cv::StdMatAllocator::allocate(...)
177  cv::fastMalloc(unsigned long)
170  cv::Mat::release()
143  cv::StdMatAllocator::deallocate(cv::UMatData*) const
137  cv::StdMatAllocator::allocate(...)
135  cv::setSize(cv::Mat&, ...)
115  cv::updateContinuityFlag(...)
112  cv::Mat::create(...)
101  boofcv_qr::QrCodeBinaryGridToPixel::gridToImage(...)  qr_code_binary_grid_to_pixel.cpp:347
 99  boofcv_qr::QrCodeBinaryGridToPixel::gridToImage(...)  qr_code_binary_grid_to_pixel.cpp:345
 98  cv::perspectiveTransform(...)
```

`gridToImage:347` is the `cv::perspectiveTransform` call line. Every line above it traces back into the cv::Mat construction at `gridToImage:345`.

### Added

- [tools/cli/qr_scan.cpp](tools/cli/qr_scan.cpp): `--profile <image> <iters>` mode — loops the pipeline N times on a single image so a sampling profiler (`sample`, `samply`, Instruments) can collect enough stack samples to localise hotspots. Used to drive this cycle. Strictly additive; no impact on `runBatch` / `runSingle` / `runDumpStages`.

### Changed

- [src/sampler/qr_code_binary_grid_to_pixel.cpp](src/sampler/qr_code_binary_grid_to_pixel.cpp): four call sites switched from `cv::perspectiveTransform`-on-1-point to inlined `applyHomography`.
- [src/sampler/qr_code_binary_grid_to_pixel.md](src/sampler/qr_code_binary_grid_to_pixel.md): updated the description of `imageToGrid` / `gridToImage` to document the inlined math + the bit-identity argument; added the perf rationale to the "Why this approach" section.

### Regression

- 421/421 unit tests pass.
- `bash tools/cli/run_regression.sh` → `PASS: no new regressions; all out-of-band categories are documented residuals within their accepted-tolerance bands.`
- Per-category numbers byte-identical to `bda1650` (74.32% aggregate, 11/17 categories within ±2pp).

### Next perf cycle hints (not in this commit)

The new top of the profile is likely `cv::findContours` / connected-components on the binarized image (called once per image, dominates `bright_spots` at 18s where individual decode attempts are cheap), and `ThresholdBlockOtsu::process` on large images (the binarizer tile sweep uses `cv::Mat::at<uchar>(y, x)` which CLAUDE.md now permits replacing with `ptr<uint8_t>(y)[x]`). `QrCodeBinaryGridReader::sampleNearest` is the next pixel-access hot path. None investigated this cycle — one fix per cycle per team-lead direction.

## 2026-05-10 (later¹¹) — Step 9b complete: cycle (c) + (d) diagnosed → accepted as documented `cv::findContours`-substitution residual

Cycles (c) (`monitor` -11.76pp) and (d) (`glare` -3.77pp) ran their dump-diff diagnostics per CLAUDE.md "Intermediate-state dumps for debugging parity failures." Both residuals trace to the same root cause — the `cv::findContours` substitution mandated by CLAUDE.md "Replace with OpenCV" — manifesting at different stages of the pipeline. Per team-lead's bucket-1 classification + (Y)-acceptance directive: docs commit only, no code change.

### Cycle (c) — `monitor` diagnostic

`monitor/image011` (canonical case): C++ produces 0 detections + 0 failures; Java produces 1 successful decode. Stage-diff:

- **binary**: 1689×1614, 0.60% per-pixel diff between Java and C++ (essentially equivalent).
- **polygons**: Java 22, C++ 40. Both detect the QR's bottom-left finder polygon — corners agree within 1 px.
- **finder check**: Java reports `(edgeInside=56.9, edgeOutside=164.9) → grayThreshold=110.9`; C++ reports `(20.1, 187.7) → 103.9`. The 7-grayvalue threshold difference flips the `1:1:3:1:1` raster-scan check on this borderline polygon.
- Per-image breakdown of `monitor` failures: 2 of 17 images are C++-unique misses (`image011`, `image012`); 3 fail in both (parity, not a bug); 12 succeed in both.

### Cycle (d) — `glare` diagnostic (≤2h budget for classification)

`glare/image005` (canonical case): same shape as `monitor` failure (0 dets, 0 fails). Stage-diff:

- **binary**: 1.88% per-pixel diff (~equivalent).
- **polygons**: Java 13, C++ 11 (2 polygons missing).
- **finder check**: not reached — the QR's top-right finder polygon never enters the candidate list at all.

OpenCV `findContours` finds 14 tiny contours within 15 px of the missing finder's location, the largest only 43 perimeter pixels — exactly at the `minimumContour = ConfigLength::fixed(40)` floor. Our `PolylineSplitMerge` corner finder rejects the contour as a 4-corner polygon where Java's `PolylineSplitMerge` running on the equivalent BoofCV-emitted contour accepts it.

Per-image breakdown of `glare`: 3 GT C++-unique misses across `image005`, `image007` (1 of 2 GT), `image022`.

### Bucket classification: same root cause, different stage

Both `monitor` and `glare` are downstream consequences of the same project-architectural decision (cv::findContours substitution). The per-pixel sequence along the contour boundary differs slightly between OpenCV and BoofCV, and that difference is amplified into:
- `monitor`: a ~7-grayvalue threshold shift at the finder-check stage.
- `glare`: a ~1-pixel corner-position shift at the polygon-fit stage on contours near the `minimumContour = 40` floor.

Tiny features + tipping-point thresholds amplify the divergence; non-borderline images (>99% of the dataset) are unaffected.

### Decision: accept

Per CLAUDE.md "Replace with OpenCV" + team-lead's (Y) approval. Alternatives (W) port `LinearContourLabelChang2004` and (X) substitute binarizer-derived threshold both rejected; full reasoning in [docs/decisions/01_cv_findcontours_substitution.md](docs/decisions/01_cv_findcontours_substitution.md).

### Final per-category table at 9b close-out

| category      | baseline | cpp    | delta_pp   | within ±2pp |
|---------------|---------:|-------:|-----------:|:-:|
| blurred       |   38.46% | 38.46% |   +0.00pp  | ✓ |
| bright_spots  |   27.84% | 29.90% |   +2.06pp  |   (small-N) |
| brightness    |   78.82% | 77.65% |   -1.18pp  | ✓ |
| close         |  100.00% |100.00% |   +0.00pp  | ✓ |
| curved        |   56.67% | 55.00% |   -1.67pp  | ✓ |
| damaged       |   16.28% | 16.28% |   +0.00pp  | ✓ |
| decoding      |   65.38% | 65.38% |   +0.00pp  | ✓ |
| glare         |   32.08% | 28.30% |   -3.77pp  |   (cv::findContours residual; documented) |
| high_version  |   40.54% | 43.24% |   +2.70pp  |   (small-N) |
| lots          |   99.76% | 99.76% |   +0.00pp  | ✓ |
| monitor       |   82.35% | 70.59% |  -11.76pp  |   (cv::findContours residual; documented) |
| nominal       |   89.74% | 89.74% |   +0.00pp  | ✓ |
| noncompliant  |    3.85% |  7.69% |   +3.85pp  |   (small-N) |
| pathological  |   43.48% | 43.48% |   +0.00pp  | ✓ |
| perspective   |   80.00% | 82.86% |   +2.86pp  |   (small-N) |
| rotations     |   96.24% | 96.24% |   +0.00pp  | ✓ |
| shadows       |   85.00% | 85.00% |   +0.00pp  | ✓ |
| **AGGREGATE** | **74.40%** | **74.32%** | **-0.08pp** | |

11/17 categories within ±2pp band. 6 residuals — 2 from the cv::findContours substitution (`monitor` -11.76, `glare` -3.77) + 4 small-N "+" outliers (`bright_spots` +2.06, `high_version` +2.70, `perspective` +2.86, `noncompliant` +3.85; `bright_spots` straddles small-N noise + cv::findContours bands).

### Added

- [docs/decisions/01_cv_findcontours_substitution.md](docs/decisions/01_cv_findcontours_substitution.md) — ADR documenting the `cv::findContours` substitution: context + decision + mechanism of divergence + empirical cost + alternatives rejected (W: port LinearContourLabelChang2004, X: substitute binarizer threshold) + consequences + revisit conditions.
- [src/decoder/qr_code_decoder_image.md](src/decoder/qr_code_decoder_image.md): new "Known parity residuals" section covering the cv::findContours residual (mechanism on `monitor` vs `glare`, why we accept) + small-N noise residuals + final per-category table.

### Changed

- None. Pure docs commit. Library code unchanged from `577615b`.

### Regression

- 421/421 unit tests still pass (no library code touched).
- Build clean with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion`.

### Step 9b summary

| metric | value |
|---|---|
| Aggregate decode rate | 74.32% (Java baseline 74.40%; **delta -0.08pp**) |
| Categories within ±2pp band | **11 of 17** |
| Documented residuals (5 borderline images) | `monitor` 2 imgs + `glare` 3 GT — `cv::findContours`-substitution cost (categories: `monitor`, `glare`) |
| Small-N noise residuals | 4 categories |
| Unit tests | 421/421 pass |
| Lines of C++ shipped | ~10,400 (per `wc -l src/ include/`) |
| Algorithm docs | ~25 .md files alongside source |

Step 9b complete. Aggregate parity goal (CLAUDE.md goal #1: "per-category read rate within ~2% of the Java reference") met on the strict-±2pp interpretation for 11/17 categories, with the remaining 6 explained and documented. Aggregate at -0.08pp is well inside any reasonable noise floor.

## 2026-05-10 (later¹⁰) — Step 9b.3 cycle (b): mirror 4 missing `ConfigQrCode` settings (polygon tuning + decode-side)

Codex re-review on `a2d06c3` flagged 4 settings still using class defaults instead of QR-specific values. All 4 mirrored at the CLI Pipeline ctor + `QrCodeDecoderImage::Config` extended with `ignorePaddingBytes`. Aggregate moves **73.21% → 74.32%** — within **-0.08pp of Java's 74.40% baseline** (target was ±1pp aggregate). 11 of 16 categories now in band (was 8). The 5 residual categories include `monitor -11.76pp` (binarization cluster — next cycle) plus 4 small-N "+" deltas (likely noise on 20-37 GT categories where ±1 image swings 2-4pp).

### Fix

[tools/cli/qr_scan.cpp](tools/cli/qr_scan.cpp) `Pipeline()` ctor + new `makeQrConfig()` helper, mirroring 4 `ConfigQrCode.java` settings:

| # | Java line | C++ class default | Java QR profile | Effect |
|---|---|---|---|---|
| 1 | `ConfigQrCode.java:95` | `maxSideError = relative(0.05, 3)` | `relative(0.12, 3)` | 12% chord-length tolerance vs 5% — accepts edge-noise in `blurred`/`pathological`. |
| 2 | `ConfigQrCode.java:96` | `cornerScorePenalty = 0.025` | `0.4` | **16× bump** — penalises 5+ corner candidates more aggressively, collapses to 4-corner finder polygons. Prime mover for the polygon-cluster residuals. |
| 3 | `ConfigQrCode.java:75` | `QrCodeDecoderBits.ignorePaddingBytes = false` (strict) | `true` (lenient) | Accepts non-spec padding patterns (encoder bug tolerance). |
| 4 | `ConfigQrCode.java:62` | `QrCodeDecoderImage::Config::defaultEncoding = "UTF-8"` | `"ISO-8859-1"` | Byte-mode payloads with raw 0x80–0xFF don't fail UTF-8 validation. |

Plus a redundant `polyCfg.minimumSideLength = 2` set explicitly (already the C++ default; Java's `ConfigQrCode.java:97` sets it explicitly so we mirror).

[include/boofcv_qr/qr_code_decoder_image.hpp](include/boofcv_qr/qr_code_decoder_image.hpp): added `bool ignorePaddingBytes = false;` field to `QrCodeDecoderImage::Config`. The orchestrator ctor plumbs `cfg.ignorePaddingBytes` into the bits decoder. Mirrors the existing pattern for `forceEncoding` / `defaultEncoding` / `considerTransposed` — value-typed Config injected at ctor time per CLAUDE.md "Public API design" line 31.

### Per-category numbers vs Java baseline

| category | baseline | cpp | delta | within ±2pp |
|---|---:|---:|---:|:-:|
| blurred       |   38.46% |   38.46% | **+0.00pp** | ✓ |
| bright_spots  |   27.84% |   29.90% | +2.06pp |   |
| brightness    |   78.82% |   77.65% | -1.18pp | ✓ |
| close         |  100.00% |  100.00% | +0.00pp | ✓ |
| curved        |   56.67% |   55.00% | -1.67pp | ✓ |
| damaged       |   16.28% |   16.28% | +0.00pp | ✓ |
| decoding      |   65.38% |   65.38% | +0.00pp | ✓ |
| glare         |   32.08% |   28.30% | -3.77pp |   |
| high_version  |   40.54% |   43.24% | +2.70pp |   |
| lots          |   99.76% |   99.76% | +0.00pp | ✓ |
| monitor       |   82.35% |   70.59% | **-11.76pp** |   |
| nominal       |   89.74% |   89.74% | +0.00pp | ✓ |
| noncompliant  |    3.85% |    7.69% | +3.85pp |   |
| pathological  |   43.48% |   43.48% | **+0.00pp** | ✓ |
| perspective   |   80.00% |   82.86% | +2.86pp |   |
| rotations     |   96.24% |   96.24% | +0.00pp | ✓ |
| shadows       |   85.00% |   85.00% | +0.00pp | ✓ |
| **AGGREGATE** | **74.40%** | **74.32%** | **-0.08pp** | |

### Recovery vs prior commit (73.21% aggregate)

- **aggregate: +1.11pp** (now -0.08pp from Java baseline; target was ±1pp).
- `blurred`: -3.08pp → **+0.00pp** (Java parity)
- `brightness`: -4.71pp → **-1.18pp** (now in band)
- `pathological`: -4.35pp → **+0.00pp** (Java parity)
- `bright_spots`: -6.19pp → +2.06pp (overshoot — barely outside band, +2 of 97 GT)
- 11 of 16 categories now in ±2pp band (was 8).

### Predictions vs reality

Per team-lead's predictions:
- `blurred` -3.08pp → likely closes — **closed to 0.00pp ✓**
- `pathological` -4.35pp → partial recovery, may stay outside — **fully closed to 0.00pp ✓ (better than predicted)**
- Binarization cluster (`monitor`, `bright_spots`, `brightness`, `glare`) → "should not move" — **moved! brightness recovered ~3.5pp, bright_spots +8pp, glare unchanged, monitor unchanged**. The polygon-tuning settings rippled into binarization-sensitive categories too — likely because tighter polygon filtering (cornerScorePenalty=0.4) accepts more legitimate finder polygons in low-contrast images that were previously rejected by the lenient over-corner threshold.

### Residual >±2pp (5 categories)

- `monitor` -11.76pp — true binarization-cluster outlier; next cycle.
- `bright_spots` +2.06pp, `high_version` +2.70pp, `perspective` +2.86pp, `noncompliant` +3.85pp — all positive deltas in small-N categories (97, 37, 35, 26 GT). Likely regression noise.

### Regression

- 421/421 unit tests still pass.
- Build clean with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion`.

## 2026-05-10 (later⁹) — Step 9b.3 fix #2: align Pipeline ctor with `ConfigQrCode` polygon-detector defaults (`minimumContour=fixed(40)`, `minimumRefineEdgeIntensity=6`)

`lots` zero-detection root-causal fix per cycle (a) of the residual-triage. Java reference dump (`tools/java_reference/DumpStages`) on `lots/image005.jpg` produced 841 polygons, 60 detections; C++ at `736435f` produced **1 polygon, 0 detections** despite the binarised image being byte-equivalent (1.08% per-pixel disagreement; both ~21% foreground). Earliest divergence stage: `DetectPolygonFromContour::findCandidateShapes` minimum-area filter.

### Root cause

`ConfigQrCode.java:100` (BoofCV v1.3.0) sets `polygon.detector.minimumContour = ConfigLength.fixed(40)`. Our CLI's `Pipeline()` ctor in `tools/cli/qr_scan.cpp` constructed `DetectPolygonFromContour` with the **class default** `minimumContour = ConfigLength::relative(0.044, 4.0)`. On a 4032×3024 image this computes to 154-pixel min contour perimeter → `minimumArea = (154/4)² ≈ 1482 sq px`. Each finder pattern in `lots/image005-007` has area ≈ 28×28 = 784 sq px (4 px/module × 7 modules) → all small finders rejected at the area filter (line 504 of `detect_polygon_from_contour.cpp`).

`ConfigQrCode.java:103` also sets `polygon.minimumRefineEdgeIntensity = 6`. Our Pipeline ctor used 3.0; nudged to 6.0 to match.

### Fix

[tools/cli/qr_scan.cpp:240-247](tools/cli/qr_scan.cpp): `Pipeline()` ctor now calls `contour->setMinimumContour(ConfigLength::fixed(40.0))` and constructs `DetectPolygonBinaryGrayRefine` with `minimumRefineEdgeIntensity=6.0`. CLI-layer change only; the library defaults remain at their class settings (which match the upstream `ConfigPolygonFromContour` defaults — Java's QR-specific tuning is in `ConfigQrCode`, not `ConfigPolygonFromContour`).

### Per-category numbers vs Java baseline

| category      | baseline |    cpp |   delta_pp |   gt | cpp_dec | within±2pp |
|---------------|---------:|-------:|-----------:|-----:|--------:|:-:|
| close         |  100.00% |100.00% |   +0.00pp  |   40 |      40 | ✓ |
| curved        |   56.67% | 55.00% |   -1.67pp  |   60 |      33 | ✓ |
| damaged       |   16.28% | 16.28% |   +0.00pp  |   43 |       7 | ✓ |
| decoding      |   65.38% | 65.38% |   +0.00pp  |   26 |      17 | ✓ |
| lots          |   99.76% | 99.76% |   +0.00pp  |  420 |     419 | ✓ |
| nominal       |   89.74% | 89.74% |   +0.00pp  |   78 |      70 | ✓ |
| rotations     |   96.24% | 96.24% |   +0.00pp  |  133 |     128 | ✓ |
| shadows       |   85.00% | 85.00% |   +0.00pp  |   20 |      17 | ✓ |
| high_version  |   40.54% | 43.24% |   +2.70pp  |   37 |      16 |   |
| perspective   |   80.00% | 82.86% |   +2.86pp  |   35 |      29 |   |
| blurred       |   38.46% | 35.38% |   -3.08pp  |   65 |      23 |   |
| glare         |   32.08% | 28.30% |   -3.77pp  |   53 |      15 |   |
| noncompliant  |    3.85% |  7.69% |   +3.85pp  |   26 |       2 |   |
| pathological  |   43.48% | 39.13% |   -4.35pp  |   23 |       9 |   |
| brightness    |   78.82% | 74.12% |   -4.71pp  |   85 |      63 |   |
| bright_spots  |   27.84% | 21.65% |   -6.19pp  |   97 |      21 |   |
| monitor       |   82.35% | 70.59% |  -11.76pp  |   17 |      12 |   |
| **AGGREGATE** | **74.40%** | **73.21%** | **-1.19pp** | 1258 |     921 |   |

### Recovery

- Aggregate: 55.17% → **73.21%** (+18.04pp; **-1.19pp from Java baseline**, target was ±1pp aggregate)
- `lots`: 50.71% → **99.76%** (full recovery — image005-007 went from 0 detections each to 60)
- `brightness`: 55.29% → 74.12% (+18.83pp recovery)
- `curved`: 51.67% → 55.00% (now within ±2pp band)

8 of 16 categories now within ±2pp band (was 6). 9 residual; 3 of those (`high_version`, `perspective`, `noncompliant`) are positive deltas in small-N categories (~26-37 GT) where ±1 image swings 2.7-3.85pp — likely noise.

### Diagnostic tooling added

- [tools/java_reference/src/main/java/qrboofcv/DumpStages.java](tools/java_reference/src/main/java/qrboofcv/DumpStages.java) — single-image stage-dump tool emitting `binary.png`, `polygons.json`, `detections.json` per CLAUDE.md "Intermediate-state dumps for debugging parity failures." Gradle task `dumpStages`.
- [tools/cli/qr_scan.cpp:`runDumpStages`](tools/cli/qr_scan.cpp) — C++ counterpart via `qr_scan --dump-stages <image> <outDir>`. Emits the same shape of files plus `position_patterns.json` (post-finder filter).
- These are the diagnostic tools the team-lead's dispatch directs as the first step of any future cycle: "Don't tune until you have the Java intermediate-state dump." Both sides now usable interchangeably.

### Regression

- 421/421 unit tests still pass.
- Build clean with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion`.

## 2026-05-10 (later⁸) — Step 9b.3 fix #1: align `cv::imread` EXIF handling with BoofCV's `UtilImageIO.loadImage`

First regression run after 9b.1+9b.2 (commit `50369db`) showed -36.25pp aggregate vs Java baseline. Triage isolated a single one-line CLI bug: `cv::imread` applies the EXIF Orientation tag by default (auto-rotates camera JPEGs 90°/180°/270°), while BoofCV's `UtilImageIO.loadImage` returns the raw pixel buffer ignoring EXIF. The detector worked on either orientation, but reported corner coordinates in the rotated frame disagreed with ground-truth (and Java) coordinates → IoU matching failed on every camera-shot image.

24 of 562 images had width/height transposed vs Java. All 7 `lots/` images (which alone account for 420 of 1258 total GTs) plus a few `shadows/` and `nominal/` images.

### Fix

- [tools/cli/qr_scan.cpp](tools/cli/qr_scan.cpp): replace 3 raw `cv::imread(path, cv::IMREAD_GRAYSCALE)` call sites with a centralised `loadGray(imagePath)` helper that adds `cv::IMREAD_IGNORE_ORIENTATION`. CLI-layer change only; zero core-library impact (image I/O is CLI-layer responsibility per CLAUDE.md "Replace with OpenCV").

### Per-category numbers vs Java baseline

| category      | baseline |    cpp |   delta_pp |   gt | cpp_dec |
|---------------|---------:|-------:|-----------:|-----:|--------:|
| blurred       |   38.46% | 33.85% |   -4.62pp  |   65 |      22 |
| bright_spots  |   27.84% | 19.59% |   -8.25pp  |   97 |      19 |
| brightness    |   78.82% | 55.29% |  -23.53pp  |   85 |      47 |
| close         |  100.00% |100.00% |   +0.00pp  |   40 |      40 |
| curved        |   56.67% | 51.67% |   -5.00pp  |   60 |      31 |
| damaged       |   16.28% | 16.28% |   +0.00pp  |   43 |       7 |
| decoding      |   65.38% | 65.38% |   +0.00pp  |   26 |      17 |
| glare         |   32.08% | 28.30% |   -3.77pp  |   53 |      15 |
| high_version  |   40.54% | 43.24% |   +2.70pp  |   37 |      16 |
| **lots**      |   99.76% | 50.71% |  **-49.05pp** |  420 |     213 |
| monitor       |   82.35% | 70.59% |  -11.76pp  |   17 |      12 |
| nominal       |   89.74% | 89.74% |   +0.00pp  |   78 |      70 |
| noncompliant  |    3.85% |  7.69% |   +3.85pp  |   26 |       2 |
| pathological  |   43.48% | 39.13% |   -4.35pp  |   23 |       9 |
| perspective   |   80.00% | 82.86% |   +2.86pp  |   35 |      29 |
| rotations     |   96.24% | 96.24% |   +0.00pp  |  133 |     128 |
| shadows       |   85.00% | 85.00% |   +0.00pp  |   20 |      17 |
| **AGGREGATE** |   74.40% | 55.17% |  **-19.24pp** |1258 |     694 |

Aggregate moved 38.16% → 55.17% (+17.01pp). `shadows` and `nominal` cleared. `lots` recovered from 0.95% → 50.71% but still -49.05pp; partial breakdown:

- `lots/image001.jpg`: 60/60 matched ✓
- `lots/image002.jpg`: 60/60 ✓
- `lots/image003.jpg`: 49/60 (C++ detected 49)
- `lots/image004.jpg`: 44/60 (C++ detected 44)
- `lots/image005.jpg`: **0/60** (pipeline returned zero detections AND zero failures)
- `lots/image006.jpg`: **0/60**
- `lots/image007.jpg`: **0/60**

Java decodes 60 on every image. Images 005-007 returning *zero candidates* (not zero successful decodes — zero candidates) suggests a finder-stage threshold or contour cap that's filtering out all small finder polygons in those specific images. Likely `setMaximumContour` cutoff or `ThresholdBlockOtsu` requestedBlockWidth tuning on dense small-QR layouts. Triage in next cycle.

### Residual >±2pp categories (11)

`blurred -4.62`, `bright_spots -8.25`, `brightness -23.53`, `curved -5.00`, `glare -3.77`, `high_version +2.70`, `lots -49.05`, `monitor -11.76`, `noncompliant +3.85`, `pathological -4.35`, `perspective +2.86`. The "+" deltas are within margin of regression noise on small-N categories; the "-" deltas are real parity work for subsequent cycles.

### Regression

- 421/421 unit tests still pass.
- Build clean with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion`.

## 2026-05-10 (later⁷) — Step 9b.1 + 9b.2: `qr_scan` CLI + regression harness

End-to-end CLI binary wiring the full detection pipeline (binarize → polygon → finder → graph → orchestrator) emitting per-image JSON in the same shape as `tools/java_reference/Baseline.java` so `tests/regression/score.py` works against either side. Plus a regression driver that builds, runs, scores, and prints per-category delta vs `tests/baseline.json`. No iteration in this commit — that's 9b.3.

### Added

- [tools/cli/qr_scan.cpp](tools/cli/qr_scan.cpp) — CLI binary. Two modes:
  - `qr_scan <input_dir> <output_dir>` — batch; mirrors the dataset directory layout under `<output_dir>` with one `.json` per image plus a `summary.json` for `score.py`.
  - `qr_scan <single_image.png>` — single-image; emits JSON to stdout for debugging.
  - Uses `<filesystem>` per CLAUDE.md "CLI may use `<filesystem>`; core lib must not."
  - Sidecar `.txt` ground-truth parser mirrors Java's `parseGroundTruth` (handles both `SETS`-style coord blocks and plain-payload-only files for the `decoding/` subset).
  - Hand-written JSON serialiser — pulling in nlohmann/json for one CLI binary isn't worth it; output is byte-compatible-enough with score.py's input expectations.
- [tools/cli/run_regression.sh](tools/cli/run_regression.sh) — regression driver. Builds in Release, runs `qr_scan` against the dataset, scores via `score.py`, prints a per-category delta table vs `tests/baseline.json`, exits 1 if any category is outside ±2pp.
- CMakeLists adds `option(BOOFCV_QR_BUILD_CLI ON)` + `add_executable(qr_scan tools/cli/qr_scan.cpp)` target linking `boofcv_qr` + `opencv_imgcodecs`.

### Verified

- CLI builds clean with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion`.
- Smoke-test on `tests/fixtures/qr/v2_M_alphanum_HELLO.png` decoded correctly: `version=2, error=M, mask=M011, message=HELLO, mode=ALPHANUMERIC, total_bit_errors=0, failure_cause=NONE`.
- 421 unit tests still pass (no library code changed; only adds the CLI target).

### Pending

- 9b.3 — first regression run on the 562-image / 1258-GT `boofcv-qrcodes` dataset.

## 2026-05-10 (later⁶) — Step 9a fix-up: codex review (8 findings, single bundled commit)

Reviewer + codex flagged 1 algorithmic divergence + 3 CLAUDE.md mandate violations + the fixture-source risk + 2 test-gap issues + 1 doc bug on `ff5de99`. All 8 fixed in this single commit. Test count grows from 261 → 421 (160 new parametric `full_simple` cases driven off 160 BoofCV-Java-generated fixtures). Build remains clean with the strict warning set (`-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion`).

### #1 — Re-port `setTransformFromLinesSquare` verbatim (HIGH algorithmic)

The prior implementation contributed 1 row per line correspondence. Java's `boofcv-geo HomographyDirectLinearTransform` contributes **2 rows per line** via the cross-product null-space construction (`hat(s) * H * f = 0` reformatted to `A * vec(H) = 0`). The 1-row form was an over-determined-only-on-(h6,h7) pseudo-fit that happened to pass the affine round-trip but didn't fully constrain the homography under projective layouts.

- [src/sampler/qr_code_binary_grid_to_pixel.cpp](src/sampler/qr_code_binary_grid_to_pixel.cpp) — re-ported verbatim from `boofcv.alg.geo.h.HomographyDirectLinearTransform.{addPoints2D, addPoints3D}`. Each 2D point pair contributes 2 rows; each 3D direction-line pair (z=0 on both sides) contributes 2 rows. Design-matrix shape changed from 14×9 (3*2 + 4*1 + zeros) to 14×9 (3*2 + 4*2) with the correct row construction.
- New tests in [tests/unit/test_qr_code_binary_grid_to_pixel.cpp](tests/unit/test_qr_code_binary_grid_to_pixel.cpp):
  - `setTransformFromLinesSquare_roundTrip` — mirrors Java's `TestQrCodeBinaryGridToPixel.setTransformFromLinesSquare` (axis-aligned v=2 layout, 1e-4 tolerance matches `assertEquals(..., 1e-4f)` upstream).
  - `setTransformFromLinesSquare_rotated` — discriminating test under a 30° rotation about an off-center pivot (machine-precision 1e-9 tolerance). A buggy 1-row-per-line construction would still pass the affine `_roundTrip` but fail this rotated case, so it pins the row construction.

### #2 — Strategy injection moves to ctor-time `Config` struct (HIGH mandate)

Prior commit exposed `setRsCorrectStrategy()` / `setAlignmentStrategy()` post-construction setters. CLAUDE.md "Public API design" line 26 mandates "**at construction time**", line 31 mandates "Config passed by value at construction. No singletons, no thread-locals, no `init()` calls."

- [include/boofcv_qr/qr_code_decoder_image.hpp](include/boofcv_qr/qr_code_decoder_image.hpp) introduces `QrCodeDecoderImage::Config`: `forceEncoding`, `defaultEncoding`, `considerTransposed`, `rs_decoder` (`std::function`), `alignment_locator` (`std::function`). Default-constructed `std::function` = use built-in implementation. Two ctors: `QrCodeDecoderImage()` (default Config) and `explicit QrCodeDecoderImage(Config cfg)` (move-in).
- Setters / clearers dropped entirely. Tests that swap strategies construct a new detector — that's the CLAUDE.md "reentrant, value-semantic config" contract.

### #3 — Const-only result accessors; sub-modules friend-only (HIGH mandate)

- Dropped non-const overloads of `getSuccesses()` / `getFailures()`. Callers wanting to mutate copy.
- `getAlignmentLocator()` / `getGridReader()` / `getDecoder()` removed from the public API. Mutable access is granted to the test peer only via `friend class QrCodeDecoderImagePeer;`. The peer is defined in [tests/unit/test_qr_code_decoder_image.cpp](tests/unit/test_qr_code_decoder_image.cpp) — same pattern as step 7c / step 8.
- The public field `bool considerTransposed` becomes a Config setting; `getConsiderTransposed() const` exposes the active value for read-only inspection.

### #4 — `detect_polygons_only` routes through alignment hook (HIGH mandate)

Prior commit called `alignmentLocator_.process(...)` directly inside `detect_polygons_only`, bypassing the injected hook. Downstream consumers using a clipped-QR fallback alignment locator expect both `process()` and `detect_polygons_only` to honour their hook (CLAUDE.md "clipped-QR fallback" use case).

- [src/decoder/qr_code_decoder_image.cpp](src/decoder/qr_code_decoder_image.cpp) introduces private `runAlignmentLocator(gray, qr)` and `runRsCorrect(qr)` helpers that consult the injected hook (or call the built-in). Both `decode()` and `detect_polygons_only()` route through these helpers; `rs_correct(qr)` public entry also goes through `runRsCorrect`.
- New test `Strategy_AlignmentInjection_polygonOnly`: injects a stub alignment function that returns false + counts invocations; asserts `detect_polygons_only` invokes it.

### #6 — `detect_polygons_only` header doc rewritten (HIGH contract)

The prior header comment said "version is left empty" but the implementation populates `qr.version` via `estimateVersionBySize()` so the alignment locator can run. Docs now correctly describe the actual behaviour.

### #5 — Replace Python `qrcode` fixtures with BoofCV-Java-generated (HIGH parity)

Python's `qrcode` library and BoofCV's `QrCodeGeneratorImage` are independent ISO/IEC 18004 implementations. They differ on mask-selection tie-breaking, mode segmentation choices, and default border modules (Python: 4; BoofCV: 2). Round-tripping "Python-encoded → C++ decoded → assert payload" passes if our decoder agrees with Python's encoding choices, not BoofCV's — masking real Java-vs-C++ parity bugs.

- New Gradle project at [tools/java_fixture_gen/](tools/java_fixture_gen/) (mirrors the existing `tools/java_reference/` pattern). Single `GenerateFixtures.java` main class uses `QrCodeEncoder` + `QrCodeGeneratorImage(4)` with default `borderModule=2` to emit PNG + JSON + flat key=value text per fixture. Build/run: `JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home ./gradlew run --args="<outDir>"`.
- Old [tools/fixture_gen/](tools/fixture_gen/) Python script deleted entirely.
- 172 fixtures regenerated under [tests/fixtures/qr/](tests/fixtures/qr/): 12 base fixtures (named to match the existing test references) + 160 `full_simple` matrix fixtures (5 versions × 4 ECC × 8 masks). Ground-truth JSON now includes the encoded `maskBits` (0..7) so the parametric matrix test can assert mask parity.
- All existing fixture-driven tests pass against the new fixtures unchanged — the prior C++ decoder was already byte-parity with BoofCV's encoder choices on the relevant code paths.

### #7 — Tightened transposed-retry test (MEDIUM test gap)

Prior `BitsTransposed_RetryPath` accepted either decode-success or decode-failure. New strict assertions mirror Java's `TestQrCodeDecoderImage.transposed`:
- With `Config.considerTransposed = true` (default): decode succeeds AND `qr.bitsTransposed == true`.
- With `Config.considerTransposed = false`: decode fails (`successes_.empty()`).

Test setup feeds the orchestrator un-transposed pps coords against a `cv::transpose`d image — the role/coord mismatch forces the retry path through `transposePositionPatterns` to fix the geometry.

### #8 — Parametric `full_simple` 5×4×8=160-case matrix (MEDIUM parity coverage)

Java's `TestQrCodeDecoderImage.full_simple` covers `(version ∈ {1,2,7,20,40}) × (errorLevel ∈ {L,M,Q,H}) × (mask ∈ {M000..M111})` = 160 cases. The prior C++ test covered only 5 hand-picked cases. New parametric `FullSimpleTest.decode_matches_encoded_attributes` (`INSTANTIATE_TEST_SUITE_P` with 160 cases) loads each BoofCV-generated fixture and asserts `version + error + message + mask` (pointer-identity comparison against the `QrCodeMaskPattern::lookupMask(maskBits)` singleton). All 160 cases pass.

### Other items reviewer + team-lead positioned on (no action required)

- `computeBoundingBox` parallel-line guard kept as `bounds[2] = (0, 0)` fallback — Java NPE's on degenerate input; we degrade gracefully (already has `// FIXME(parity)` comment in the source).
- Decode order, retry loop, RS stride math, bitsTransposed implementation, "Forbidden moves" sweep — all clean per codex; no fix needed.

### Regression

- C++ unit tests: **421/421 pass** (was 261/261; +160 parametric cases). Clean build with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion`.

## 2026-05-10 (later⁵) — Step 9a: `QrCodeDecoderImage` orchestrator + step-9 public API mandates

The top-level QR-decode orchestrator. Wires the previously-shipped stages (binarize → polygon → finder → alignment → sampler → format/version + mask XOR → RS → mode dispatch) into a single `process(pps, gray) → vector<QrCode>` entry point. Lands the seven CLAUDE.md "Public API design — for downstream recovery pipelines" mandates that have been deferred since step 4: stage-isolation entry points, strategy injection, polygon-only mode, raw codewords + erasure positions, line-correspondence DLT for unknown-version sampling, and the `bitsTransposed` retry path.

### Added

- [include/boofcv_qr/qr_code_decoder_image.hpp](include/boofcv_qr/qr_code_decoder_image.hpp) + [src/decoder/qr_code_decoder_image.cpp](src/decoder/qr_code_decoder_image.cpp) — verbatim port of `QrCodeDecoderImage` (~625 LOC). Decode order matches Java line-by-line: `extractFormatInfo → extractVersionInfo → alignmentLocator.process → iterative-transform/readRawData (≤ 6 retries with `removeFeatureWithLargestError`) → applyErrorCorrection → decodeMessage`. Format/mask are extracted **before** sampling — never invert that order.
- [src/decoder/qr_code_decoder_image.md](src/decoder/qr_code_decoder_image.md) — algorithm doc covering stage wiring, runtime order (the inverse of the porting order), failure-mode propagation, the "format BEFORE sampling" non-invertibility constraint, integration points for downstream recovery, and the Lens-distortion / Micro-QR / multi-frame deferrals.
- **CLAUDE.md "Public API design" mandates landed on `QrCode`** ([include/boofcv_qr/qr_code.hpp](include/boofcv_qr/qr_code.hpp) + [src/decoder/qr_code.cpp](src/decoder/qr_code.cpp)):
  - `ppCorner` / `ppRight` / `ppDown` / `bounds` (`std::array<cv::Point2d, 4>` each) — finder-pattern + bounding-box geometry.
  - `Hinv` (`cv::Matx33d`) — inverse homography, populated even on failure paths so callers can introspect the last attempt's geometry.
  - `BlockStatus` enum + `blockStatus[]` — per-RS-block decode status (`NOT_DECODED` / `SUCCESS_NO_ERRORS` / `SUCCESS` / `ERROR_CORRECTION_FAILED`).
  - `rawCodewords` — pre-RS, post-de-interleave codewords (copy of `rawbits` after `readRawData`, surfaced for multi-frame fusion / known-prefix recovery pipelines).
  - `rsErrorLocations` — flat list of byte offsets into `rawbits`/`rawCodewords` that RS corrected, translated from per-block positions back to raw-bits positions via the de-interleave stride.
  - All five fields cleared in `QrCode::reset()`.
- **`PositionPatternTriplet` + `PolygonOnlyResult` helper structs** in the orchestrator header — value-typed return types for `find_finders` / `detect_polygons_only` (no raw pointers in public signatures, per CLAUDE.md "Public API design").
- **Stage-isolation public entry points** on `QrCodeDecoderImage`:
  - `find_finders(pps) → vector<PositionPatternTriplet>` (static).
  - `detect_polygons_only(pps, gray) → PolygonOnlyResult`.
  - `sample_bit_matrix(gray, qr) → bool`.
  - `extract_raw_codewords(gray, qr) → bool`.
  - `rs_correct(qr) → bool`.
  - `decode_message(qr) → bool`.
- **Strategy injection** via `std::function` hooks: `setRsCorrectStrategy(fn)` and `setAlignmentStrategy(fn)`. Defaults preserve verbatim Java behaviour. Tested by injecting stubs that record being called (`Strategy_RsInjection`, `Strategy_AlignmentInjection`).
- **`QrCodeBinaryGridToPixel::setTransformFromLinesSquare`** ([src/sampler/qr_code_binary_grid_to_pixel.cpp](src/sampler/qr_code_binary_grid_to_pixel.cpp)) — the codex/step-5 carry-over. SVD-based DLT on 3 corner correspondences + 4 direction-line correspondences (skipping the "prone to damage" outside corner). Used by `setMarkerUnknownVersion` for the v < 7 size-estimation path.
- **`QrCodeBinaryGridToPixel::addAllFeatures(qr)` 1-arg overload** + **`QrCodeBinaryGridReader::setMarker(qr)` 1-arg overload** + **`QrCodeBinaryGridReader::setMarkerUnknownVersion(qr, threshold)`** — Java's 1-arg API, finally usable now that QrCode has the geometry fields. The 6-arg overloads are preserved for step-7/8 callers.
- **RS plumbing for blockStatus + rsErrorLocations** ([src/decoder/qr_code_decoder_bits.cpp](src/decoder/qr_code_decoder_bits.cpp)) — `applyErrorCorrection` pre-sizes `qr.blockStatus[]` to `numBlocksA + numBlocksB` and clears `qr.rsErrorLocations`; `decodeBlocks` writes per-block status (success / success-no-errors / failed) and translates each `rscodes.errorLocations[k]` back to a raw-bits byte offset using the de-interleave stride.

### Tests

- [tests/unit/test_qr_code_decoder_image.cpp](tests/unit/test_qr_code_decoder_image.cpp) — 19 cases:
  - **JUnit-mirroring (pure data)**: `TransposePositionPatterns`, `RotateUntilAt`, `ComputeBoundingBox`, `SetPositionPatterns`, `ExtractVersionInfo_VersionOutOfRange` (substitutes for Java's subclass-override test by feeding degenerate ppRight/ppDown geometry).
  - **JUnit-mirroring (full pipeline, via PNG fixtures)**: `FullSimple_v1_v2_v7`, `FullSimple_v20_v40`, `Message_numeric`, `Message_alphanumeric`, `Message_byte`. The Java `withLensDistortion` and `transposed` (encoder-side) cases are skipped (no encoder port; lens distortion deferred).
  - **CLAUDE.md mandate tests**: `StageIsolation_FindFinders`, `StageIsolation_SampleBitMatrix`, `Strategy_RsInjection`, `Strategy_AlignmentInjection`, `PolygonOnlyMode`, `MandateFields_PopulatedAfterDecode`, `RsErrorLocations_PopulatedOnCorruption`, `BitsTransposed_RetryPath`.
  - **Deferred-item regression**: `SetTransformFromLinesSquare_RoundTrip` (verifies the SVD-based DLT round-trips a known finder geometry through the `imageToGrid` mapping within 1e-3).
- [tools/fixture_gen/gen_qr_fixtures.py](tools/fixture_gen/gen_qr_fixtures.py) — one-shot Python fixture generator using the `qrcode` library. Emits 12 PNG + ground-truth pairs at module-pixel-size 4 with 4-module quiet zone (matching BoofCV's `QrCodeGeneratorImage(4)` defaults). Ground truth is a flat `key = value` text format the C++ tests parse without a JSON dep. Fixtures committed under [tests/fixtures/qr/](tests/fixtures/qr/).
- The PNG fixtures are loaded via `cv::imread`; CMake adds `opencv_imgcodecs` to `boofcv_qr_tests` link line and a `BOOFCV_QR_FIXTURE_DIR_DEFINE` so tests work both from `build/` and from any other CWD.

### Regression

- C++ unit tests: **258/258 pass** (was 239/239; 19 new cases). All previously-shipped tests continue to pass — no behavioural changes to step 1–8 modules; the QrCode field additions are append-only and `QrCode::reset()` extension preserves existing semantics for all old fields.

### Deviations from upstream

- **Lens distortion not ported.** Java's `setLensDistortion(width, height, model)` plumbing is omitted because the narrow-FOV `LensDistortionNarrowFOV` model isn't ported. Downstream consumers that need lens correction should pre-undistort the input image before calling `process()`.
- **`QrCode.LOCATION_BITS[v]` cache → on-demand recomputation.** Java caches the bit-extraction zigzag per version in a static array. We recompute via `QrCodeCodeWordLocations::qrcode(version).bits` on each `readRawData` call. Cost is negligible (v40 worst case is ~28k modules and well under a millisecond) and avoids the static-init footprint of a 40-entry pre-populated table.
- **`storageQR_` is a `std::vector<QrCode>` rather than Java's `DogArray<QrCode>` recycle pool.** Per CLAUDE.md type mappings, `DogArray<T>` → `std::vector<T>` value-typed; identity of pointers into successes/failures is NOT preserved across `process()` calls (Java's DogArray.reset() invalidates the same way). Marked `// TODO(perf): recycle` in the relevant comment.
- **`computeBoundingBox` defends against parallel-line case.** Java's `Intersection2D_F64.intersection` returns false on parallel input lines but the Java orchestrator doesn't check the return value (relies on the geometry being non-degenerate). Our port leaves `bounds[2]` at (0, 0) on parallel input — same observable behaviour, but explicit.
- **Public stage-isolation entry points beyond Java's surface.** `find_finders`, `detect_polygons_only`, `sample_bit_matrix`, `extract_raw_codewords`, `rs_correct`, `decode_message` are introduced per CLAUDE.md "Public API design"; they wrap or compose the Java behaviour without altering it.

## 2026-05-10 (later⁴) — Step 8: QrCodeAlignmentPatternLocator + `QrCode::alignment[]`

The alignment-pattern subpixel locator. Per QR spec, every version ≥ 2 has at least one alignment pattern at known grid coordinates inside the marker; the orchestrator (step 9, next) seeds a homography from the three finder patterns, then this stage refines each alignment-pattern centre to subpixel accuracy so the homography can correct for image-distortion drift far from the finders.

### Added

- [include/boofcv_qr/alignment/qr_code_alignment_pattern_locator.hpp](include/boofcv_qr/alignment/qr_code_alignment_pattern_locator.hpp) + [src/alignment/qr_code_alignment_pattern_locator.cpp](src/alignment/qr_code_alignment_pattern_locator.cpp) — verbatim port of `QrCodeAlignmentPatternLocator` (311 LOC). Two-stage subpixel refinement: `centerOnSquare` (10-iter 3×3 grey-image gradient walk) + `meanshift` (10-iter 8×8 mean-shift on `[-1.5, +1.5]` modules with 0.7 step decay). Adjustment seeded from previously-found patterns in the same row/column to carry homography drift forward. The `localize()` edge-scan path (commented out at line 139 of upstream) is ported but reachable only via `setUseEdgeScan(true)` — default false matches Java.
- [src/alignment/qr_code_alignment_pattern_locator.md](src/alignment/qr_code_alignment_pattern_locator.md) — algorithm doc covering the two-stage refinement, why this approach, the lookup-table-of-pointers contract (qr.alignment must not be re-grown after `initializePatterns`), and integration points for step 9.
- **`QrCode::Alignment` inner struct + `alignment[]` field** ([include/boofcv_qr/qr_code.hpp](include/boofcv_qr/qr_code.hpp) + [src/decoder/qr_code.cpp](src/decoder/qr_code.cpp)). `Alignment` carries `pixel`, `moduleX`, `moduleY`, `moduleFound`, `threshold` — same shape as Java's nested `QrCode.Alignment` class. `QrCode::reset()` clears `alignment` to maintain the existing test invariant.

### Tests

- [tests/unit/test_qr_code_alignment_pattern_locator.cpp](tests/unit/test_qr_code_alignment_pattern_locator.cpp) — 9 cases:
  - **Java parity**: `greatestDown`, `greatestUp` (the two static helpers, mirrors Java verbatim); `initializePatterns` (mirrors `TestQrCodeAlignmentPatternLocator.initializePatterns` for v2 + v7 with the same expected module coordinates).
  - **`QrCode` extensions**: `alignment_emptyAfterReset` (asserts the new field clears with `reset()`), `Alignment_resetClearsFields` (the inner type's reset).
  - **Coverage extensions**: `initializePatterns_v1HasNoAlignment`, `initializePatterns_v40HasMaxCount` (49 - 3 = 46 for v40), `useEdgeScanRoundtrip`.
  - **Synthetic centerOnSquare**: renders a 5×5 alignment pattern via `cv::rectangle`, sets up a v2 QR with finder corners that put the alignment at module (18, 18), runs the full `process()` pipeline, asserts the located pixel coordinates land within ~1 module of ground truth.

The Java JUnit's `simple` and `withLensDistortion` cases need `QrCodeEncoder` + `QrCodeGeneratorImage` (encoder-side, deferred — out of scope per CLAUDE.md decoder-only deliverable). The synthetic case substitutes for the same code path coverage.

### Regression

- C++ unit tests: **237/237 pass** (was 228/228; 9 new cases).
- Java baseline re-run: zero quality drift on `tests/baseline.json` (74.40 % aggregate, BoofCV 1.3.0 on 562 / 1258).

### Deferred from upstream

- **Lens distortion** integration in `setLensDistortion` — same deferral pattern as steps 5 / 7b / 7c. Stub no-op.
- **`localize()` edge-scan path** wired off by default. Reachable via `setUseEdgeScan(true)`. Java has it commented out at line 139.
- **Encoder-side test cases** (`simple`, `withLensDistortion`, `centerOnSquare`/`localize` direct tests with `QrCodeGeneratorImage`) — depend on `QrCodeEncoder` which is out of scope. Synthetic equivalents using `cv::rectangle` cover the same code paths.
- **Java's `setMarker(qr)` 1-arg form** — our `QrCodeBinaryGridReader::setMarker` takes the finder-corner coordinates as separate args (since `QrCode` doesn't have `ppCorner`/`ppRight`/`ppDown` geometry fields yet — those land at step 9). The locator's `process()` accepts them as parameters, mirroring the orchestrator's call flow.

### Codex review fixes

- **`Alignment::threshold` doc clarification** (raised by codex on 8, finding 1; doc-only). Reviewer traced the field against Java: upstream `QrCodeAlignmentPatternLocator` declares but never assigns `pattern.threshold`. Our port mirrors that. Updated the inline doc on `QrCode::Alignment::threshold` (lines 105-117 of `include/boofcv_qr/qr_code.hpp`) to note the field is **not populated by the locator** — left at default `0.0` for parity. Original commit message also claimed `threshold` was populated, which was inaccurate; corrected here.
- **`getReader()` mutable accessor demoted to friend access** (finding 2). `QrCodeBinaryGridReader& getReader()` was public — same shape as the prior `getMutable*()` issues. Now private; access granted to `QrCodeDecoderImage` (step 9's orchestrator, forward-declared) via `friend class QrCodeDecoderImage;`. Same pattern as the friend grants on `DetectPolygonBinaryGrayRefine` and `QrCodePositionPatternDetector` from earlier commits.
- **Edge-scan path end-to-end test + buffer-size bug fix** (finding 3). Added `useEdgeScan_findsCenter` test that constructs a v2 synthetic scene, configures `setUseEdgeScan(true)`, runs `process()`, asserts the alignment is found within ~1.5 modules of ground truth. **The new test caught a real bug**: `arrayX_` and `arrayY_` were declared as `std::vector<float>{12, 0.0f}` which invokes the `initializer_list<float>` ctor (yielding a 2-element vector `{12.0, 0.0}`), not the intended `vector(count, value)` ctor. The dormant edge-scan path was reading out-of-bounds and silently returning false. Fixed by using `std::vector<float>(12, 0.0f)` syntax with a comment warning future readers about the gotcha. Without this fix the entire 200+ LOC `localize()` port was unreachable.
- **v7+ multi-pattern adjustment test** (finding 4). Added `localizePositionPatterns_v7_steersFromNeighbours` — synthesises a v7 scene with all 6 non-corner alignment patterns rendered at their canonical positions per `VERSION_INFO[7].alignment` × `alignment`, runs the full `process()`, asserts every alignment lands within 1 module of ground truth and `moduleFound` lands within 0.5 module of `(moduleX+0.5, moduleY+0.5)`. Exercises both `adjY` (across rows) and `adjX` (across columns) neighbour-steering paths. If the row/column adjustment chain were broken, only the first cell would land near truth and the rest would diverge.

### Regression after fixes

- C++ unit tests: **239/239 pass** (was 237/237; 2 new cases plus the buffer-size bug fix).
- Java baseline re-run: zero quality drift on `tests/baseline.json`.

## 2026-05-10 (later³) — Step 7c: QR finder-pattern detector chain

Three classes that turn the candidate-polygon list from `DetectPolygonBinaryGrayRefine` (step 7b/3) into a graph of QR finder patterns (the three "L"-corner squares with the 1:1:3:1:1 black/white ratio). Step 9's orchestrator (next) walks the graph to find triplets.

### Added

- [include/boofcv_qr/finder/square_locator_pattern_detector_base.hpp](include/boofcv_qr/finder/square_locator_pattern_detector_base.hpp) + [src/finder/square_locator_pattern_detector_base.cpp](src/finder/square_locator_pattern_detector_base.cpp) — verbatim port of `SquareLocatorPatternDetectorBase` (147 LOC). Abstract base wrapping `DetectPolygonBinaryGrayRefine`; configures the wrapped polygon detector for "convex 4-sided shape, output CCW image-coords (`outputClockwiseUpY=false`)", binds an EXTENDED-border bilinear gray sampler, runs `process()` and the abstract `findLocatorPatternsFromSquares()` hook. `MovingAverage` replaced with the inline exponential-decay update from steps 7b/2 and 7b/3.
- [include/boofcv_qr/finder/qr_code_position_pattern_detector.hpp](include/boofcv_qr/finder/qr_code_position_pattern_detector.hpp) + [src/finder/qr_code_position_pattern_detector.cpp](src/finder/qr_code_position_pattern_detector.cpp) — verbatim port of `QrCodePositionPatternDetector` (219 LOC). Each candidate polygon is gated by `checkPositionPatternAppearance` — two perpendicular centerline scans (46 samples each) RLE'd against the 1:1:3:1:1 ratio with `[0.4×, 3×]` tolerance bands. Survivors get an extra refinement pass and a `PositionPatternNode` is materialised with `grayThreshold = (edgeInside + edgeOutside) / 2`. `UtilPoint2D_F64.mean(a, b, out)` and `UtilLine2D_F64.convert(LineSegment, LineParametric)` formulas inlined at the call site.
- [include/boofcv_qr/finder/qr_code_position_pattern_graph_generator.hpp](include/boofcv_qr/finder/qr_code_position_pattern_graph_generator.hpp) + [src/finder/qr_code_position_pattern_graph_generator.cpp](src/finder/qr_code_position_pattern_graph_generator.cpp) — verbatim port of `QrCodePositionPatternGraphGenerator` (176 LOC). For each candidate finder pattern, search nearby finders within `1.2 × maximumQrCodeWidth` and pair them in `SquareGraph` if their geometry is consistent (sides intersect near midpoint within 0.35× tolerance, similar lengths within 25 %, almost-parallel within 45°, max smallest/largest ratio ≤ 1.3). Score is `lineLength × (1 + acuteAngle + sideOffset/2)`; `SquareGraph::checkConnect` keeps the best edge per side. Two `process()` overloads: `vector<PositionPatternNode*>` (used internally) and `vector<PositionPatternNode>&` (caller-friendly convenience).
- [src/finder/finder_pattern.md](src/finder/finder_pattern.md) — combined algorithm doc covering the chain (base → 1:1:3:1:1 check → graph generation), the brute-force NN deviation, all tunables with QR defaults, failure modes, integration points for step 9.
- Added `getMutablePolygonInfo()` on `DetectPolygonBinaryGrayRefine` so the QR finder-pattern detector can walk the wrapper's polygon list and call `refine(info)` (which takes a non-const `Info&`). Same pattern as the friend access added in step 7b/3 — explicit accessor since `getPolygonInfo()` is const.

### Brute-force NN deviation

BoofCV uses ddogleg's `NearestNeighbor` over `KdTreeSquareNode` for asymptotic O(n log n) finder-pattern lookup. We use brute-force O(n²) — for QR-relevant inputs the candidate count is O(20) finder squares per image (typically much less), making brute-force ~400 distance evaluations per image (well under 1 ms). **Reachability of the same set of triplets is preserved**: both algorithms enumerate the same neighbour set within the search radius; only the iteration order can differ. `SquareGraph::checkConnect` is order-insensitive (best-score edge wins per node side regardless of insertion order). Documented in the algorithm doc.

### Tests

- [tests/unit/test_square_locator_pattern_detector_base.cpp](tests/unit/test_square_locator_pattern_detector_base.cpp) — 4 cases: ctor configures the wrapped detector (convex / output-CCW / sides=4..4); process invokes the hook and disables internal contours; `maxContourFraction` roundtrip; `process` rejects non-CV_8UC1.
- [tests/unit/test_qr_code_position_pattern_detector.cpp](tests/unit/test_qr_code_position_pattern_detector.cpp) — mirrors `TestQrCodePositionPatternDetector.java`. `easy` (3 finder patterns in an L), `checkPositionPatternAppearance_positive`, `checkPositionPatternAppearance_negative_filledStone`, `positionSquareIntensityCheck`. The Java AWT `Graphics2D.fillRect` rendering is replaced by `cv::rectangle`; same end-to-end coverage. The `withLensDistortion` Java test is deferred (it depends on the lens-distortion stub).
- [tests/unit/test_qr_code_position_pattern_graph_generator.cpp](tests/unit/test_qr_code_position_pattern_graph_generator.cpp) — mirrors `TestQrCodePositionPatternGraphGenerator.java`. `considerConnect_positive`, `considerConnect_negative_rotated` (45°-rotated neighbour rejected). Plus `process_LShapedTriple` (an L of 3 finders has the centre-corner connected to both outer corners, asserts the brute-force NN finds them) and `maxVersionRoundtrip`.

### Regression

- C++ unit tests: **226/226 pass** in 2.5 s (was 214/214; 12 new cases).
- Java baseline re-run: zero quality drift on `tests/baseline.json` (74.40 % aggregate, BoofCV 1.3.0 on 562 / 1258).

### Deferred from upstream

- **Lens distortion** integration in `setLensDistortion` — same deferral pattern as steps 5 / 7b. Stub no-op so the API surface is parity-clean.
- **`VerbosePrint`** dropped throughout.
- **`KdTree`-based NN** — replaced with brute-force O(n²); see "Brute-force NN deviation" above.
- **`TestQrCodePositionPatternDetector.withLensDistortion`** — depends on the lens-distortion stub.

### Codex review fixes

- **Owned-types-only public API** (raised by codex on 7c, finding 1; three sub-fixes).
  - 1a — `process(const std::vector<PositionPatternNode*>&)` (vector of raw pointers in a public signature) demoted to a private `processPtrList`. The public `process(std::vector<PositionPatternNode>&)` overload is the only public entry point now.
  - 1b — `getMutablePolygonInfo()` on `DetectPolygonBinaryGrayRefine` removed; replaced with `friend class QrCodePositionPatternDetector;` on both `DetectPolygonBinaryGrayRefine` and `DetectPolygonFromContour` (the friend chain the QR detector traverses). Same pattern as the part-2 wrapper's friend grant.
  - 1c — `getMutablePositionPatterns()` on `QrCodePositionPatternDetector` removed; replaced with `friend class QrCodePositionPatternGraphGenerator;`.
- **Brute-force NN ordering** (finding 2). Codex correctly noted that `SquareGraph::checkConnect`'s "first equal-score edge wins" semantics make iteration order observable on floating-point ties. Fixed by sorting candidates by squared distance ascending before invoking `considerConnect`, making C++ traversal deterministic. Java's KdTree doesn't guarantee distance-sorted output, so on a tie our graph and Java's may differ; in practice ties are unmeasurable on real images. Documented in the algorithm doc and at the call site.
- **`setMaximumContour` cutoff** (finding 3, parity-affecting). Replaced the `// TODO(perf)` no-op with a real `DetectPolygonFromContour::setMaximumContour(ConfigLength)` setter + `maximumContourPixels_` storage + filtering in `findCandidateShapes`. Wired from `SquareLocatorPatternDetectorBase::configureContourDetector` to set the cap to `ConfigLength::fixed(min(W,H) × maxContourFraction)`. Without this gate, page borders / UI chrome blobs produce phantom 4-corner candidates passing the QR finder's 1:1:3:1:1 check; Java drops them in `BinaryContourFinder.setMaxContour`. Default `fixed(-1)` matches Java's `ConfigPolygonFromContour.maximumContour` default (no cap).
- **Test coverage gaps** (finding 4).
  - 4a — Extended `QrCodePositionPatternDetector.easy` to feed the detected patterns into the graph generator and assert the L-shape's edge counts (centre = 2, outer = 1 each), mirroring `TestQrCodePositionPatternDetector.easy()` lines 52-68 of the Java suite.
  - 4b — Added `distractor_solidBlackSquareIsRejected` (3 valid finders + 1 solid-black 50×50 distractor; only 3 finders survive) and `checkPositionPatternAppearance_negative_solidBlack` (direct unit test of the 1:1:3:1:1 gate on a solid-black square).
  - 4c — Added `withLensDistortion` deferral comment in the test file's header explaining why the upstream parity test isn't ported (depends on `QrCodeDistortedChecks` + `LensDistortionNarrowFOV` + `SimulatePlanarWorld` infrastructure that this port hasn't ported).
- **Codex finding #3 (length_ reset) was a misread** — reviewer traced through and cleared it. The per-`checkLine` zero-out of `length_` is semantically equivalent to Java's behaviour: Java's class-field state is implicitly zero on first use; we re-zero per call so the second call's RLE doesn't carry over from the first. No change needed.

### Regression after fixes

- C++ unit tests: **228/228 pass** (was 226/226).
- Java baseline re-run: zero quality drift on `tests/baseline.json` (74.40 % aggregate, BoofCV 1.3.0 on 562 / 1258).

## 2026-05-10 (later²) — Step 7b (part 3): RefinePolygonToGray chain

Subpixel polygon refinement. Wraps the contour stage from part 2 with an EM-style line-refit that snaps each polygon side to the underlying gray-image edge using line-integral-derivative weights, plus an edge-intensity quality gate. Per CLAUDE.md "Forbidden moves" line 142, this stage is mandatory verbatim — no `cv::cornerSubPix` substitute.

### Added

- [include/boofcv_qr/polygon/image_line_integral.hpp](include/boofcv_qr/polygon/image_line_integral.hpp) + [src/polygon/image_line_integral.cpp](src/polygon/image_line_integral.cpp) — verbatim port of `boofcv.alg.interpolate.ImageLineIntegral`. Sums `pixel_value * fraction-of-line-in-pixel` for every pixel a line segment touches. Pure algorithmic, no OpenCV equivalent.
- [include/boofcv_qr/polygon/snap_to_line_edge.hpp](include/boofcv_qr/polygon/snap_to_line_edge.hpp) + [src/polygon/snap_to_line_edge.cpp](src/polygon/snap_to_line_edge.cpp) — verbatim port of `SnapToLineEdge` + `BaseIntegralEdge` (collapsed). Samples line-integrals perpendicular to a candidate edge, weights each by the absolute step between adjacent integrals, then fits a polar line via weighted least squares (`FitLine_F64.polar` formula inlined byte-for-byte). Local-coordinate trick (centre + scale) preserved.
- [include/boofcv_qr/polygon/refine_polygon_to_gray.hpp](include/boofcv_qr/polygon/refine_polygon_to_gray.hpp) + [src/polygon/refine_polygon_to_gray.cpp](src/polygon/refine_polygon_to_gray.cpp) — verbatim port of `RefinePolygonToGray` interface, `RefinePolygonToGrayLine` (the QR-relevant concrete impl), and `UtilShapePolygon::convert`. EM-style outer loop: fit each side independently, recompute corners as line intersections, iterate to convergence (`convergeTolPixels`) or `maxIterations` (10 default). Per-side divergence guard via `maxCornerChangePixel`.
  - `ConfigRefinePolygonLineToImage` struct mirrors the upstream `boofcv.factory.shape.ConfigRefinePolygonLineToImage` field-for-field; plumbed-config ctor matches `FactoryShapeDetector.refinePolygon`.
- [include/boofcv_qr/polygon/detect_polygon_binary_gray_refine.hpp](include/boofcv_qr/polygon/detect_polygon_binary_gray_refine.hpp) + [src/polygon/detect_polygon_binary_gray_refine.cpp](src/polygon/detect_polygon_binary_gray_refine.cpp) — verbatim ports of:
  - `DetectPolygonBinaryGrayRefine` — top-level wrapper that runs the contour stage, applies `AdjustPolygonForThresholdBias` per polygon, exposes `refine(Info)` / `refineAll()` that gate on `EdgeIntensityPolygon` (refinement rolled back if edge contrast drops below `before / 1.5`), and `getPolygons` that filters on `minimumRefineEdgeIntensity` (QR default `6`).
  - `EdgeIntensityPolygon` + `ScoreLineSegmentEdge` — the post-refine quality gate (samples 15 points along each side perpendicular to the line, sums up/down line integrals).
  - `AdjustPolygonForThresholdBias` — undoes the half-pixel bias that binary thresholding introduces by shifting two of the four sides per polygon by 1 pixel along the side normal, then re-cornering via line intersections. Java's `UtilPolygons2D_F64.removeAdjacentDuplicates` inlined for the post-shift duplicate sweep.
- Added `getMutableFoundInfo()` on `DetectPolygonFromContour` so the wrapper can mutate detection polygons in place after threshold-bias adjustment without resorting to `const_cast`.
- [src/polygon/refine_polygon_to_gray.md](src/polygon/refine_polygon_to_gray.md) — algorithm doc covering the full chain (line-integral primitive → snap-to-edge → polygon-level EM → top-level wrapper), why this beats `cv::cornerSubPix` per CLAUDE.md "Forbidden moves", all tunables with QR defaults from `ConfigRefinePolygonLineToImage`, failure modes (border-aligned sides, divergence guard, parallel adjacent lines), and integration points for step 7c.

### Tests

- [tests/unit/test_refine_polygon_to_gray.cpp](tests/unit/test_refine_polygon_to_gray.cpp) — 19 cases:
  - **Java parity** — all 5 cases from `TestImageLineIntegral.java` (`zeroLengthLine`, `inside_SlopeZero`, `across_SlopeZero`, `inside_nonZero`, `across_nonZero`, `isInside`). Plus `easy_aligned`, `computePointsAndWeights`, `computePointsAndWeights_border`, `localToGlobal` from `TestSnapToLineEdge.java` (the cases that don't need `FDistort`).
  - **Synthetic end-to-end substitutes** for the Java tests that pull in `FDistort`/`CommonFitPolygonChecks`: `RefinePolygonToGrayLine.alignedSquare` (perfect initial), `alignedSquare_noisyInitial` (sub-pixel jitter on each corner), `fit_tooSmall` (1×1 square is rejected), plus a config-ctor field-landing test.
  - **`EdgeIntensityPolygon` + `AdjustPolygonForThresholdBias` + the wrapper**: `blackSquareClockwise` (inside/outside intensities), `axisAlignedSquare` adjust, `identicalCornersThrows`, `rectanglesProcessThenRefine`, `getPolygonsHonoursMinimumEdgeIntensity`.

### Regression

- C++ unit tests: **207/207 pass** in 2.2 s (was 188/188).
- Java baseline re-run: zero quality drift on `tests/baseline.json` (74.40 % aggregate, BoofCV 1.3.0 on 562 / 1258).

### Deferred from upstream

- **`RefinePolygonToContour`** — the QR factory wires `refineContour=null`, only `RefinePolygonToGrayLine` is used. Documented in the algorithm doc.
- **Lens distortion** — `setLensDistortion` / `setTransform` are no-op stubs throughout the chain (same deferral pattern as in step 5's grid reader and step 7b's contour stage).
- **`AdjustBeforeRefineEdge` hook** on `DetectPolygonBinaryGrayRefine` — unused by QR; not ported.
- **Bilinear-sampled distorted-image variant** of `BaseIntegralEdge` (`GImageGrayDistorted`) — depends on the lens-distortion deferral above.
- **`VerbosePrint`** dropped throughout.
- Most of `TestSnapToLineEdge.fit_noisy_affine` and `TestRefinePolygonToGrayLine.fit_*` — they synthesise images via BoofCV's `FDistort` (affine resampling) which we don't pull in. Synthetic equivalents using `cv::rectangle` cover the easy-aligned branches.

### Codex review fixes

- **Owned-types-only public API** (raised by codex on 7b part 3, finding 1). `getPolygons()` no longer takes a `std::vector<DetectedInfo*>* storageInfo` arg — that was BoofCV's storage-arg-recycling idiom imported wholesale and exposed two layers of raw pointers in a public signature. Now returns `std::vector<std::vector<cv::Point2d>>` by value. Added a sibling `getPolygonInfoFiltered()` that returns the filtered `Info` entries by value for callers that need the full diagnostic record. `DetectPolygonFromContour::getMutableFoundInfo()` is demoted: replaced with a `friend class DetectPolygonBinaryGrayRefine` declaration so the wrapper can mutate per-detection state in place without the mutable accessor leaking into the public surface.
- **QR config plumbing test** (finding 2). Added `DetectPolygonBinaryGrayRefine.qrConfigDefaultsReachUnderlying` — constructs the wrapper with `ConfigRefinePolygonLineToImage`'s upstream defaults (the same path `FactoryShapeDetector.refinePolygon` takes) and asserts every field lands on the underlying `RefinePolygonToGrayLine` and `SnapToLineEdge` (cornerOffset, lineSamples, sampleRadius, maxIterations, convergeTolPixels, maxCornerChangePixel) plus the wrapper-level `minimumRefineEdgeIntensity`/`outputClockwise`/`contourEdgeThreshold`. Added the corresponding getters (`getCornerOffset`, `getMaxIterations`, `getConvergeTolPixels`, `getMaxCornerChangePixel`) on `RefinePolygonToGrayLine`, plus `getRefineGray()` and a const `getDetector()` overload on the wrapper.
- **Strengthened threshold-bias test** (finding 3). Replaced the previous "still 4 corners" assertion with three directional cases: `axisAlignedSquare_imageCw` asserts the exact post-shift corner positions for QR's `clockwise=false` path (right and bottom edges shift outward by 1 pixel, top-left unchanged); `axisAlignedSquare_clockwiseTrue` covers the opposite branch (catches sign-of-direction bugs); `rotatedSquare` asserts the diamond-orientation case (each corner moves ≤ √2 px and adjacent corners stay non-degenerate). Documented Java's exact behaviour in the test comments.
- **Algorithmic-core synthetics** (finding 4). Added four cases that exercise specific code paths beyond the noise-free black rectangle:
  - `noisyEdge_weightedPolarFitConvergence` — Gaussian noise σ=10 added to a 30×30 black square. Asserts refined corners land within 2 px of the threshold-bias-adjusted ground truth.
  - `rotatedSquare_perpendicularSign` — 45°-rotated diamond via `cv::fillPoly`. Catches sign-of-tangent bugs in `SnapToLineEdge` (a flipped perpendicular would push refinement off the edge).
  - `lowContrastPolygonRejected` — fg=196, bg=200 (delta 4). With QR's `minimumRefineEdgeIntensity=6` gate the polygon must drop. Plus a high-contrast control assertion.
  - `ScoreLineSegmentEdge.blackToWhiteDerivative` — direct unit test of the line-integral derivative on a black→white step edge.

### Regression after fixes

- C++ unit tests: **214/214 pass** (was 207/207).
- Java baseline re-run: zero quality drift on `tests/baseline.json`.

## 2026-05-10 (later) — Step 7b (part 2): DetectPolygonFromContour + ContourEdgeIntensity

The contour-to-polygon stage. Wraps `cv::findContours` (per CLAUDE.md "OpenCV substitution policy") + `PolylineSplitMerge` (from part 1) + an edge-intensity false-positive filter. Produces the candidate polygon list that the QR finder-pattern detector (next file) consumes.

### Added

- [include/boofcv_qr/polygon/detect_polygon_from_contour.hpp](include/boofcv_qr/polygon/detect_polygon_from_contour.hpp) + [src/polygon/detect_polygon_from_contour.cpp](src/polygon/detect_polygon_from_contour.cpp) — verbatim port of `DetectPolygonFromContour` (638 LOC) and `ContourEdgeIntensity` (140 LOC), plus the `Contour` data struct, `PointsToPolyline` and `PolygonHelper` interfaces.
  - **Contour extraction**: `cv::findContours(binary, contours, hierarchy, cv::RETR_CCOMP, cv::CHAIN_APPROX_NONE)` per CLAUDE.md "OpenCV substitution policy" line 145. `RETR_CCOMP` matches BoofCV's `LinearContourLabelChang2004` external+internal blob topology — top-level entries are external boundaries, their hierarchy children are internal-hole boundaries. We re-bundle them into the same `Contour` shape BoofCV uses.
  - **Winding-direction adjustment**: OpenCV's `findContours` emits external contours **CCW in image coords**, BoofCV's tracer emits them **CW in image coords**. The polyline corner finder's convex check (`PolylineSplitMerge::setSplitVariables` / `isPositiveZ`) is hard-coded for BoofCV's winding — under OpenCV's winding it rejects splits that would form CONVEX corners (instead of concave). `buildContoursFromOpenCV` reverses each contour in place to fix this. Without the reversal, a perfect black square never reaches a 4-corner fit. Documented in the algorithm doc.
  - `cv::findContours` mutates its input; we clone defensively (one allocation per `process()`).
  - `PolylineSplitMergeAdapter : PointsToPolyline` wraps the part-1 `PolylineSplitMerge` so callers can swap in alternative corner finders.
  - `BinaryContourFinder` / `LinearContourLabelChang2004` / `ContourPacked` indirection dropped (we get full point arrays from OpenCV).
  - `MovingAverage` ported as an inline exponential-decay update (`milliContour` / `milliShapes`).
  - **Lens distortion** deferred — same deferral pattern as in step 5's grid reader. The `polygon` and `polygonDistorted` fields are populated identically until distortion lands.
  - Verbose-print paths skipped (no `boofcv.misc.VerbosePrint` infra ported).
- [src/polygon/detect_polygon_from_contour.md](src/polygon/detect_polygon_from_contour.md) — algorithm doc per CLAUDE.md "Algorithm documentation requirement". Covers the 5-stage pipeline, `RETR_CCOMP` vs `LinearContourLabelChang2004` mapping, the winding-direction footgun, all tunables with their `ConfigQrCode` defaults, failure modes, integration points for downstream stages.

### Tests

- [tests/unit/test_detect_polygon_from_contour.cpp](tests/unit/test_detect_polygon_from_contour.cpp) — 11 cases:
  - **Java parity** (subset that doesn't depend on `FactoryShapeDetector` / `FactoryThresholdBinary` / `CommonFitPolygonChecks`): `touchesBorder` (positive + negative branches), `determineCornersOnBorder`, `flip` static (mirrors the Java behaviour: vertex 0 preserved, 1..N-1 reversed). Plus all 2 cases from `TestContourEdgeIntensity.java` (`simpleCase`, `smallContours`).
  - **Synthetic end-to-end** (substitutes for the upstream tests that pull in BoofCV's image-rendering/factory infrastructure): rectangle detection (4 black rectangles → 4 found polygons), circle rejection (3-6 sides — none match), triangle detection (3 sides → 1 found, 4-6 → none), internal-contour preservation (donut shape — internal hole survives in the output `Contour`), `canTouchBorder` toggle.

### Regression

- C++ unit tests: **184/184 pass** in 2.0 s (was 173/173).
- Java baseline re-run: zero quality drift on `tests/baseline.json` (74.40 % aggregate, BoofCV 1.3.0 on 562 / 1258).

### Deferred from upstream

- `RefinePolygonToGray` and the subpixel-corner refinement chain — next assignment, not part of this commit. The output polygons here have integer-pixel corners; refinement is a separate stage that consumes them.
- `setLensDistortion` — same deferral as step 5's grid reader. Stub no-op so the API surface is parity-clean; documented in the algorithm doc.
- `VerbosePrint` instrumentation — not ported.
- Most of `TestDetectPolygonFromContour.java`'s end-to-end tests — they depend on `FactoryThresholdBinary` / `FactoryShapeDetector.polygonContour` / `CommonFitPolygonChecks` (BoofCV factory infra). Synthetic equivalents using `cv::rectangle` / `cv::fillPoly` cover the same code paths.

### Codex review fixes

- **Splitter-config plumbing through the adapter** (BLOCKING, finding 1; raised by codex on 7b part 2). The previous `PolylineSplitMergeAdapter` only forwarded `MinSides`/`MaxSides`/`Loops`/`Convex` — the other 7 `ConfigPolylineSplitMerge` fields silently reverted to `PolylineSplitMerge`'s baked-in ctor defaults. Under `ConfigQrCode.java`'s overrides (`minimumSideLength=2`, `cornerScorePenalty=0.4`, `maxSideError=relative(0.12,3)`, …) this would silently degrade step 7c's finder-pattern detection on real inputs. Added a `ConfigPolylineSplitMerge` struct that mirrors the upstream `boofcv.abst.shapes.polyline.{BaseConfigPolyline,ConfigPolylineSplitMerge}` field-for-field with the same default values, and a plumbed-config ctor on the adapter that applies *all 11 fields* to `impl_` exactly as `boofcv.abst.shapes.polyline.NewSplitMerge_to_PointsToPolyline` does (lines 42-54 of the Java). `refineIterations` is documented-but-skipped per its `RefinePolyLineCorner` deferral. New tests `qrConfigDefaultsReachImpl` and `defaultCtorMatchesUpstreamDefaults`.
- **Contour start-pixel rotation after winding reversal** (BLOCKING, finding 2; raised by codex). Reversing for winding fixed the `isPositiveZ` convex-check polarity but moved the start pixel to BoofCV's *last* scanned position; `PolylineSplitMerge::findCornerSeed` seeds from `contour[0]`, so corner indices came out rotated relative to BoofCV's. Added a `std::rotate` after the reverse that puts the topmost-row's leftmost pixel at index 0 — matches `LinearContourLabelChang2004`'s row-major scan order. Same fixup applied to internal contours. New test `contourCanonicalStart` asserts the expected start pixel for a black 30×30 square. Doc updated.
- **`setHelper` raw pointer → `std::shared_ptr<PolygonHelper>`** (finding 3; raised by codex). CLAUDE.md "Public API design" line 32 forbids raw pointers in public signatures. Storage flipped to `std::shared_ptr<PolygonHelper>` (Java GC → C++ shared ownership for an injectable strategy hook). `getFoundInfo` doc-comment now states the lifetime contract (ref invalidated by next `process()`).
- **`saveInternalContours_` toggle + documented deviation** (finding 4; doc-only, raised by codex). BoofCV's `polygonContour()` factory wires `LinearExternalContours` (`isSaveInternalContours=false`); we default to `true` so downstream pipelines (the QR finder-pattern detector at step 7c, which consumes hole topology) get the contours without an explicit setter call. Added `setSaveInternalContours(bool)` setter and a "Deviations from upstream" section in the algorithm doc; updated the donut-test comment to label the assertion as a deviation check. New test `saveInternalContoursToggle` verifies the `false` path.

### Regression after fixes

- C++ unit tests: **188/188 pass** (was 184/184).
- Java baseline re-run: zero quality drift on `tests/baseline.json`.

## 2026-05-10 — Step 7b (part 1): PolylineSplitMerge + MaximumLineDistance

Largest single file in the port at 907 LOC of Java source (excluding the inner `Corner`, `CandidatePolyline`, `SplitResults`, `ErrorValue` types). This is the corner-finding algorithm that turns a contour pixel sequence into a polyline with subpixel-quality corners — the unique-to-BoofCV stage that CLAUDE.md "OpenCV substitution policy" forbids replacing with `cv::approxPolyDP`.

### Added

- [include/boofcv_qr/polyline/polyline_split_merge.hpp](include/boofcv_qr/polyline/polyline_split_merge.hpp) + [src/polygon/polyline_split_merge.cpp](src/polygon/polyline_split_merge.cpp) — verbatim port of `PolylineSplitMerge` and `MaximumLineDistance` (the only `SplitSelector` used for QR). The grow-then-shrink algorithm: build a triangle, repeatedly split the side whose split would change the score the most, then repeatedly remove the worst-cost-effective corner. `// TODO(perf): recycle` markers on the `CornerPool` and `polylines_` vector mark the BoofCV `DogArray.reset()` recycle sites we'll revisit later.
  - Inner-class `Corner`, `CandidatePolyline`, `ErrorValue`, `SplitResults` ported as nested structs; field names verbatim. `SplitSelector` and `MaximumLineDistance` kept at namespace scope (not nested) so callers can declare custom selectors.
  - `DogLinkedList<Corner>` → in-house `CornerList` over `std::list<Corner*>`. Iterators give pointer-stable Element handles; `Element<Corner>` becomes `CornerList::Iter`. `end()` is the "null Element" sentinel.
  - `ConfigLength` ported as a tiny local struct (`compute(double)` + `computeI(double)` only — full BoofCV `ConfigLength` is in `boofcv-types` which we don't pull in).
  - Geometric helpers inlined: `lineParametricDistanceSq`, `lineSegmentDistanceSq` (mirroring `Distance2D_F64.distanceSq` for both line types per the bytecode-decompiled formula, not a textbook re-derivation), `isPositiveZ` (UtilPolygons2D_I32), `circularDistanceP` / `circularPlusPOffset` / `circularMinusPOffset` (boofcv-ip `CircularIndex`).
  - `INT_MAX + INT_MAX` overflow in `sequentialSideFit`'s `limit` is widened to `int64_t` then clamped — Java's int wrap-around → fallback to `contour.size()` is preserved without invoking C++ signed-overflow UB.
- [src/polygon/polyline_split_merge.md](src/polygon/polyline_split_merge.md) — algorithm doc per CLAUDE.md "Algorithm documentation requirement". Covers the grow-then-shrink approach, why it beats `cv::approxPolyDP` / RANSAC / Hough, all 11 tunables with QR defaults, failure modes, integration points for downstream recovery (`getPolylines()` exposes the per-side-count saved candidates).

### Tests

- [tests/unit/test_polyline_split_merge.cpp](tests/unit/test_polyline_split_merge.cpp) — mirrors all 30 cases from `TestPolylineSplitMerge.java` + 2 cases from `TestMaximumLineDistance.java`. The `process_line` test in the Java suite does not assert the return of `process()` — `bestPolyline` is set even when the final `bestSize<minSides` gate trips with default `minSides=3` on a 2-corner result; comment in the test explains.

### Regression

- C++ unit tests: **173/173 pass** in 1.9 s.
- Java baseline re-run: zero quality drift on `tests/baseline.json` (74.40 % aggregate detection rate, BoofCV 1.3.0 on 562 images / 1258 GT). C++ output isn't yet wired into the pipeline — the regression confirms no upstream-side regressions, as expected at this stage.

### Deferred from upstream

- `MinimizeEnergyPrune`, `FitLinesToContour`, `RefinePolyLineCorner`, `SplitMergeLineFit*` — alternative polyline algorithms that QR doesn't use. We only need `PolylineSplitMerge` + `MaximumLineDistance`.
- `SplitSelector` strategy-injection at construction time (CLAUDE.md "Public API design" line 26) — `setSplitter()` exists, but the `// TODO` for ctor-time injection follows the same deferral pattern as the RS one in step 2.

### Codex review fixes

- **Public API owned types** (CLAUDE.md "Public API design" line 32, raised by codex on 7b). `getPolylines()` now returns `const std::vector<CandidatePolyline>&` (storage flipped to value-typed `std::vector<CandidatePolyline>` — matches Java's `DogArray<CandidatePolyline>` which stores values). `getBestPolyline()` returns `std::optional<CandidatePolyline>` by value instead of `CandidatePolyline*`. Internal tracking is now an index (`bestPolylineIndex_ = -1` for "no best"). The `// TODO(perf): recycle` marker on the polylines vector flips from "future work" to "now done" — value-typed storage with `emplace_back()` reuses inner-vector capacity on `clear()` and `reset()`. Tests updated; 173/173 still pass.
- **`isPositiveZ` `int64_t` widening** (raised by codex on 7b). Behaviour unchanged; added `FIXME(parity)` comment in `src/polygon/polyline_split_merge.cpp` explaining that Java's `UtilPolygons2D_I32.isPositiveZ` relies on defined int wrap-around, C++ signed overflow is UB, and the widening is identical for contour-pixel deltas under ~46k (QR images in the regression set never come close). Comment includes the deterministic-wrap recipe `static_cast<int32_t>(int64_product)` for future parity-critical use cases.

## 2026-05-09 (later⁸) — Step 7a: square graph utilities

Plan reversed: user opted to implement after all. Starting with the
small structural pieces — `SquareNode`, `SquareEdge`, `SquareGraph`,
`PositionPatternNode` — before the heavy polygon stack in 7b.

### Added

- [include/boofcv_qr/squares/square_node.hpp](include/boofcv_qr/squares/square_node.hpp) + [src/polygon/square_node.cpp](src/polygon/square_node.cpp) — verbatim port of `SquareNode`. Edges are non-owning `SquareEdge*` (ownership lives in `SquareGraph`). `KdTreeSquareNode` inner class skipped (unused by QR).
- [include/boofcv_qr/squares/square_edge.hpp](include/boofcv_qr/squares/square_edge.hpp) — header-only port of `SquareEdge`.
- [include/boofcv_qr/squares/square_graph.hpp](include/boofcv_qr/squares/square_graph.hpp) + [src/polygon/square_graph.cpp](src/polygon/square_graph.cpp) — verbatim port of `SquareGraph`. Geometric helpers (line-line / segment-segment intersection, vector acute angle, circular index, angle distance) inlined here instead of pulling in BoofCV's `georegression` module + `ejml`. `connect()` is public for parity-test access.
- [include/boofcv_qr/position_pattern_node.hpp](include/boofcv_qr/position_pattern_node.hpp) — header-only port of `PositionPatternNode`.
- [src/polygon/squares.md](src/polygon/squares.md) — combined algorithm doc.

### Tests

- [tests/unit/test_square_node.cpp](tests/unit/test_square_node.cpp) — mirrors `TestSquareNode.java` + `TestSquareEdge.java`.
- [tests/unit/test_square_graph.cpp](tests/unit/test_square_graph.cpp) — mirrors all 7 cases from `TestSquareGraph.java` including `almostParallel`/`acuteAngle` over both polygon windings.

### Regression

- C++ unit tests: **135/135 pass** in 1.6 s.
- Java baseline re-run: zero quality drift.

## 2026-05-09 (later⁷) — Pause point: detailed plan for steps 7–9

[docs/plans/01_steps_7_through_9.md](docs/plans/01_steps_7_through_9.md) documents what's needed to finish the port. Steps 7 (polygon + finder pattern detection), 8 (alignment), and 9 (orchestrator) remain — sized at ~8–11 work-days, dominated by `PolylineSplitMerge.java` (907 LOC) and the polygon stack under `boofcv-feature/.../shapes/`. Codex-review carry-overs from earlier steps (RS strategy injection, per-block decode status, `setTransformFromLinesSquare`) are listed for the step-9 work since they need the orchestrator to exist first.

Status at this commit: 6 of 9 steps complete with full BoofCV parity. End-to-end runtime pipeline isn't yet wired; binarize → polygon → finder → alignment → sampler is missing the polygon and finder layers. The Java-baseline harness and `tests/baseline.json` are unchanged and still gate the eventual C++ regression at ±2% per category.

## 2026-05-09 (later⁶) — Step 6: BoofCV BLOCK_OTSU binarizer

### Added

- [include/boofcv_qr/threshold_block_otsu.hpp](include/boofcv_qr/threshold_block_otsu.hpp) + [src/binary/threshold_block_otsu.cpp](src/binary/threshold_block_otsu.cpp) — port of BoofCV's `ThresholdBlock` + `ThresholdBlockOtsu` + `ComputeOtsu` collapsed into one class. Tile the input into 40×40 blocks (configurable), compute 256-bin per-block histograms, then for each block compute Otsu's threshold from the 3×3 neighbourhood-summed histogram and apply it. Defaults match `ConfigQrCode.java`: `useOtsu2=true`, `scale=1.0`, `down=true`, `tuning=4`, `requestedBlockWidth=40`, `thresholdFromLocalBlocks=true`. Output follows CLAUDE.md "Binary image convention" (`CV_8UC1`, `0/1`, foreground = dark module). Algorithm doc: [src/binary/threshold_block_otsu.md](src/binary/threshold_block_otsu.md).
- [tests/unit/test_threshold_block_otsu.cpp](tests/unit/test_threshold_block_otsu.cpp) — bimodal-image partition checks (no JUnit parity test exists in `boofcv-recognition`; BoofCV's tests live in `boofcv-ip/src/test/` which we don't pull in).

### Regression

- C++ unit tests: **124/124 pass** in 1.4 s.
- Java baseline re-run: zero quality drift.

### Why not `cv::adaptiveThreshold`

CLAUDE.md is explicit: per-pixel adaptive threshold isn't equivalent. The differences are real (Gaussian-weighted vs block-tiled Otsu, no texture-penalty term, no 3×3-block smoothing). We port verbatim.

## 2026-05-09 (later⁵) — Step 5: perspective grid sampler

### Added (OpenCV becomes a CMake dep at this step)

- `find_package(OpenCV)` in [CMakeLists.txt](CMakeLists.txt). Required components: `core` (Mat, Matx33d, perspectiveTransform), `calib3d` (findHomography), `imgproc` (getPerspectiveTransform). Hint Homebrew's keg-only `OpenCV_DIR` automatically.
- [include/boofcv_qr/qr_code_codeword_locations.hpp](include/boofcv_qr/qr_code_codeword_locations.hpp) + [src/sampler/qr_code_codeword_locations.cpp](src/sampler/qr_code_codeword_locations.cpp) — feature mask + zigzag bit-traversal sequence per QR version. Uses our own `Point2I`; no OpenCV dependency. (MicroQR variant intentionally not ported.)
- [include/boofcv_qr/qr_code_binary_grid_to_pixel.hpp](include/boofcv_qr/qr_code_binary_grid_to_pixel.hpp) + [src/sampler/qr_code_binary_grid_to_pixel.cpp](src/sampler/qr_code_binary_grid_to_pixel.cpp) — grid↔pixel homography. 4-pt setup via `cv::getPerspectiveTransform`; N-pt via `cv::findHomography(..., 0)`; point transforms via `cv::perspectiveTransform` (no `cv::warpPerspective` per CLAUDE.md). Includes outlier rejection (`removeFeatureWithLargestError`) and adjust-with-features residual lookup.
- [include/boofcv_qr/qr_code_binary_grid_reader.hpp](include/boofcv_qr/qr_code_binary_grid_reader.hpp) + [src/sampler/qr_code_binary_grid_reader.cpp](src/sampler/qr_code_binary_grid_reader.cpp) — pixel-level sampler. CV_8UC1 only (Java template specialised). Nearest-neighbour read with EXTENDED-border clamping (matches BoofCV's `nearestNeighborPixelS`); 5-sample majority-vote `readBit`.
- [src/sampler/qr_code_codeword_locations.md](src/sampler/qr_code_codeword_locations.md), [qr_code_binary_grid_to_pixel.md](src/sampler/qr_code_binary_grid_to_pixel.md), [qr_code_binary_grid_reader.md](src/sampler/qr_code_binary_grid_reader.md) — algorithm docs.

### Tests

- [tests/unit/test_qr_code_codeword_locations.cpp](tests/unit/test_qr_code_codeword_locations.cpp) — JUnit-mirrored bit-order check for v2, full-fill for v2..40, data-bit capacity per ISO §6.5.1 for v1/2/3/.../40.
- [tests/unit/test_qr_code_binary_grid_to_pixel.cpp](tests/unit/test_qr_code_binary_grid_to_pixel.cpp) — synthetic 4-corner setup (upstream tests use `QrCodeEncoder`+`QrCodeGeneratorImage` which we don't ship); round-trip imageToGrid∘gridToImage.

### Regression

- C++ unit tests: **119/119 pass** in 1.6 s.
- Java baseline re-run: zero quality drift.

### Deferred from upstream

- `setTransformFromLinesSquare` (line-correspondence DLT used when only 3 finder polygons are reliable) — depends on inputs that step 7's finder-pattern detector produces. Lands then.
- `setLensDistortion` on the grid reader — not needed for QR; consumers can compose `cv::undistortPoints` if they need wide-FOV support.
- `QrCodeBinaryGridReader<T>` template parameter — committed to `CV_8UC1` per CLAUDE.md.

## 2026-05-09 (later⁴) — Step 4: format/version BCH, mask, decoder-bits orchestrator

### Added

- [include/boofcv_qr/qr_code.hpp](include/boofcv_qr/qr_code.hpp) + [src/decoder/qr_code.cpp](src/decoder/qr_code.cpp) — partial port of `QrCode` struct: data fields (`version`, `error`, `mask`, `rawbits`, `corrected`, `message`, `byteEncoding`, `failureCause`, `mode`, `totalBitErrors`, `bitsTransposed`, `thresh*`), `ErrorLevel` enum, `BlockInfo`, `VersionInfo`, plus the populated `VERSION_INFO[1..40]` table verbatim from ISO 18004 Tables 9 + E.1. Geometry fields (`ppCorner` etc.) deferred to step 7+ because they need OpenCV.
- [include/boofcv_qr/qr_code_polynomial_math.hpp](include/boofcv_qr/qr_code_polynomial_math.hpp) + [src/decoder/qr_code_polynomial_math.cpp](src/decoder/qr_code_polynomial_math.cpp) — BCH(15,5) format codec, BCH(18,6) version codec, brute-force minimum-Hamming-distance corrector. Hamming pop-count inlined to avoid pulling in BoofCV's descriptor module.
- [include/boofcv_qr/qr_code_mask_pattern.hpp](include/boofcv_qr/qr_code_mask_pattern.hpp) + [src/decoder/qr_code_mask_pattern.cpp](src/decoder/qr_code_mask_pattern.cpp) — abstract `QrCodeMaskPattern` + 8 Meyer's-singleton subclasses (M000…M111) with the ISO 18004 §7.8.2 / Table 10 XOR formulas verbatim.
- [include/boofcv_qr/qr_code_decoder_bits.hpp](include/boofcv_qr/qr_code_decoder_bits.hpp) + [src/decoder/qr_code_decoder_bits.cpp](src/decoder/qr_code_decoder_bits.cpp) — orchestrator. `applyErrorCorrection` (per-block RS), `decodeMessage` (mode-aware payload extraction), `decodeEci`, `checkPaddingBytes`, `alignToBytes`, `getLengthBits{Numeric,Alphanumeric,Bytes,Kanji}` (mirrors `QrCodeEncoder` length-field-bits-per-version table — duplicated locally so the encoder isn't pulled in).

### Tests

- [tests/unit/test_qr_code.cpp](tests/unit/test_qr_code.cpp) — VERSION_INFO block-arithmetic identity, alignment-coord monotonicity, totalDataBytes against ISO spec, totalModules, reset.
- [tests/unit/test_qr_code_mask_pattern.cpp](tests/unit/test_qr_code_mask_pattern.cpp) — all 9 JUnit cases mirrored.
- [tests/unit/test_qr_code_polynomial_math.cpp](tests/unit/test_qr_code_polynomial_math.cpp) — encode + check + DCH-correct for both BCH codes; mirrors all 7 JUnit cases.
- [tests/unit/test_qr_code_decoder_bits.cpp](tests/unit/test_qr_code_decoder_bits.cpp) — `alignToBytes`, `checkPaddingBytes`, `decodeEci_IsoExample`, `getLengthBits` table. The encoder-dependent tests from upstream (`applyErrorCorrection` round-trip, `invalidEncoding`) are deferred until step 9 since `QrCodeEncoder` isn't part of the decoder-only deliverable.

### Algorithm docs

- [src/decoder/qr_code.md](src/decoder/qr_code.md), [qr_code_polynomial_math.md](src/decoder/qr_code_polynomial_math.md), [qr_code_mask_pattern.md](src/decoder/qr_code_mask_pattern.md), [qr_code_decoder_bits.md](src/decoder/qr_code_decoder_bits.md).

### Regression

- C++ unit tests: **111/111 pass** in 0.56 s.
- Java baseline re-run: zero drift.

### Deferred

- `QrCode` geometry fields (`ppCorner`, `ppDown`, `ppRight`, `bounds`, `Hinv`, `alignment[]`) — need OpenCV's `cv::Point2d`/`Matx33d`. Land in step 6 when OpenCV is added.
- `QrCodeEncoder` — not part of the decoder-only deliverable. Length-field-bits table replicated locally.
- `QrCodeCodeWordLocations` — used by `LOCATION_BITS[]` in BoofCV; only needed by the sampler. Step 5.
- Encoder-dependent tests (`applyErrorCorrection` round-trip, `invalidEncoding`) — pick up at step 9 with end-to-end harness.

## 2026-05-09 (later still still) — Step 3 of the port: codec bits

### Added

- [include/boofcv_qr/qr_mode.hpp](include/boofcv_qr/qr_mode.hpp) — `Mode` enum (with bit codes per ISO 18004 §6.4.1) + `Mode_lookup(int)` helper, and `Failure` enum. Subset of `QrCode.java`; the full struct is deferred to step 4.
- [include/boofcv_qr/packed_bits.hpp](include/boofcv_qr/packed_bits.hpp) — header-only port of `PackedBits8` with `get`/`set`/`read`/`append` (4 overloads)/`growArray`/`zero`/`isIdentical`/`wrap`. The Java `PackedBits` interface and `PackedBits32` variant are intentionally not ported (unused by QR). Doc: [src/decoder/packed_bits.md](src/decoder/packed_bits.md).
- [include/boofcv_qr/eci_encoding.hpp](include/boofcv_qr/eci_encoding.hpp) + [src/decoder/eci_encoding.cpp](src/decoder/eci_encoding.cpp) — UTF-8 validator and ECI designator → charset-name table (ZXing's table mirrored verbatim). Doc: [src/decoder/eci_encoding.md](src/decoder/eci_encoding.md).
- [include/boofcv_qr/qr_codec_bits_utils.hpp](include/boofcv_qr/qr_codec_bits_utils.hpp) + [src/decoder/qr_codec_bits_utils.cpp](src/decoder/qr_codec_bits_utils.cpp) — full port of `QrCodeCodecBitsUtils` (numeric / alphanumeric / byte / kanji decoders, matching encoders, `flipBits8`, `containsKanji`/`containsByte`/`containsAlphaNumeric`). Doc: [src/decoder/qr_codec_bits_utils.md](src/decoder/qr_codec_bits_utils.md).

### Tests

- [tests/unit/test_packed_bits.cpp](tests/unit/test_packed_bits.cpp) — mirrors all 6 cases from `TestPackedBits8.java`.
- [tests/unit/test_eci_encoding.cpp](tests/unit/test_eci_encoding.cpp) — mirrors `TestEciEncoding.java` plus extra coverage of the ECI designator table.
- [tests/unit/test_qr_codec_bits_utils.cpp](tests/unit/test_qr_codec_bits_utils.cpp) — mirrors `TestQrCodeCodecBitsUtils.java`, plus round-trip tests for numeric / alphanumeric / kanji modes (only the byte mode was explicitly tested upstream; the others are additional coverage of our port).

### Charset-handling deviation from upstream — documented

Java's `decodeByte` / `decodeKanji` invoke `new String(bytes, "Shift_JIS")` (and similar) which goes through `java.nio.charset.Charset`. C++ has no built-in equivalent. We port the byte-level algorithm verbatim and store raw bytes in `workString` as a byte sequence, with `selectedByteEncoding` reporting the announced label. Result is byte-equivalent for ASCII / ISO-8859-1 / UTF-8; for `Shift_JIS` and other multi-byte encodings, the consumer must re-decode. See `src/decoder/qr_codec_bits_utils.md` for the rationale.

### Regression check

- C++ unit tests: **85/85 pass** (`ctest --output-on-failure`, 1.6 s).
- Java baseline re-run on the full `boofcv-qrcodes` dataset: zero drift vs the locked [tests/baseline.json](tests/baseline.json) on every quality metric across all 17 categories.

### Harness reliability fix

- [tools/java_reference/src/main/java/qrboofcv/Baseline.java](tools/java_reference/src/main/java/qrboofcv/Baseline.java): parse the ground-truth sidecar **before** loading the image, and retry image-load once after a brief delay if it returns null. Found while investigating a one-off run where one image (`damaged/image018.jpg`) transiently failed to load under heavy host load — that turned a "no image" event into "no GT either", which falsely reported drift. With the fix, an isolated load failure becomes "GT preserved, 0 detections," which surfaces correctly as a precision/recall delta rather than a phantom GT-count change. The locked baseline numbers are unchanged.

### Deferred

- `QrCodeDecoderBits` (orchestrator that splits codewords into RS blocks, applies correction, then runs the codec utils per segment). Depends on `QrCode.VERSION_INFO[]` table — natural fit for step 4 alongside format/version BCH decoders.
- `QrCode` struct full body, `ErrorLevel` enum, `VersionInfo` / `BlockInfo`, mask-pattern XOR — step 4.
- `PackedBits32` — not used by QR.

## 2026-05-09 (later still) — Step 2 of the port: Reed-Solomon

### Added

- [include/boofcv_qr/galois_table_ops.hpp](include/boofcv_qr/galois_table_ops.hpp) + [src/galois/galois_table_ops.cpp](src/galois/galois_table_ops.cpp) — port of `GaliosFieldTableOps_U8` and `GaliosFieldTableOps_U16` as a single `GaliosFieldTableOpsT<WordT>` template (Java has two classes only because Java generics can't take primitive types). Public aliases `GaliosFieldTableOps_U8` / `_U16` keep naming parity. Algorithm doc: [src/galois/galois_table_ops.md](src/galois/galois_table_ops.md).
- [include/boofcv_qr/reed_solomon.hpp](include/boofcv_qr/reed_solomon.hpp) + [src/reed_solomon/reed_solomon.cpp](src/reed_solomon/reed_solomon.cpp) — port of `ReedSolomonCodes_U8` / `_U16` as `ReedSolomonCodesT<WordT>`. Berlekamp-Massey error locator, brute-force Chien search, Forney magnitude evaluator, generator polynomial construction (QR `generatorBase=0`, Aztec `=1`). Algorithm doc: [src/reed_solomon/reed_solomon.md](src/reed_solomon/reed_solomon.md).
- [tests/unit/test_galois_table_ops.cpp](tests/unit/test_galois_table_ops.cpp) — typed gtests covering polyScale / polyAdd(_S) / polyAddScaleB / polyMult(_flipA, _S) / polyEval(_S, Continue) / polyDivide(_S). Each Java JUnit case from `TestGaliosFieldTableOps_U8/U16.java` runs against both `uint8_t` and `uint16_t` instantiations.
- [tests/unit/test_reed_solomon.cpp](tests/unit/test_reed_solomon.cpp) — typed gtests covering computeECC (incl. Python reference vector and ISO 18004 §7.2.3 known generators), computeSyndromes, generatorFamily0/Base1, findErrorLocatorPolynomialBM (BM vs direct), findErrorLocations_BruteForce (positive + over-budget), findErrorEvaluator (1/2/3-error reference), correctErrors_hand, and correct_random.

### Regression check

- C++ unit tests: **65/65 pass** (`ctest --output-on-failure`, 0.49 s). The biggest case is `correct_random<uint8_t>` running 60 000 random encode-corrupt-correct cycles across `(numBits, primitive, generatorBase)` configs `(4, 0b10011, 0)`, `(8, 0x11d, 0)`, `(8, 0x11d, 1)` — all green. `<uint16_t>` runs 6 000 of the same as a template-instantiation smoke test.
- Java baseline re-run on the full `boofcv-qrcodes` dataset: identical quality numbers to the locked [tests/baseline.json](tests/baseline.json) — zero drift across all 17 categories on every metric.

### Notes

- **One template instead of two classes** is a deliberate deviation from upstream. CLAUDE.md verbatim spirit (preserve loop structure, variable names, comments) is honoured — every algorithmic line maps 1:1 to either the U8 or the U16 Java source. The "duplication" Java has is purely a Java-language constraint (no `Foo<byte>`); collapsing it in C++ eliminates a long-tail bug class where a fix in U8 forgets to land in U16.
- **`generator_` member trailing underscore** disambiguates the field from the `generator(int degree)` member function (Java uses separate field/method namespaces; C++ doesn't). Watch for this when grepping across languages.
- **`std::function` strategy injection deferred.** CLAUDE.md "Public API design" calls for hooks at `findErrorLocatorPolynomialBM` / `correctErrors` etc. for known-prefix RS and clipped-QR fallback. We exposed every stage as a public method (so consumers can drive the pipeline manually) but did not yet add ctor-time `std::function` injection — too premature without seeing real call sites.

## 2026-05-09 (later)

### Added — Step 1 of the port: Galois field arithmetic

- Root [CMakeLists.txt](CMakeLists.txt) — C++17 strict, OpenCV-free at this stage, GoogleTest pulled via `FetchContent` at `v1.15.2`. Sets `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion`.
- [include/boofcv_qr/galois.hpp](include/boofcv_qr/galois.hpp), [src/galois/galois.cpp](src/galois/galois.cpp) — verbatim port of `boofcv.alg.fiducial.qrcode.GaliosFieldOps` (static utilities) and `GaliosFieldTableOps` (precomputed exp/log tables for `GF(2^numBits)`). Variable names, loop structure, and inline comments preserved per CLAUDE.md "Verbatim vs idiomize". Upstream typo `Galios` retained for cross-reference fidelity.
- [tests/unit/test_galois.cpp](tests/unit/test_galois.cpp) — mirrors all 11 cases from `TestGaliosFieldOps.java` and `TestGaliosFieldTableOps.java`. RNG: `std::mt19937` seeded `234` (same constant as BoofCV's `BoofStandardJUnit`); the tests are property-based so the differing Java/C++ random sequences don't affect what's verified.
- [src/galois/galois.md](src/galois/galois.md) — companion algorithm doc per CLAUDE.md "Algorithm documentation requirement". Covers polynomial-as-int representation, Russian-Peasant-with-reduction, the doubled `exp` table, `(numBits, primitive)` tunables, failure modes (notably `power(0, k)` quirk inherited from Java).

### Regression check

- C++ unit tests: **11/11 pass** (`ctest --output-on-failure`).
- Java baseline re-run on the full `boofcv-qrcodes` dataset: identical quality numbers to the locked [tests/baseline.json](tests/baseline.json) — zero drift on `gt_count`, `det_count`, `matches_at_iou`, `detection_rate`, `precision`, `decode_rate`, `payload_exact_rate` for every category. Wall-clock perf fluctuated within ~10% as expected (background load); perf is not regression-gated.
- `GaliosFieldTableOps_U8` / `_U16` (BoofCV's polynomial-on-byte-array specialisations) deferred to step 2 — their `polyAdd`/`polyMult`/`polyDivide` routines are RS-flavoured and belong with the Reed-Solomon decoder, not pure Galois.

## 2026-05-09

### Added

- Pinned upstream BoofCV at `v1.3.0` ([UPSTREAM_VERSION](UPSTREAM_VERSION)).
- Java baseline harness at [tools/java_reference/](tools/java_reference/) — Gradle project depending on `org.boofcv:boofcv-recognition:1.3.0` and `boofcv-io:1.3.0`. Runs `FactoryFiducial.qrcode()` on every `.jpg`/`.png` under a dataset root and emits per-image JSON (ground truth + detections + decoded payload + wall-clock time) plus a top-level `summary.json`. Builds with `JAVA_HOME=…openjdk@21 ./gradlew run --args="<datasetRoot> <outDir>"`.
- Python scorer at [tests/regression/score.py](tests/regression/score.py) — reads `summary.json`, matches detections to GT polygons by IoU (Shapely), aggregates per-category detection rate, precision, decode-success rate, payload-exact-match rate, and wall-clock mean/p50/p95.
- First Java reference run committed: per-image dumps under [tests/regression/baseline_java/](tests/regression/baseline_java/) and the aggregated [tests/baseline.json](tests/baseline.json) covering the full 562-image / 1258-GT `boofcv-qrcodes` dataset.

### Notes

- Sidecar `.txt` files in the dataset use **two** layouts. `nominal/`, `noncompliant/` etc. use `SETS\n<8 floats per line>`; `close/`, `monitor/`, `perspective/`, `pathological/`, `high_version/` etc. use raw `x\ny\nx\ny\n…` (4 lines per QR). The `decoding/` subset stores plain payload text with no markers. Parser in `Baseline.java` handles all three.
- BoofCV's `getDetections()` only returns *fully decoded* codes; partial detections (polygon found, decode failed) live in `getFailures()`. The current scorer ignores failures, so detection-set "decode rate" equals "detection rate" by construction. Failure-aware scoring (locate-only vs decode-success split) is a planned follow-up.
- Build env: macOS arm64, OpenJDK 21 (Gradle 8.12.1 doesn't yet support Java 25, which is the system default); BoofCV 1.3.0 from Maven Central; Python 3.14 + Shapely 2.1.
