# ResearchLog

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
