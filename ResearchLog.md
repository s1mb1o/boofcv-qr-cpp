# ResearchLog

## 2026-05-11 — Post-target performance profile and rejected image-line trial

### Why

Targets 3-8 banked the remaining items from the refreshed bottleneck list:
packed contour storage reuse, compact mixed line-DLT SVD, sliding Otsu local
histograms, polyline corner/list pooling, in-bounds contour edge sampling, and
the Reed-Solomon clean-codeword fast path. This pass checks the current tree
against the locked BoofCV Java timing baseline and refreshes the bottleneck map
before choosing any new target.

### Commands

Current full regression timing and parity gate:

```bash
bash tools/cli/run_regression.sh
```

Fixed-count probes:

```bash
build/qr_scan --profile \
  /Users/ashmelev/Projects/30_moonlighting/pricetag-vision-datasets/data/external/boofcv-qrcodes/qrcodes/detection/lots/image005.jpg \
  250
build/qr_scan --profile \
  /Users/ashmelev/Projects/30_moonlighting/pricetag-vision-datasets/data/external/boofcv-qrcodes/qrcodes/detection/bright_spots/image012.jpg \
  300
```

Sampling profiles:

```bash
mkdir -p /tmp/qr_boofcv_post_targets_20260511
build/qr_scan --profile \
  /Users/ashmelev/Projects/30_moonlighting/pricetag-vision-datasets/data/external/boofcv-qrcodes/qrcodes/detection/bright_spots/image012.jpg \
  10000
sample <pid> 10 -file /tmp/qr_boofcv_post_targets_20260511/sample_bright_spots_image012.txt
build/qr_scan --profile \
  /Users/ashmelev/Projects/30_moonlighting/pricetag-vision-datasets/data/external/boofcv-qrcodes/qrcodes/detection/lots/image005.jpg \
  10000
sample <pid> 10 -file /tmp/qr_boofcv_post_targets_20260511/sample_lots_image005.txt
```

### Current performance

Quality is unchanged: regression PASS, aggregate decode rate remains
byte-identical to the BoofCV Java baseline at **74.40%**.

Current C++ versus locked BoofCV Java detector-core timing:

| metric | BoofCV Java baseline | current C++ | C++ / Java |
|---|---:|---:|---:|
| Detector-core mean/image | 15.41 ms | 22.53 ms | 1.46x slower |
| Detector-core p50 | 5.36 ms | 10.04 ms | 1.87x slower |
| Detector-core p95 | 66.38 ms | 89.47 ms | 1.35x slower |

The current C++ regression run wrote
`tests/regression/baseline_cpp/summary.json` with `17351 ms total` for the
562-image scan. The earlier same-session Java batch timing recorded in this log
was `25841 ms total`, so the C++ CLI remains faster end-to-end while still
slower in detector-core per-image accounting.

Largest weighted detector-core contributors versus Java:

| category | Java mean | C++ mean | ratio | C++ weighted | extra vs Java |
|---|---:|---:|---:|---:|---:|
| `close` | 20.39 ms | 34.35 ms | 1.68x | 1374 ms | +558 ms |
| `bright_spots` | 50.07 ms | 67.12 ms | 1.34x | 2148 ms | +545 ms |
| `curved` | 16.71 ms | 27.28 ms | 1.63x | 1364 ms | +528 ms |
| `brightness` | 38.42 ms | 55.85 ms | 1.45x | 1564 ms | +488 ms |
| `glare` | 9.33 ms | 17.24 ms | 1.85x | 862 ms | +396 ms |
| `blurred` | 17.83 ms | 26.20 ms | 1.47x | 1179 ms | +377 ms |

### Fresh bottleneck samples

`bright_spots/image012.jpg`, 10-second `sample`, 8489 samples:

| function / cluster | samples | share |
|---|---:|---:|
| Finder / square detector total | 6937 | 81.7% |
| `DetectPolygonFromContour::process()` subtree | 6572 | 77.4% |
| `ContourTracer::searchOne8()` | 2929 | 34.5% |
| `ThresholdBlockOtsu::computeStatistics()` | 889 | 10.5% |
| `ContourTracer::trace()` | 480 | 5.7% |
| `ThresholdBlockOtsu::thresholdBlock()` | 461 | 5.4% |
| `ThresholdBlockOtsu::applyThreshold()` | 94 | 1.1% |
| `ImageLineIntegral::compute()` | 42 | 0.5% |
| `PolylineSplitMerge::computeSideError()` | 42 | 0.5% |
| `ContourEdgeIntensity::process()` | 39 | 0.5% |

`lots/image005.jpg`, 10-second `sample`, 8551 samples:

| function | samples | share |
|---|---:|---:|
| `ContourTracer::searchOne8()` | 1248 | 14.6% |
| `ThresholdBlockOtsu::computeStatistics()` | 632 | 7.4% |
| `ThresholdBlockOtsu::thresholdBlock()` | 569 | 6.7% |
| `QrCodeBinaryGridReader::readBitIntensity()` | 334 | 3.9% |
| `PolylineSplitMerge::computeSideError()` | 328 | 3.8% |
| `ImageLineIntegral::compute()` | 325 | 3.8% |
| `cv::JacobiSVDImpl_<double>()` | 304 | 3.6% |
| `ContourEdgeIntensity::process()` | 196 | 2.3% |
| `GaliosFieldTableOps::multiply()` | 160 | 1.9% |

### Rejected follow-up trial

Trial: inline `ImageLineIntegral::isInside()` in the header and replace
`pixel()`'s `cv::Mat::at<uint8_t>(y, x)` with `image_.ptr<uint8_t>(y)[x]`.
Rationale was the current `lots` sample's 3.8% `ImageLineIntegral::compute()`
island.

Correctness was fine:

```bash
ctest --test-dir build --output-on-failure -R 'ImageLineIntegral|SnapToLineEdge|RefinePolygonToGray|DetectPolygonBinaryGrayRefine|QrCodePositionPatternDetector'
```

Result: 26/26 PASS, but fixed probes regressed and the edit was reverted before
commit.

| image | before | trial | delta |
|---|---:|---:|---:|
| `lots/image005.jpg` (250 iters) | 116.30 ms/iter | 121.74 ms/iter | +4.7% |
| `bright_spots/image012.jpg` (300 iters) | 96.98 ms/iter | 101.88 ms/iter | +5.1% |

Do not retry this exact helper-inline / row-pointer edit without a better
microbenchmark explaining the regression. The likely explanation is that the
compiler/OpenCV release path already inlines `at()` well enough, while moving
`isInside()` into the header increased code size in refinement-heavy loops.

### Best next targets

1. **Contour tracer / labeler core remains the main bottleneck.**
   `ContourTracer::searchOne8()` is still the top leaf on both representative
   images, especially noisy high-resolution input. The easy arithmetic and
   packed-storage wins are already banked; another useful contour target likely
   needs a deeper row-scan or representation change with a dedicated parity
   audit.
2. **`ThresholdBlockOtsu` remains the broadest cross-category island.** The
   sliding local-histogram target helped the aggregate, but statistics +
   thresholding still account for ~12-16% on the sampled 4MP images. Any next
   Otsu change is parity-sensitive because this stage is also the documented
   source of monitor/glare residuals.
3. **The `lots` workload is now split across several 3-4% islands:**
   `readBitIntensity`, polyline side scoring, image-line integral, and residual
   mixed line-DLT SVD. These are too small for speculative edits; each needs
   the same fixed-probe + full-regression gate used above. The image-line
   helper shortcut already failed that gate.
