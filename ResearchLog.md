# ResearchLog

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