4. **Reed-Solomon/Galois is still visible but low leverage.** The clean-block
   tail skip banked the safe no-op case. Remaining samples are inside syndrome
   computation, BM, Chien on genuinely non-trivial locators, and generator
   setup, so further RS changes are unlikely to move end-to-end QR detection
   unless the workload becomes much more decode-heavy.

## 2026-05-11 — Target 8: Reed-Solomon clean-codeword fast path

### Why

The refreshed `lots/image005` profile still showed
`GaliosFieldTableOps::multiply()` at roughly 1.7% of samples through the
Reed-Solomon correction path. A previous inline table-access trial failed the
perf gate, so this pass targeted a narrower no-op path: QR blocks whose
syndromes produce the trivial Berlekamp-Massey locator.

Before this change, `correct()` always ran Chien search across the whole block
and then entered Forney setup even when the locator polynomial was degree zero.
That preserved output, but spent Galois table operations to discover no
locations and correct nothing.

### Change

- After `findErrorLocatorPolynomialBM()`, `correct()` checks
  `errorLocatorPoly.size() == 1`.
- On that clean-codeword path, it clears `errorLocations` so
  `getTotalErrors()` cannot report stale positions from a previous correction,
  then returns `true`.
- The corrupted-codeword path is unchanged: non-trivial locators still go
  through brute-force Chien search and Forney correction.
- Added a typed regression test that first corrects a corrupted message, then
  runs a clean correction on the same decoder and verifies message/ECC
  stability plus zero reported errors for both generator bases.

The check is placed after BM instead of directly after syndromes so the public
`errorLocatorPoly` workspace still reflects the stage result (`[1]`) for
diagnostics and manual pipeline composition.

### Commands

```bash
cmake --build build --target boofcv_qr_tests qr_scan -- -j
ctest --test-dir build --output-on-failure -R 'ReedSolomon|Galois|QrCodeDecoderBits'
ctest --test-dir build --output-on-failure
build/qr_scan --profile \
  /Users/ashmelev/Projects/30_moonlighting/pricetag-vision-datasets/data/external/boofcv-qrcodes/qrcodes/detection/lots/image005.jpg \
  250
build/qr_scan --profile \
  /Users/ashmelev/Projects/30_moonlighting/pricetag-vision-datasets/data/external/boofcv-qrcodes/qrcodes/detection/bright_spots/image012.jpg \
  300
bash tools/cli/run_regression.sh
```

### Result

Focused Reed-Solomon/Galois/decoder-bit tests passed 37/37, and the full unit
suite passed 435/435.

Fixed-count profile comparison against target 7:

| image | before | after | delta |
|---|---:|---:|---:|
| `lots/image005.jpg` (250 iters) | 117.68 ms/iter | 116.30 ms/iter | -1.38 ms / -1.2% |
| `bright_spots/image012.jpg` (300 iters) | 97.74 ms/iter | 96.98 ms/iter | -0.76 ms / -0.8% |

Full regression:

| metric | target 7 | target 8 | delta |
|---|---:|---:|---:|
| `summary.total_elapsed_ms` | 17457 ms | 17351 ms | -0.6% |
| aggregate mean ms | 22.55 | 22.53 | -0.1% |
| `bright_spots` mean ms | 67.90 ms | 67.12 ms | -1.1% |

The category means remain noisy at this small target size, but both fixed
probes and full-run total elapsed moved in the right direction. Quality is
unchanged: regression PASS, aggregate decode rate remains byte-identical to
the BoofCV Java baseline at **74.40%**.

## 2026-05-11 — Target 7: in-bounds fast path for `ContourEdgeIntensity`

### Why

The post-target-6 profile cluster still included edge scoring in the
polyline/edge bucket. `ContourEdgeIntensity::process()` already rejects tangent
samples outside the image before sampling, but the private bilinear sampler then
repeated the border clamp, used `std::floor()`, and fetched pixels through
`cv::Mat::at()`.

### Change

- Added `sampleInside()`, a bilinear sampler for coordinates already known to
  satisfy `0 <= x <= width - 1` and `0 <= y <= height - 1`.
- Kept the existing clamped `sample()` helper semantics by forwarding to
  `sampleInside()` after clamping.
- Switched the process hot path to call `sampleInside()` only inside the
  existing bounds checks.
- Hoisted the contour size and image max bounds out of the loop.

This preserves sample positions and interpolation weights. Positive-coordinate
`static_cast<int32_t>` is equivalent to `floor()` for the in-bounds calls, and
the `x1/y1 = min(x0/y0 + 1, max)` edge behaviour is unchanged.

### Commands

```bash
cmake --build build --target boofcv_qr_tests qr_scan -- -j
ctest --test-dir build --output-on-failure -R 'ContourEdgeIntensity|DetectPolygonFromContour|QrCodePositionPatternDetector'
ctest --test-dir build --output-on-failure
build/qr_scan --profile \
  /Users/ashmelev/Projects/30_moonlighting/pricetag-vision-datasets/data/external/boofcv-qrcodes/qrcodes/detection/lots/image005.jpg \
  250
build/qr_scan --profile \
  /Users/ashmelev/Projects/30_moonlighting/pricetag-vision-datasets/data/external/boofcv-qrcodes/qrcodes/detection/bright_spots/image012.jpg \
  300
bash tools/cli/run_regression.sh
```

### Result

Fixed-count profile comparison against target 6:

| image | before | after | delta |
|---|---:|---:|---:|
| `lots/image005.jpg` (250 iters) | 120.58 ms/iter | 117.68 ms/iter | -2.90 ms / -2.4% |
| `bright_spots/image012.jpg` (300 iters) | 99.74 ms/iter | 97.74 ms/iter | -2.00 ms / -2.0% |

Full regression timing was near-flat but directionally positive on detector
mean:

| metric | target 6 | target 7 | delta |
|---|---:|---:|---:|
| `summary.total_elapsed_ms` | 17460 ms | 17457 ms | flat |
| aggregate mean ms | 22.62 | 22.55 | -0.3% |
| `bright_spots` mean ms | 68.79 | 67.90 | -1.3% |

Quality is unchanged: regression PASS, aggregate decode rate remains
byte-identical to the BoofCV Java baseline at **74.40%**.

## 2026-05-11 — Target 6: pooled polyline corner/list storage

### Why

The refreshed `lots/image005` profile still showed the polygon corner-finder
cluster: `PolylineSplitMerge::computeSideError()` at roughly 3.5%,
`MaximumLineDistance::selectSplitPoint()` around 2.6%, and
`ContourEdgeIntensity::process()` around 2.5%. A previous invariant-hoist trial
failed the perf gate, so this pass targeted allocation overhead around the
verbatim split/merge algorithm instead of changing scoring math.

### Change

- `CornerPool::reset()` now preserves allocated `Corner` objects and resets only
  the logical active size. `grow()` returns a reset retained object when
  available.
- Replaced the internal `std::list<Corner*>` with a small linked list over
  recycled nodes. The iterator contract used by the BoofCV-port code is
  unchanged: `end()` is still the null-element sentinel, and `next()` /
  `previous()` keep their wrap-around behaviour.
- Added tests for corner-pool state clearing and list reset link cleanup.
- A corner-object-only trial passed correctness but was noisy/mixed on the
  aggregate timing gate. The retained change includes list-node pooling because
  `std::list` node allocation was still happening for every contour fit.

### Commands

```bash
cmake --build build --target boofcv_qr_tests qr_scan -- -j
ctest --test-dir build --output-on-failure -R PolylineSplitMerge
ctest --test-dir build --output-on-failure
build/qr_scan --profile \
  /Users/ashmelev/Projects/30_moonlighting/pricetag-vision-datasets/data/external/boofcv-qrcodes/qrcodes/detection/lots/image005.jpg \
  250
build/qr_scan --profile \
  /Users/ashmelev/Projects/30_moonlighting/pricetag-vision-datasets/data/external/boofcv-qrcodes/qrcodes/detection/bright_spots/image012.jpg \
  300
bash tools/cli/run_regression.sh
```

### Result

Fixed-count profile comparison against target 5:

| image | before | after | delta |
|---|---:|---:|---:|
| `lots/image005.jpg` (250 iters) | 125.52 ms/iter | 120.58 ms/iter | -4.94 ms / -3.9% |
| `bright_spots/image012.jpg` (300 iters) | 101.73 ms/iter | 99.74 ms/iter | -1.99 ms / -2.0% |

Full regression timing:

| metric | target 5 | target 6 | delta |
|---|---:|---:|---:|
| `summary.total_elapsed_ms` | 17799 ms | 17460 ms | -339 ms / -1.9% |
| aggregate mean ms | 23.10 | 22.62 | -2.1% |
| `brightness` mean ms | 57.43 | 56.57 | -1.5% |
| `curved` mean ms | 28.01 | 27.11 | -3.2% |
| `lots` mean ms | 111.66 | 107.24 | -4.0% |

Quality is unchanged: regression PASS, aggregate decode rate remains
byte-identical to the BoofCV Java baseline at **74.40%**.

## 2026-05-11 — Target 5: sliding local histograms in `ThresholdBlockOtsu`

### Why

The refreshed profile still showed `ThresholdBlockOtsu` at roughly 14-16% of samples on 4MP images. A previous tiny arithmetic cleanup failed the fixed-count perf gate, so this target focused on data movement that preserves the exact integer histograms passed into the unchanged Otsu computation.

### Change

- Removed duplicate zeroing in `computeBlockStatistics()`. `process()` already clears the full `stats_` buffer with `stats_.assign(..., 0)` before every histogram build, and each block is computed once.
- Split `thresholdBlock()` so it consumes a prepared histogram instead of constructing the local 3×3 neighbourhood itself.
- For `thresholdFromLocalBlocks=true`, `applyThreshold()` now builds per-column vertical sums for the current three block rows, then slides a three-column horizontal window across the row. The resulting histogram for each block is identical to summing the same 3×3 neighbourhood directly, but avoids re-adding all nine source histograms for every adjacent block.
- For `thresholdFromLocalBlocks=false`, `applyThreshold()` passes each block's own histogram directly into `thresholdBlock()`.
- Added `ThresholdBlockOtsu.thresholdFromSingleBlocks` to exercise the non-local branch.

### Commands

```bash
cmake --build build --target boofcv_qr_tests qr_scan -- -j
ctest --test-dir build --output-on-failure -R ThresholdBlockOtsu
ctest --test-dir build --output-on-failure
build/qr_scan --profile \
  /Users/ashmelev/Projects/30_moonlighting/pricetag-vision-datasets/data/external/boofcv-qrcodes/qrcodes/detection/bright_spots/image012.jpg \
  300
build/qr_scan --profile \
  /Users/ashmelev/Projects/30_moonlighting/pricetag-vision-datasets/data/external/boofcv-qrcodes/qrcodes/detection/lots/image005.jpg \
  250
bash tools/cli/run_regression.sh
```

### Result

Fixed-count profile comparison against target 4:

| image | before | after | delta |
|---|---:|---:|---:|
| `bright_spots/image012.jpg` (300 iters) | 104.51 ms/iter | 101.73 ms/iter | -2.78 ms / -2.7% |
| `lots/image005.jpg` (250 iters) | 123.13 ms/iter | 125.52 ms/iter | +2.39 ms / +1.9% |

Full regression timing:

| metric | target 4 | target 5 | delta |
|---|---:|---:|---:|
| `summary.total_elapsed_ms` | 17858 ms | 17799 ms | -59 ms / -0.3% |
| aggregate mean ms | 23.21 | 23.10 | -0.5% |
| `bright_spots` mean ms | 68.87 | 68.52 | -0.5% |
| `brightness` mean ms | 57.64 | 57.43 | -0.4% |
| `curved` mean ms | 29.62 | 28.01 | -5.4% |
| `lots` mean ms | 112.37 | 111.66 | -0.6% |

Quality is unchanged: regression PASS, aggregate decode rate remains byte-identical to the BoofCV Java baseline at **74.40%**. The fixed `lots/image005` probe worsened, but the full `lots` category and aggregate timing improved, so this target passed the broader dataset gate.

## 2026-05-11 — Target 4: compact SVD for `setTransformFromLinesSquare`

### Why

After target 3, the next marked sampler item was the residual `setTransformFromLinesSquare()` SVD path on multi-QR workloads. A previous fixed 9×9 normal-equation eigensolve passed coarse tests but degraded the strict rotated line-DLT fixture from machine precision to about 1e-5 grid units, so this target keeps the SVD null-space objective and removes avoidable work around the fixed-size solve.

### Change

- Kept the existing BoofCV row equations: 3 point correspondences contribute 6 rows and 4 direction-line correspondences contribute 8 rows.
- Replaced heap `cv::Mat::zeros(14, 9, CV_64F)` with stack-backed `cv::Matx<double,14,9>`.
- Replaced `cv::SVDecomp(A, w, u, vt, cv::SVD::FULL_UV)` with `cv::SVD::compute(A, w, cv::noArray(), vt)`. The right singular vectors in `vt` are still computed, but OpenCV no longer builds the unused U/full-UV basis.

### Commands

Same-session pre-change fixed probes:

```bash
build/qr_scan --profile \
  /Users/ashmelev/Projects/30_moonlighting/pricetag-vision-datasets/data/external/boofcv-qrcodes/qrcodes/detection/lots/image005.jpg \
  250
build/qr_scan --profile \
  /Users/ashmelev/Projects/30_moonlighting/pricetag-vision-datasets/data/external/boofcv-qrcodes/qrcodes/detection/bright_spots/image012.jpg \
  300
```

Post-change verification:

```bash
cmake --build build --target boofcv_qr_tests qr_scan -- -j
ctest --test-dir build --output-on-failure -R 'QrCodeBinaryGridToPixel|SetTransformFromLinesSquare'
ctest --test-dir build --output-on-failure
build/qr_scan --profile \
  /Users/ashmelev/Projects/30_moonlighting/pricetag-vision-datasets/data/external/boofcv-qrcodes/qrcodes/detection/lots/image005.jpg \
  250
build/qr_scan --profile \
  /Users/ashmelev/Projects/30_moonlighting/pricetag-vision-datasets/data/external/boofcv-qrcodes/qrcodes/detection/bright_spots/image012.jpg \
  300
bash tools/cli/run_regression.sh
```

### Result

Fixed-count profile comparison:

| image | before | after | delta |
|---|---:|---:|---:|
| `lots/image005.jpg` (250 iters) | 130.17 ms/iter | 123.13 ms/iter | -7.04 ms / -5.4% |
| `bright_spots/image012.jpg` (300 iters) | 103.52 ms/iter | 104.51 ms/iter | +0.99 ms / +1.0% |

Full regression quality is unchanged: aggregate decode rate remains byte-identical to the BoofCV Java baseline at **74.40%**. The timing effect is local to the multi-QR path: `lots` mean improved from target 3's 115.22 ms to 112.37 ms, while aggregate mean was effectively flat at 23.18 -> 23.21 ms and total elapsed was 17827 -> 17858 ms.

The strict line-DLT numeric tests still pass, including `QrCodeBinaryGridToPixel.setTransformFromLinesSquare_rotated` at 1e-9 tolerance. This keeps the safer SVD behavior while trimming the residual SVD cost.

## 2026-05-11 — Target 3: packed contour block reuse and block-wise materialisation

### Why

The refreshed profile after targets 1 and 2 still showed contour extraction as the largest remaining cost on noisy high-resolution images. `ContourTracer::searchOne8()` remained the top leaf, while `PackedSetsPoint2D_I32::addPointToTail/grow` and contour materialisation were visible around it. This target keeps Chang contour tracing unchanged and trims allocation/indexing overhead in the packed point representation.

### Change

- `PackedSetsPoint2D_I32::reset()` now preserves allocated point blocks behind a logical `activeBlocks` count instead of resizing the backing vector back to a single block on every image.
- `grow()`, `removeTail()`, and `addPointToTail()` update that logical active count while reusing already allocated block buffers when possible.
- Added `PackedSetsPoint2D_I32::appendSetTo()`, which copies one stored contour block-by-block into a `std::vector<cv::Point2i>` and avoids the iterator path's per-point division/modulo.
- `DetectPolygonFromContour::buildContoursFromPort()` now uses `appendSetTo()` for external and internal contour materialisation.
- A broader trial also reused `DetectPolygonFromContour`'s per-frame `Contour` and `DetectedInfo` vector slots. It improved `bright_spots/image012` but regressed `lots/image005` and the full-regression `lots` category, so that part was dropped before commit.

### Commands

```bash
cmake --build build --target boofcv_qr_tests qr_scan -- -j
ctest --test-dir build --output-on-failure
build/qr_scan --profile \
  /Users/ashmelev/Projects/30_moonlighting/pricetag-vision-datasets/data/external/boofcv-qrcodes/qrcodes/detection/bright_spots/image012.jpg \
  300
build/qr_scan --profile \
  /Users/ashmelev/Projects/30_moonlighting/pricetag-vision-datasets/data/external/boofcv-qrcodes/qrcodes/detection/lots/image005.jpg \
  250
bash tools/cli/run_regression.sh
```

### Result

Fixed-count profile comparison against the refreshed post-target-2 baseline:

| image | refreshed baseline | target 3 | delta |
|---|---:|---:|---:|
| `bright_spots/image012.jpg` (300 iters) | 107.19 ms/iter | 103.81 ms/iter | -3.38 ms / -3.2% |
| `lots/image005.jpg` (250 iters) | 128.77 ms/iter | 127.11 ms/iter | -1.66 ms / -1.3% |

Full regression timing stayed directionally positive and quality remained unchanged:

| metric | refreshed baseline | target 3 | delta |
|---|---:|---:|---:|
| `summary.total_elapsed_ms` | 18873 ms | 17827 ms | -1046 ms / -5.5% |
| aggregate mean ms | 24.73 | 23.18 | -6.3% |
| `bright_spots` mean ms | 74.73 | 68.72 | -8.0% |
| `lots` mean ms | 117.66 | 115.22 | -2.1% |

Regression PASS: aggregate decode rate remains byte-identical to the BoofCV Java baseline at **74.40%**, with only the documented accepted residual categories out-of-band.

## 2026-05-11 — Post-target performance comparison and refreshed bottleneck profile

### Why

After landing the contour-tracer arithmetic cleanup and the pure-point grid-transform eigensolve, refresh the C++ vs original BoofCV Java comparison on the BoofCV `qrcodes_v3` dataset and re-sample the representative slow images. The goal is to verify that quality held, quantify the new perf state, and identify the next bottlenecks after the first two targets moved.

### Commands

Regression and scoring against the locked BoofCV Java 1.3.0 baseline:

```bash
cmake --build build --target qr_scan boofcv_qr_tests -- -j
bash tools/cli/run_regression.sh
```

Fixed-count image profiles:

```bash
build/qr_scan --profile \
  /Users/ashmelev/Projects/30_moonlighting/pricetag-vision-datasets/data/external/boofcv-qrcodes/qrcodes/detection/bright_spots/image012.jpg \
  300
build/qr_scan --profile \
  /Users/ashmelev/Projects/30_moonlighting/pricetag-vision-datasets/data/external/boofcv-qrcodes/qrcodes/detection/lots/image005.jpg \
  250
```

Sampling profiles:

```bash
OUT=/tmp/qr_boofcv_perf_again_20260511_130522
build/qr_scan --profile detection/bright_spots/image012.jpg 10000 &
sample <pid> 10 -file "$OUT/sample_bright_spots_image012.txt"
build/qr_scan --profile detection/lots/image005.jpg 10000 &
sample <pid> 10 -file "$OUT/sample_lots_image005.txt"
```

### Result

Quality is unchanged: regression PASS, aggregate decode rate remains byte-identical to the BoofCV Java baseline at **74.40%**. The only out-of-band categories are the existing accepted residuals (`bright_spots`, `glare`, `monitor`, `noncompliant`, `perspective`).

Performance comparison:

| metric | BoofCV Java baseline | current C++ | C++ vs Java |
|---|---:|---:|---:|
| Full batch wall time (`total_elapsed_ms`) | 25.841 s | 18.873 s | C++ 1.37x faster |
| Detector-core mean/image | 15.41 ms | 24.73 ms | C++ 1.61x slower |
| Detector-core p50 | 5.36 ms | 10.61 ms | C++ 1.98x slower |
| Detector-core p95 | 66.38 ms | 97.55 ms | C++ 1.47x slower |

Weighted category contributors, sorted by current C++ time:

| category | Java mean | C++ mean | ratio | C++ weighted | extra vs Java |
|---|---:|---:|---:|---:|---:|
| `bright_spots` | 50.07 ms | 74.73 ms | 1.49x | 2391 ms | +789 ms |
| `brightness` | 38.42 ms | 62.21 ms | 1.62x | 1742 ms | +666 ms |
| `close` | 20.39 ms | 39.66 ms | 1.94x | 1586 ms | +771 ms |
| `curved` | 16.71 ms | 29.82 ms | 1.78x | 1491 ms | +655 ms |
| `blurred` | 17.83 ms | 29.09 ms | 1.63x | 1309 ms | +507 ms |
| `glare` | 9.33 ms | 18.35 ms | 1.97x | 918 ms | +451 ms |
| `lots` | 110.90 ms | 117.66 ms | 1.06x | 824 ms | +47 ms |
| `nominal` | 8.93 ms | 12.28 ms | 1.38x | 798 ms | +218 ms |

Fixed-count profiles on the two sampled images:

| image | original profile | after target 2 | refreshed current | delta vs original |
|---|---:|---:|---:|---:|
| `bright_spots/image012.jpg` (300 iters) | 112.26 ms/iter | 107.04 ms/iter | 107.19 ms/iter | -4.5% |
| `lots/image005.jpg` (250 iters) | 142.15 ms/iter | 127.47 ms/iter | 128.77 ms/iter | -9.4% |

### Refreshed bottlenecks

#### `bright_spots/image012.jpg`

10-second `sample`: 8556 samples.

| cluster / function | samples | share |
|---|---:|---:|
| Finder / square detector total | 7060 | 82.5% |
| `DetectPolygonFromContour` / contour extraction | 6694 | 78.2% |
| `ContourTracer::searchOne8()` | 2831 | 33.1% |
| `ThresholdBlockOtsu::computeStatistics()` | 821 | 9.6% |
| `ThresholdBlockOtsu::thresholdBlock()` | 559 | 6.5% |
| `ContourTracer::trace()` | 526 | 6.1% |

Interpretation: the noisy high-resolution case is still contour-tracer dominated. Target 1 trimmed arithmetic overhead but did not change the basic cost shape; remaining contour wins likely need deeper representation/memory work, not another small `%`/division cleanup.

#### `lots/image005.jpg`

10-second `sample`: 8530 samples.

| function | samples | share |
|---|---:|---:|
| `ContourTracer::searchOne8()` | 1171 | 13.7% |
| `ThresholdBlockOtsu::thresholdBlock()` | 628 | 7.4% |
| `ThresholdBlockOtsu::computeStatistics()` | 581 | 6.8% |
| `cv::JacobiSVDImpl_<double>()` | 458 | 5.4% |
| `PolylineSplitMerge::computeSideError()` | 301 | 3.5% |
| `QrCodeBinaryGridReader::readBitIntensity()` | 293 | 3.4% |
| `ImageLineIntegral::compute()` | 287 | 3.4% |
| `GaliosFieldTableOps::multiply()` | 144 | 1.7% |

Interpretation: target 2 removed the previous N-point `cv::SVD::solveZ` hotspot, but `cv::JacobiSVDImpl_<double>()` remains through `QrCodeBinaryGridToPixel::setTransformFromLinesSquare()` during unknown-version setup. The residual SVD island is much smaller than before (13.4% -> 5.4% on this sample) and is now comparable to Otsu/polyline/edge scoring.

### Best next targets

1. **Contour-stage memory/representation work.** `searchOne8()` is still the dominant leaf, and the samples show visible `PackedSetsPoint2D_I32::addPointToTail/grow` and contour-build cost. The next meaningful contour attempt should target point-storage reuse or reducing contour materialisation overhead, with full regression gating.
2. **`setTransformFromLinesSquare()` SVD.** The pure-point transform path is improved, but the line-correspondence unknown-version path still uses OpenCV SVD. A replacement needs a tighter numeric parity gate than the point path because the previous normal-equation trial caused measurable rotated-line fixture drift.
3. **`ThresholdBlockOtsu`.** Still 14-16% on 4MP images. Prior micro-cleanup failed the fixed-count gate, so useful gains likely require a broader histogram/data-layout or parallel-block strategy and must be treated as parity-sensitive.
4. **Polyline / edge-scoring allocation and scan cost.** Individually smaller than contour/Otsu but now prominent on `lots`; the previous invariant-hoist trial failed the perf gate, so object reuse or reducing saved-polyline allocation is a better angle.
5. **Reed-Solomon / Galois.** Still visible at ~1.7% on `lots`, but an inline table-access trial failed the perf gate. Keep it low priority unless the workload becomes decode-heavy with many QRs per frame.

## 2026-05-11 — Target 2: pure-point grid-transform null-space solve

### Why

The fresh bottleneck profile showed `cv::JacobiSVDImpl_<double>()` at 13.4% of samples on `lots/image005.jpg`, reached from repeated QR grid-transform DLT solves. After target 1, contour extraction improved modestly, making the multi-QR transform path the next best isolated target.

### Change

- Kept `computeTransform()` on the same pure-DLT point-correspondence row equations that replaced `cv::findHomography` in ADR 03.
- Replaced the N>4 point path's 2N×9 `cv::SVD::solveZ(A, h)` with direct accumulation of `A^T A` into a fixed 9×9 matrix and `cv::eigen()` on that symmetric matrix.
- Kept `setTransformFromLinesSquare()` on its previous explicit matrix + `cv::SVDecomp` implementation. A normal-equation trial passed coarse checks but degraded the rotated line-DLT fixture from machine precision to about 1e-5 grid units; that path is rough pre-version setup and not the sampled hotspot.
- Added `QrCodeBinaryGridToPixel.computeTransform_manyPoints_perspective` to cover the N>4 point-DLT path on a noise-free projective fixture and probes not used by the fit.

### Commands

Short noise check:

```bash
for img in \
  /Users/ashmelev/Projects/30_moonlighting/pricetag-vision-datasets/data/external/boofcv-qrcodes/qrcodes/detection/bright_spots/image012.jpg \
  /Users/ashmelev/Projects/30_moonlighting/pricetag-vision-datasets/data/external/boofcv-qrcodes/qrcodes/detection/lots/image005.jpg; do
  for i in 1 2 3; do
    build/qr_scan --profile "$img" 50
  done
done
```

Profile-count comparison:

```bash
build/qr_scan --profile \
  /Users/ashmelev/Projects/30_moonlighting/pricetag-vision-datasets/data/external/boofcv-qrcodes/qrcodes/detection/bright_spots/image012.jpg \
  300
build/qr_scan --profile \
  /Users/ashmelev/Projects/30_moonlighting/pricetag-vision-datasets/data/external/boofcv-qrcodes/qrcodes/detection/lots/image005.jpg \
  250
```

Regression:

```bash
bash tools/cli/run_regression.sh
```

### Result

Short 50-iteration runs after the change:

| image | runs ms/iter | best | median |
|---|---:|---:|---:|
| `bright_spots/image012.jpg` | 106.51, 107.23, 107.19 | 106.51 | 107.19 |
| `lots/image005.jpg` | 131.63, 127.98, 133.49 | 127.98 | 131.63 |

Same iteration counts as the prior profile/target logs:

| image | original profile | after target 1 | after target 2 | target-2 delta |
|---|---:|---:|---:|---:|
| `bright_spots/image012.jpg` (300 iters) | 112.26 ms/iter | 107.65 ms/iter | 107.04 ms/iter | -0.61 ms / -0.6% |
| `lots/image005.jpg` (250 iters) | 142.15 ms/iter | 140.26 ms/iter | 127.47 ms/iter | -12.79 ms / -9.1% |

Full regression timing:

| metric | after target 1 | after target 2 | delta |
|---|---:|---:|---:|
| `summary.total_elapsed_ms` | 18629 ms | 18184 ms | -445 ms |
| aggregate mean ms | 24.66 | 23.95 | -2.9% |
| `lots` mean ms | 124.56 | 112.59 | -9.6% |

### Interpretation

The target landed where expected: the multi-QR workload improved substantially, while the contour-heavy noisy image stayed flat. The full-regression speedup is smaller because `lots` is only 7 of 562 images, but it removes a major sampled decoder-side hotspot without changing output quality.

### Verification

`ctest --test-dir build --output-on-failure` passed 429/429 tests. `bash tools/cli/run_regression.sh` also passed: aggregate C++ decode rate stayed byte-identical to the Java BoofCV baseline at 74.40%, with only the already accepted residual categories out-of-band.

## 2026-05-11 — Target 1: contour-tracer arithmetic cleanup

### Why

The fresh bottleneck profile identified `ContourTracer::searchOne8()` as the top self-time function on `bright_spots/image012.jpg` (35.6% of samples) and a leading function on `lots/image005.jpg` (14.0%). The first target was intentionally narrow: remove arithmetic overhead from the contour-walk representation while preserving direction order, label writes, seed handling, and emitted contour points.

### Change

- Added coordinate-offset tables matching the existing direction-indexed linear-offset tables.
- Changed `moveToNext()` to update `(x, y)` by the selected direction delta instead of recomputing `(x, y)` from `indexBinary` with division and modulo.
- Changed unrolled neighbour direction wrap from `% 4` / `% 8` to `& 3` / `& 7`; the tracer invariant keeps `dir` in range, so this is exactly equivalent for the 4- and 8-connected rule sizes.

### Commands

Short noise check:

```bash
for img in \
  /Users/ashmelev/Projects/30_moonlighting/pricetag-vision-datasets/data/external/boofcv-qrcodes/qrcodes/detection/bright_spots/image012.jpg \
  /Users/ashmelev/Projects/30_moonlighting/pricetag-vision-datasets/data/external/boofcv-qrcodes/qrcodes/detection/lots/image005.jpg; do
  for i in 1 2 3; do
    build/qr_scan --profile "$img" 50
  done
done
```

Profile-count comparison:

```bash
build/qr_scan --profile \
  /Users/ashmelev/Projects/30_moonlighting/pricetag-vision-datasets/data/external/boofcv-qrcodes/qrcodes/detection/bright_spots/image012.jpg \
  300
build/qr_scan --profile \
  /Users/ashmelev/Projects/30_moonlighting/pricetag-vision-datasets/data/external/boofcv-qrcodes/qrcodes/detection/lots/image005.jpg \
  250
```

### Result

Short 50-iteration runs after the change:

| image | runs ms/iter | best | median |
|---|---:|---:|---:|
| `bright_spots/image012.jpg` | 107.22, 105.14, 105.46 | 105.14 | 105.46 |
| `lots/image005.jpg` | 141.04, 140.77, 139.90 | 139.90 | 140.77 |

Same iteration counts as the original profile:

| image | before | after | delta |
|---|---:|---:|---:|
| `bright_spots/image012.jpg` (300 iters) | 112.26 ms/iter | 107.65 ms/iter | -4.61 ms / -4.1% |
| `lots/image005.jpg` (250 iters) | 142.15 ms/iter | 140.26 ms/iter | -1.89 ms / -1.3% |

### Interpretation

The change moves the contour-heavy high-resolution workload in the expected direction without changing detector output. The multi-QR workload improves only slightly because its cost is split between contour extraction and decoder-side transform / sampling work; that confirms the next target should be the small-matrix transform path rather than more contour-only cleanup unless a new profile says otherwise.

### Verification

`bash tools/cli/run_regression.sh` passed after the change. Aggregate C++ decode rate stayed byte-identical to the Java BoofCV baseline at 74.40%; the only out-of-band categories were the already documented accepted residuals (`bright_spots`, `glare`, `monitor`, `noncompliant`, `perspective`).

## 2026-05-11 — Fresh performance comparison and C++ bottleneck profile on `qrcodes_v3`

### Why

Compare current C++ performance against the original BoofCV Java reference on the same BoofCV `qrcodes_v3` dataset, then profile representative slow C++ images after the `LinearContourLabelChang2004` port reshaped the old `cv::findContours` bottleneck.

### Commands

Fresh Java reference:

```bash
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home \
  ./gradlew --no-daemon --quiet run \
  --args="/Users/ashmelev/Projects/30_moonlighting/pricetag-vision-datasets/data/external/boofcv-qrcodes/qrcodes /tmp/qr_boofcv_perf_20260511_121749/java"
```

Fresh C++ run and scoring:

```bash
build/qr_scan \
  /Users/ashmelev/Projects/30_moonlighting/pricetag-vision-datasets/data/external/boofcv-qrcodes/qrcodes \
  /tmp/qr_boofcv_perf_20260511_121749/cpp
python3 tests/regression/score.py /tmp/qr_boofcv_perf_20260511_121749/java/summary.json /tmp/qr_boofcv_perf_20260511_121749/java_score.json --iou 0.5
python3 tests/regression/score.py /tmp/qr_boofcv_perf_20260511_121749/cpp/summary.json /tmp/qr_boofcv_perf_20260511_121749/cpp_score.json --iou 0.5
```

Sampling profiles:

```bash
build/qr_scan --profile detection/bright_spots/image012.jpg 300
sample <pid> 10 -file /tmp/qr_boofcv_perf_20260511_121749/sample_bright_spots_image012.txt

build/qr_scan --profile detection/lots/image005.jpg 250
sample <pid> 10 -file /tmp/qr_boofcv_perf_20260511_121749/sample_lots_image005.txt
```

### Headline results

There are two different performance stories depending on what is being measured:

| metric | Java BoofCV | current C++ | C++ vs Java |
|---|---:|---:|---:|
| Full batch wall time (`summary.total_elapsed_ms`) | 24.976 s | 18.627 s | C++ 1.34x faster |
| Detector-core sum (`sum elapsed_ms`) | 8.319 s | 13.908 s | C++ 1.67x slower |
| Detector-core mean/image | 14.80 ms | 24.75 ms | C++ 1.67x slower |
| Harness overhead | 16.657 s | 4.719 s | C++ 3.53x less overhead |

The full C++ CLI run is faster because the Java reference harness spends most of its wall time outside `detector.process()` - image IO, sidecar parsing, Jackson pretty JSON, and keeping all records before writing `summary.json`. The library-level comparison is the detector-core number: current C++ is still about **1.67x slower than Java** on this dataset.

Quality stayed unchanged from the parity run: aggregate decode rate is 74.40% on both sides.

### Per-category detector-core comparison

Sorted by current C++ detector time contribution:

| category | images | Java mean ms | C++ mean ms | ratio | C++ weighted ms | extra vs Java ms |
|---|---:|---:|---:|---:|---:|---:|
| `bright_spots` | 32 | 46.84 | 75.18 | 1.60x | 2406 | +907 |
| `brightness` | 28 | 37.54 | 62.91 | 1.68x | 1762 | +710 |
| `close` | 40 | 19.96 | 38.06 | 1.91x | 1522 | +724 |
| `curved` | 50 | 16.43 | 29.63 | 1.80x | 1481 | +660 |
| `blurred` | 45 | 17.09 | 27.64 | 1.62x | 1244 | +475 |
| `glare` | 50 | 9.32 | 18.55 | 1.99x | 927 | +461 |
| `lots` | 7 | 111.86 | 129.76 | 1.16x | 908 | +125 |
| `nominal` | 65 | 6.08 | 12.61 | 2.07x | 820 | +425 |
| `monitor` | 17 | 25.91 | 42.81 | 1.65x | 728 | +287 |
| `high_version` | 33 | 12.38 | 17.08 | 1.38x | 564 | +155 |
| `rotations` | 44 | 6.93 | 12.37 | 1.78x | 544 | +239 |
| `damaged` | 37 | 5.89 | 11.68 | 1.98x | 432 | +214 |
| `shadows` | 14 | 12.04 | 21.11 | 1.75x | 295 | +127 |
| `noncompliant` | 16 | 3.86 | 7.53 | 1.95x | 120 | +59 |
| `perspective` | 35 | 1.44 | 2.96 | 2.06x | 104 | +53 |
| `decoding` | 26 | 2.83 | 1.43 | 0.50x | 37 | -36 |
| `pathological` | 23 | 0.42 | 0.57 | 1.37x | 13 | +4 |

The extra detector time is distributed; `bright_spots`, `close`, `brightness`, `curved`, `blurred`, `glare`, and `nominal` are the largest aggregate contributors. `lots` has high absolute latency but small regression-set weight and only a 1.16x ratio.

### Slowest current C++ images

Top C++ elapsed images are the 60-QR `lots` images, then high-resolution `bright_spots` / `brightness` images:

| image | C++ ms | Java ms | ratio | detections | failures |
|---|---:|---:|---:|---:|---:|
| `detection/lots/image005.jpg` | 137.98 | 149.55 | 0.92x | 60 | 395 |
| `detection/lots/image006.jpg` | 135.18 | 117.72 | 1.15x | 60 | 393 |
| `detection/lots/image003.jpg` | 131.23 | 100.44 | 1.31x | 60 | 397 |
| `detection/lots/image004.jpg` | 130.71 | 101.88 | 1.28x | 59 | 396 |
| `detection/lots/image007.jpg` | 128.16 | 109.16 | 1.17x | 60 | 385 |
| `detection/bright_spots/image012.jpg` | 110.81 | 78.38 | 1.41x | 2 | 3 |
| `detection/bright_spots/image008.jpg` | 109.61 | 76.96 | 1.42x | 2 | 6 |
| `detection/bright_spots/image011.jpg` | 109.53 | 76.80 | 1.43x | 1 | 5 |

### C++ bottleneck profiles

#### `bright_spots/image012.jpg` - noisy high-resolution image

Profile loop: 3024x4032 image, 300 iterations, 112.26 ms/iter.

Stage-level sample split from `sample`:

| stage / function cluster | samples | share |
|---|---:|---:|
| Finder / square detector total | 7015 / 8470 | 82.8% |
| `DetectPolygonFromContour::process` / contour extraction | 6664 / 8470 | 78.7% |
| `ThresholdBlockOtsu` total | 1368 / 8470 | 16.2% |
| `QrCodeDecoderImage` decode path | 16 / 8470 | 0.2% |

Top collapsed stacks:

| function | samples | share |
|---|---:|---:|
| `ContourTracer::searchOne8()` | 3018 | 35.6% |
| `ThresholdBlockOtsu::computeStatistics()` | 810 | 9.6% |
| `ThresholdBlockOtsu::thresholdBlock()` | 532 | 6.3% |
| `ContourTracer::trace()` | 428 | 5.1% |
| `PolylineSplitMerge::computeSideError()` | 44 | 0.5% |

Interpretation: on noisy high-resolution images, the current bottleneck is the BoofCV contour tracer, not QR decode. This is the expected post-`cv::findContours` shape: the expensive region moved from OpenCV's contour implementation to the verbatim C++ contour port. The hot inner loop is `ContourTracer::searchOne8()`.

#### `lots/image005.jpg` - 60 QR codes plus hundreds of failures

Profile loop: 4032x3024 image, 250 iterations, 142.15 ms/iter.

Stage-level sample split:

| stage / function cluster | samples | share |
|---|---:|---:|
| Finder / square detector total | 4767 / 8553 | 55.7% |
| Decoder / orchestrator total | 2516 / 8553 | 29.4% |
| `ThresholdBlockOtsu` total | 1127 / 8553 | 13.2% |

Top collapsed stacks:

| function | samples | share |
|---|---:|---:|
| `ContourTracer::searchOne8()` | 1200 | 14.0% |
| `cv::JacobiSVDImpl_<double>()` | 1144 | 13.4% |
| `LinearContourLabelChang2004::process()` | 1104 | 12.9% |
| `ThresholdBlockOtsu::thresholdBlock()` | 568 | 6.6% |
| `ThresholdBlockOtsu::computeStatistics()` | 535 | 6.3% |
| `PolylineSplitMerge::computeSideError()` | 308 | 3.6% |
| `ImageLineIntegral::compute()` | 278 | 3.3% |
| `QrCodeBinaryGridReader::readBitIntensity()` | 271 | 3.2% |
| `MaximumLineDistance::selectSplitPoint()` | 221 | 2.6% |
| `ContourEdgeIntensity::process()` | 212 | 2.5% |
| `GaliosFieldTableOps::multiply()` | 138 | 1.6% |

Interpretation: multi-QR workloads split between contour extraction and decoder-side transform / sampling work. The largest decoder hot path is still small-matrix SVD during repeated grid transforms (`QrCodeBinaryGridToPixel::computeTransform()` / `setTransformFromLinesSquare()`), followed by bit-intensity sampling and RS/Galois work.

### Bottleneck ranking and likely next levers

1. **Contour tracing (`ContourTracer::searchOne8`, `LinearContourLabelChang2004`)**
   - Dominates noisy high-resolution images and remains a large share on multi-QR images.
   - Candidate levers: remove `% 8` from the unrolled `searchOne8` path, avoid `moveToNext()` division/modulo per contour pixel by tracking `(dx,dy)` for each direction, and audit `PackedSetsPoint2D_I32` block reuse. This is algorithmic core, so any change needs Java-state parity fixtures plus full regression.

2. **`ThresholdBlockOtsu`**
   - Stable 13-16% on the profiled 4MP images.
   - Candidate levers: row-pointer and histogram reuse are already in place; further gains likely require parallel block processing, vectorised histogram updates, or integral/histogram reshaping. This is parity-sensitive and is also the root of the `monitor` / `glare` accepted residuals, so it should be treated as a parity-audit task, not a casual optimisation.

3. **Small-matrix SVD in QR grid transforms**
   - `cv::JacobiSVDImpl_<double>` is 13.4% on `lots/image005`, due repeated DLT solves across many detections/failures.
   - Candidate levers: fixed-size stack-allocated DLT/SVD path, cheaper null-space solver for known 14x9 / small Nx9 matrices, cache/reuse transforms within a decode attempt, or fail earlier before invoking transform-heavy decode. Must preserve the pure-DLT behaviour that fixed parity in ADR 03.

4. **Polyline / edge-scoring cleanup**
   - `PolylineSplitMerge::computeSideError`, `MaximumLineDistance::selectSplitPoint`, `ContourEdgeIntensity`, and `ImageLineIntegral` are individually small but add up on `lots`.
   - Candidate levers: object pooling for polyline corner structures and precomputed contour metrics. Lower priority than contour tracing and SVD.

5. **Reed-Solomon / Galois**
   - Visible but not primary (`GaliosFieldTableOps::multiply` 1.6% in `lots`).
   - Candidate levers: reuse/capacity for temporary vectors, table-access inlining, or specialised U8 QR path. Low priority unless downstream workloads are decode-only / many-QR.

### Decision

No code change in this pass. The next worthwhile perf cycle should start with contour-tracer micro-optimisations or a fixed-size SVD replacement, not broad refactoring. Because the top two hotspots are parity-sensitive algorithmic core, each candidate should be isolated behind one measurable commit and gated by `tools/cli/run_regression.sh`.

## 2026-05-11 — Fresh C++ vs BoofCV Java parity check on `qrcodes_v3`

### Why

Re-run the existing C++ port against the original BoofCV Java reference numbers using the BoofCV `qrcodes_v3` dataset, after the contour-port and `ThresholdBlockOtsu` residual-audit work. The goal was to verify the current tree still matches the original QR BoofCV results at the aggregate level and only differs in documented accepted-residual categories.

### Command

```bash
bash tools/cli/run_regression.sh
```

Dataset root: `/Users/ashmelev/Projects/30_moonlighting/pricetag-vision-datasets/data/external/boofcv-qrcodes/qrcodes`

### Result

`PASS: no new regressions; all out-of-band categories are documented residuals within their accepted-tolerance bands.`

Aggregate quality is byte-identical to the Java BoofCV 1.3.0 baseline:

| metric | Java baseline | current C++ | delta |
|---|---:|---:|---:|
| images | 562 | 562 | 0 |
| GT codes | 1258 | 1258 | 0 |
| detections | 945 | 945 | 0 |
| matches @ IoU >= 0.5 | 936 | 936 | 0 |
| decode success | 936 | 936 | 0 |
| precision | 99.05% | 99.05% | +0.00pp |
| decode rate | 74.40% | 74.40% | +0.00pp |

### Per-category residuals

All other categories are exactly in-band / byte-identical on decode rate. Non-zero deltas are the already accepted residuals:

| category | Java | current C++ | delta | status |
|---|---:|---:|---:|---|
| `bright_spots` | 27/97 (27.84%) | 29/97 (29.90%) | +2.06pp | accepted |
| `glare` | 17/53 (32.08%) | 15/53 (28.30%) | -3.77pp | accepted |
| `monitor` | 14/17 (82.35%) | 12/17 (70.59%) | -11.76pp | accepted |
| `noncompliant` | 1/26 (3.85%) | 2/26 (7.69%) | +3.85pp | accepted |
| `perspective` | 28/35 (80.00%) | 29/35 (82.86%) | +2.86pp | accepted |

### Performance note

The fresh C++ run wrote `tests/regression/baseline_cpp/summary.json` with `18698 ms total` for the 562-image scan. Scored per-image detector timing in `score.json` was mean `24.79 ms`, p50 `10.92 ms`, p95 `99.11 ms`. The Java baseline file records mean `15.41 ms`, p50 `5.36 ms`, p95 `66.38 ms`; timing is not the parity gate, but the current C++ run remains roughly in the same measured performance band documented after the contour-port close-out.

## 2026-05-09 — BoofCV 1.3.0 Java baseline on `qrcodes_v3`

### Why

Before porting any code we need a reference number per category that the C++ port has to stay within ~2% of (per CLAUDE.md goal #1). This run establishes that reference using BoofCV's own Java implementation against the same dataset the regression harness will eventually run on.

### Setup

- BoofCV 1.3.0 release artifact from Maven Central: `org.boofcv:boofcv-recognition:1.3.0` + `boofcv-io:1.3.0`.
- Detector: high-level factory `FactoryFiducial.qrcode(null, GrayU8.class)` with default `ConfigQrCode`. Returns a `QrCodePreciseDetector<GrayU8>`.
- Image loading: `UtilImageIO.loadImage(path, GrayU8.class)` — converts JPEG/PNG to greyscale internally.
- Dataset: `/Users/ashmelev/Projects/30_moonlighting/pricetag-vision-datasets/data/external/boofcv-qrcodes/qrcodes/` — 562 images, 1232 detection-set + 26 decoding-set ground-truth codes.
- Warmup: 5 images processed without recording before timed runs (so first-image JIT cost doesn't dominate the p95).
- Host: macOS arm64, OpenJDK 21.0.10, default Gradle JVM args + `-Xmx4g`.

### Aggregate results

```
total images:          562
total GT codes:        1258  (1232 detection + 26 decoding)
total detections:      945
matches @ IoU>=0.5:    936
detection rate:        74.40%
precision:             99.0%
decode success:        74.40%   (matches detection rate by construction; see Caveat 1)
payload exact match:    1.35%   (only computable for 26-code decoding subset; 17/26 = 65.38% there)
wall-clock per image:  mean 15.4 ms, p50 5.4 ms, p95 66.4 ms
```

### Per-category numbers (committed to [tests/baseline.json](tests/baseline.json))

| Category      | imgs | GT   | TP  | det rate | prec  | mean ms | p95 ms |
|---------------|-----:|-----:|----:|---------:|------:|--------:|-------:|
| blurred       |   45 |   65 |  25 |   38.46% | 100%  |   17.83 |  67.95 |
| bright_spots  |   32 |   97 |  27 |   27.84% | 100%  |   50.07 |  81.92 |
| brightness    |   28 |   85 |  67 |   78.82% | 100%  |   38.42 |  68.81 |
| close         |   40 |   40 |  40 |  100.00% | 100%  |   20.39 |  47.60 |
| curved        |   50 |   60 |  34 |   56.67% | 100%  |   16.71 |  54.79 |
| damaged       |   37 |   43 |   7 |   16.28% | 100%  |    5.92 |  22.89 |
| decoding      |   26 |   26 |  17 |   65.38% |  65%  |    2.67 |   5.26 |
| glare         |   50 |   53 |  17 |   32.08% | 100%  |    9.33 |  33.27 |
| high_version  |   33 |   37 |  15 |   40.54% | 100%  |   10.31 |  24.24 |
| lots          |    7 |  420 | 419 |   99.76% | 100%  |  110.90 | 125.17 |
| monitor       |   17 |   17 |  14 |   82.35% | 100%  |   26.05 |  42.11 |
| nominal       |   65 |   78 |  70 |   89.74% | 100%  |    8.93 |  29.23 |
| noncompliant  |   16 |   26 |   1 |    3.85% | 100%  |    4.00 |  10.04 |
| pathological  |   23 |   23 |  10 |   43.48% | 100%  |    0.51 |   0.74 |
| perspective   |   35 |   35 |  28 |   80.00% | 100%  |    1.67 |   2.30 |
| rotations     |   44 |  133 | 128 |   96.24% | 100%  |    7.41 |  11.15 |
| shadows       |   14 |   20 |  17 |   85.00% | 100%  |   12.32 |  34.37 |

### Caveats and known limitations

1. **Decode-rate is conflated with detection-rate on the detection set.** BoofCV's `QrCodeDetector` API splits results into `getDetections()` (fully decoded) and `getFailures()` (polygon located, decode failed). The current scorer only consumes `getDetections()`. So for the detection set, "decoded successfully" and "polygon matched at IoU>=0.5" are the same number. To separately measure "located but not decoded," we need to also IoU-match `failures` against GT — planned follow-up.

2. **Payload-exact match is only meaningful on the 26-image decoding subset.** Detection-set sidecars carry corner coordinates but no payload strings, so we cannot verify decoded text against ground truth there. The 1.35% aggregate `payload_exact_rate` is an artefact of decoding being 26 codes out of 1258. The relevant number for that metric is the 65.38% within the decoding subset itself.

3. **`bright_spots` discrepancy.** GT has 97 codes across 32 images (≈3 per image); we see 27 detections. Confirms this is a hard category. Worth a manual eyeball before signing the C++ port off as "matches Java" — Java itself is doing only 28% here.

4. **`noncompliant` low rate is by design.** That category is for QR-like patterns that are intentionally outside spec; BoofCV's 3.85% reflects the spec-strict decoder, not a bug. Don't treat parity below ~10% as a regression.

5. **Wall-clock perf depends on image resolution per category.** `lots/` averages 111 ms because it has many large images with multiple QRs per frame; `pathological/` averages 0.5 ms because the images are tiny synthetic 280×280 patterns. p95s within a category are more comparable than means across categories.

### Open questions / TODO

- Add failure-cause distribution to the scorer (`failure_cause` enum is already serialised in per-image JSON). Helps localise where the C++ port lags Java.
- The `bright_spots` and `damaged` categories are where any improvements to BoofCV's binarization would show up; once C++ port is at parity, these are the obvious "interesting" categories for downstream recovery experiments.
- Decoding-subset `0/26 ≠ 26/26` failures: 9 codes BoofCV detected but decoded wrong. Worth dumping payload diffs per failed decode-set image to see if there's a pattern (encoding-mode regression? specific format?).
- We're using BoofCV from Maven (release artifact). The `3rdparty/BoofCV/` checkout is at `v1.3.0-21-gbf0cc4e3ff` — 21 commits ahead of `v1.3.0`, all KLT-tracker-related (irrelevant to QR). Source-of-truth for the port is `v1.3.0`; the local checkout is for human reading and can be `git checkout v1.3.0`'d when porting starts.

### Decisions logged

- **Pin BoofCV at v1.3.0** rather than HEAD (decided 2026-05-09). Reproducibility > freshness for a port reference. The v1.3.1-SNAPSHOT in the local checkout is in-development and not a stable target.
- **Score with Shapely** in Python instead of porting BoofCV's Java scorer. The dataset has only ~1000 polygons per run — performance is irrelevant; correctness and clarity matter.
- **Greedy IoU matching** (sort all (gt,det) pairs by IoU desc, assign 1:1) instead of Hungarian. With low-IoU threshold and quad-shaped polygons, the two converge; greedy is simpler and easier to debug.
- **Use Java 21 to run Gradle** (not the system OpenJDK 25). Gradle 8.12.1 doesn't support Java 25 yet (`Unsupported class file major version 69`).
