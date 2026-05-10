# `QrCodeDecoderImage` — top-level QR-decode orchestrator (step 9)

**Upstream:** `boofcv.alg.fiducial.qrcode.QrCodeDecoderImage` (BoofCV
v1.3.0, ~625 LOC).
**Step:** 9 — top-level orchestrator + step-9 public-API mandates.
**Inputs:** `std::vector<PositionPatternNode>` (graph from step 7c) +
`cv::Mat CV_8UC1` (greyscale source image).
**Outputs:** `successes_` and `failures_` populated on the orchestrator
instance. Each `QrCode` carries the full decode result: payload,
version, error-correction level, mask, raw codewords, RS error
locations, per-block decode status, geometry (`ppCorner` / `ppRight` /
`ppDown` / `bounds` / `Hinv`).

---

## One-paragraph summary

Given the position-pattern graph from the finder-pattern detector, this
stage walks every (j, k) edge pair on every node, treats each pair as
a candidate QR, and runs the full decode pipeline: format-info BCH
correction → version-info BCH correction (or size estimate for v < 7)
→ alignment-pattern localisation → iterative homography fitting with
outlier rejection (≤ 6 attempts) + bit-matrix sampling + Reed-Solomon
error correction → mode-aware payload decode. On RS success the
candidate moves to `successes_`; on any earlier failure it lands in
`failures_` with `failureCause` set. A second pass with transposed
finder positions is attempted automatically when `considerTransposed`
is true, surfaced via `qr.bitsTransposed = true` on success.

---

## Algorithm description

### Outer loop (`process` — Java line ~88)

```
for each PositionPatternNode in pps:
  for (j=3, k=0; k < 4; j=k, k++):    // 4 rotations
    if (edges[j] != null && edges[k] != null):
      qr = storageQR_.emplace_back()
      setPositionPatterns(ppn, j, k, qr)
      computeBoundingBox(qr)
      if (decode(gray, qr)):
        if (failureCause == NONE) successes_.push_back(qr)
        else                       failures_.push_back(qr)
      else if (considerTransposed):
        transposePositionPatterns(qr)
        if (decode(gray, qr)):
          qr.bitsTransposed = true
          successes_.push_back(qr)  // or failures_ if cause != NONE
        else:
          failures_.push_back(qr)
      else:
        failures_.push_back(qr)
```

`setPositionPatterns` rotates each of the three finder-pattern polygons
into canonical orientation: corner index 0 of the corner-pp at the
outside corner; corner index 1 of corner-pp connects to the right pp;
etc. After rotation the orchestrator can use index-based access without
worrying about which physical corner is which.

`computeBoundingBox` extrapolates the missing 4th corner of the QR
(diagonally opposite the corner finder) by intersecting the side lines
of the right and down finders. Three of the four bounds corners come
directly from finder polygons.

### Inner decode (`decode` — Java line ~226)

```
1. extractFormatInfo(qr)               // BCH(15,5) twice (region0 + region1)
   → on fail: failureCause = FORMAT, return false
2. extractVersionInfo(qr)              // size estimate or BCH(18,6)
   → on fail: failureCause = VERSION, return false
3. alignmentLocator.process(gray, qr)   // step-8 locator
   → on fail: failureCause = ALIGNMENT, return false
4. gridReader.setMarker(qr)             // 12 finder corners → H + Hinv
   gridReader.transformGrid().addAllFeatures(qr)  // re-add 15 (+alignment)
   for i in 0..5:
     if i > 0:
       removed = removeFeatureWithLargestError()
       if !removed: break
     computeTransform()
     if !readRawData(qr): failureCause = READING_BITS; continue
     if !applyErrorCorrection(qr): failureCause = ERROR_CORRECTION; continue
     success = true; break
5. if success:
     decoder.decodeMessage(qr)   // mode dispatch — failure encoded in qr
6. qr.Hinv = transformGrid.Hinv  // populated even on failure for diag
```

### Format-info extraction (`extractFormatInfo` — Java line ~291)

QR encodes its error-correction level + mask twice: once near the
top-left finder (region 0) and once split between the top-right + bottom
finders (region 1). The orchestrator tries region 0 first, then region
1. Each read produces a 15-bit field; we XOR it with `FORMAT_MASK`
(`0b101010000010010`) to undo the encoder-side mask, then run the
BCH(15,5) checker. If the field has zero error, shift right 10 to get
the 5-bit message; otherwise run the brute-force minimum-Hamming
corrector. The 5-bit message decodes to the (`error`, `mask`) pair.

### Version-info extraction (`extractVersionInfo` — Java line ~492)

For QR codes ≥ v7 the version is encoded explicitly in two 18-bit
regions (one near each non-corner finder) protected by BCH(18,6). For
v < 7 there is no version info — version is estimated from the *number
of modules between the corner pp and the right pp* using a rough
homography from `setTransformFromLinesSquare` and a heuristic check
that the right and down finders are roughly at right angles.

`setTransformFromLinesSquare` fits a homography from 3 of the 4 corner
finder corners (skipping the outside corner, which is "prone to
damage") plus 4 direction lines connecting the corner finder to the
other two finders. This makes the fit robust to mis-localisation at
any single corner. The line-correspondence DLT solves a 10-row 9-column
linear system via SVD, picking the right-singular vector with the
smallest singular value.

### Iterative homography (Java line ~242–269)

The Java code re-adds *all 15* features (12 finder corners + 3
alignment-pattern centres for v2; more for higher versions) after
`setMarker(qr)` (which adds 12 finder corners + alignment, then
`removeOutsideCornerFeatures` drops the 3 outside corners → 9 + alignment,
then `computeTransform`). The orchestrator's second `addAllFeatures`
call resets `pairs2D` to all 15. Then on each iteration `i > 0`,
`removeFeatureWithLargestError` drops the feature with the largest
re-projection residual (provided the residual exceeds 4.0 px²). When
no further feature can be safely removed (returns false), the loop
exits.

This iterative outlier rejection is the main mechanism by which the
decoder copes with one-or-two damaged finder corners or a mis-located
alignment pattern.

### Bit sampling (`readRawData` — Java line ~365)

`QrCodeCodeWordLocations::qrcode(version)` produces the spec-defined
zigzag bit-extraction order through the QR's data modules. For each
data module, the orchestrator:
1. samples the source image at 5 sub-pixel positions around the module
   centre via `gridReader.readBitIntensity(row, col, intensities)`,
2. computes a per-module threshold by bilinearly interpolating the four
   corner thresholds (`threshCorner` / `threshRight` / `threshDown` /
   `threshDownRight`),
3. votes 5 ways: > threshold → 0, < threshold → 1; majority wins,
4. XORs the result with `qr.mask->apply(row, col, 0)` to undo the
   encoder's mask pattern.

The lower-right corner of the QR has no nearby finder, so its
threshold is computed at sample time as the mean intensity of the data
modules in that corner (everything below module column `max(8, N-10)`
and below row `max(8, N-10)`, where `N = numModules`).

### Reed-Solomon (delegated to `QrCodeDecoderBits::applyErrorCorrection`)

Step 9 wires this into the public-mandate fields:
- `qr.blockStatus[i]` — one entry per RS block, `SUCCESS_NO_ERRORS` /
  `SUCCESS` / `ERROR_CORRECTION_FAILED`.
- `qr.rsErrorLocations` — flat list of byte offsets into `qr.rawbits`
  / `qr.rawCodewords` that RS corrected. Translated from per-block
  positions back to raw-bits positions using the de-interleave stride.

### Mode dispatch (delegated to `QrCodeDecoderBits::decodeMessage`)

Always called *after* RS succeeds. Mode dispatch failure (e.g. invalid
ECI, unknown mode, padding violation) sets `qr.failureCause` but
`decode()` still returns true — the QR was at least RS-correctable, so
it counts as a "found marker, but message parse error" rather than a
non-detection. Java has the same convention.

---

## Why this approach over alternatives

- **Format/version BEFORE alignment, before sampling.** Format info is
  in fixed positions next to the corner finder so we can read it with
  only the rough homography from `setSquare(ppCorner)`. Version info
  for v7+ uses a similar fixed-position read. Knowing version + ECC
  level + mask is a precondition for everything downstream:
  - alignment-pattern *expected positions* depend on version;
  - the de-interleave stride depends on version + ECC level;
  - the bit-extraction zigzag depends on version (alignment patterns
    block data modules);
  - the per-module bit value depends on the mask.
  Doing format/mask AFTER sampling — a tempting refactor — would
  require sampling with the *wrong* mask and re-sampling, and would
  re-introduce the dependency cycle.
- **Iterative outlier rejection (≤ 6 attempts) over a one-shot
  least-squares fit.** Damaged outside corners are the dominant noise
  source on real QR images; greedily dropping the worst one and
  refitting gives much better numerical conditioning than a single
  pass. The 6-iteration bound comes from a worst-case scenario where
  the 3 outside corners + 3 alignment patterns all need rejection
  (this never happens in practice; usually 1–2 iterations suffice).
- **`setTransformFromLinesSquare` over a 3-point homography for v<7
  size estimation.** A 3-point homography is rank-deficient — 4 points
  are needed for the standard DLT. BoofCV adds 4 *direction lines*
  between the corner finder and the other two finders, which fully
  constrains the homography even when only 3 corner finder corners
  are reliable (the 4th, outside corner is intentionally skipped).
- **Voting threshold (≥ 3 of 5) over a single-sample read.** A single
  sample is sensitive to printing artefacts (e.g. a stray dot inside
  a "white" module) and sub-pixel coordinate rounding. Five samples
  around the module centre + majority vote are a cheap robustness
  win.
- **`considerTransposed` retry.** Some encoders (notably old industrial
  QR generators) emit codes with the bit order transposed, which would
  otherwise fail format extraction. Java's pragmatic fix: try the
  normal orientation first; on any failure, swap right ↔ down finders
  and corner-1 ↔ corner-3 on each finder, retry. Cheap correctness
  improvement.

## Failure modes and known limits

| Stage | Failure → `failureCause` | Trigger |
|-------|--------------------------|---------|
| `extractFormatInfo` | `FORMAT` | Both 15-bit regions exceed BCH(15,5) correction capacity (3 errors). |
| `extractVersionInfo` | `VERSION` | v < 7 size estimate falls outside [1, 40]; or v ≥ 7 BCH(18,6) decode failure on both regions. |
| `alignmentLocator.process` | `ALIGNMENT` | One or more expected alignment patterns couldn't be localised. v1 has no alignment patterns so this never fires for v1. |
| `readRawData` (sampling) | `READING_BITS` | Indexing out-of-bounds or sample failure. Rare in practice; the `gridReader.readBit` returning `-1` is masked to `0` so partial QRs go through. |
| `applyErrorCorrection` | `ERROR_CORRECTION` | Any block exceeded its RS capacity. Iterative loop retries with one more correspondence dropped — break early if no further drop is helpful. |
| `decodeMessage` | various | Mode dispatch failure; message parse error. `decode()` still returns true; caller checks `qr.failureCause` to disambiguate "decoded but invalid payload" from "RS-correctable, no payload". |

**Out of scope for v1 of this port:**
- Lens distortion (`setLensDistortion(width, height, model)`). The
  Java orchestrator threads the model through `alignmentLocator` and
  `gridReader`; we don't have the narrow-FOV distortion model ported,
  so this codepath is omitted. Downstream consumers that need lens
  correction should pre-undistort the input image before calling
  `process()`.
- Micro QR. The decoder is ISO 18004 QR only.
- Multi-frame fusion / temporal voting. Per CLAUDE.md "Public API
  design": this library is single-frame; consumers compose multi-frame
  recovery on top of the exposed mandate fields (`rawCodewords` /
  `rsErrorLocations` / `blockStatus`).

## Tunable parameters

- `considerTransposed` (bool, default `true`). Toggle the
  transposed-bits retry. Disable if you know upstream encoders are
  always orientation-correct and you want to halve worst-case
  per-candidate decode time.
- The `≤ 6` iteration cap on the outlier-rejection loop is hard-coded.
  In practice 1–2 iterations suffice; raising it doesn't help and
  lowering it risks rejecting a recoverable but noisy QR.
- `removeFeatureWithLargestError`'s residual threshold (`> 4.0`, in
  pixel² space) is set in `qr_code_binary_grid_to_pixel.cpp`. Lower
  → more aggressive outlier rejection (may discard legitimate
  correspondences); higher → more conservative (may keep bad ones).
- Strategy-injection hooks on `QrCodeDecoderImage`:
  - `setRsCorrectStrategy(fn)` — replace built-in
    `QrCodeDecoderBits::applyErrorCorrection`. Use case: known-prefix
    RS recovery, where `n` data bytes are known a priori and treated
    as erasures.
  - `setAlignmentStrategy(fn)` — replace built-in
    `QrCodeAlignmentPatternLocator::process`. Use case: clipped-QR
    fallback that uses a different localiser when patterns are
    out-of-frame.

## Integration points for downstream recovery (CLAUDE.md mandates)

1. **Stage isolation.** `find_finders`, `sample_bit_matrix`,
   `extract_raw_codewords`, `rs_correct`, `decode_message` are all
   public entry points and can be composed without the top-level
   `process()`. Multi-frame fusion typically calls
   `extract_raw_codewords` per frame, fuses the rawCodewords across
   frames, then calls `rs_correct` once on the fused buffer.
2. **Strategy injection.** `RsCorrectFn` and `AlignmentLocatorFn`
   `std::function` hooks. Defaults preserve verbatim Java behaviour.
3. **Polygon-only mode.** `detect_polygons_only(pps, gray)` runs
   `find_finders` + alignment localisation, then stops. Useful for
   best-frame selection over video where you call detection many times
   per full decode and want to pay the RS / mode-decode cost only for
   the chosen frame.
4. **Public mandate fields on `QrCode`.**
   `rawCodewords` / `rsErrorLocations` / `blockStatus` are populated by
   the RS step and surfaced for downstream multi-frame fusion or
   known-prefix recovery.
5. **`setTransformFromLinesSquare` and `setMarkerUnknownVersion`.**
   Used internally by `estimateVersionBySize`; also useful as a
   standalone for applications that have a partial finder-pattern
   triplet and want a rough homography.
6. **`bitsTransposed` retry path.** Surface the `qr.bitsTransposed`
   flag so consumers know which orientation succeeded. Toggle via
   `considerTransposed = false` to disable.
7. **No raw pointers in public signatures.** All public APIs return
   value-typed `std::vector` / `std::optional`. The internal sub-modules
   (`QrCodeAlignmentPatternLocator`, `QrCodeBinaryGridReader`,
   `QrCodeDecoderBits`) are returned by reference for tests +
   diagnostics; ownership stays with the orchestrator.

---

## Known parity residuals

The C++ port reaches **-0.08pp aggregate from Java's 74.40% baseline** on
the locked `qrcodes_v3` regression set (562 images, 1258 GT). 11 of 17
categories are within ±2pp of Java; 6 categories are outside the band
for two distinct, well-understood reasons. Future engineers staring at
the per-category table should read this section before assuming any of
the residuals is a fixable bug.

### 1. `monitor` -11.76pp + `glare` -3.77pp — `cv::findContours` substitution cost

Both residuals trace to the same root cause: the project uses OpenCV's
`cv::findContours(RETR_CCOMP, CHAIN_APPROX_NONE)` instead of porting
BoofCV's `LinearContourLabelChang2004` (per CLAUDE.md "Replace with
OpenCV"). Both contour extractors emit ~the same boundary, but the
**per-pixel sequence differs at the boundary**, especially around
diagonal moves. Binarised images agree to within ~1% per-pixel diff;
the divergence is downstream of binarisation.

Empirical breakdown:

| category | C++-unique misses (Java decodes, C++ doesn't) | mechanism |
|---|---|---|
| `monitor` | 2 of 17 images: `image011`, `image012` | finder-check stage flips |
| `glare`   | 3 of 53 GT (across `image005`, `image007`, `image022`) | polygon-fit stage rejects |

**Mechanism on `monitor`** (finder-check stage). The polygon for the
QR's bottom-left finder IS detected (corner positions agree with Java
within 1 px). But our `ContourEdgeIntensity` averages 30 tangent-
direction probes along the boundary. Because OpenCV's contour visits a
slightly different set of pixels than BoofCV's, those 30 probes land
on slightly different positions. The resulting `(edgeInside +
edgeOutside) / 2` `grayThreshold` differs from Java's by ~7 grayvalue
units. On borderline images that's enough to flip the `1:1:3:1:1`
raster check that decides whether the polygon is a finder pattern.

**Mechanism on `glare`** (polygon-fit stage). The QR's finders are
tiny (~5–10 px wide; perimeter ~43 px on the borderline cases —
exactly at the `minimumContour = ConfigLength::fixed(40)` floor). The
polyline corner finder (`PolylineSplitMerge`) needs to fit a 4-corner
convex polygon to a contour with that few pixels. With OpenCV's
contour pixel encoding (CCW-in-image, Moore-neighbor 8-connectivity,
slightly different inner-loop ordering than `LinearContourLabelChang2004`),
the corner finder rejects the contour where Java's equivalent passes.
Net: the finder polygon never makes it into the candidate list at all.

Both mechanisms are sensitivity amplifications of the same underlying
contour-encoding divergence — a few-pixel boundary shift propagates
into a `~7-unit` greyvalue threshold shift on `monitor` (tipping the
1:1:3:1:1 check) or a `~1-pixel` corner-position shift on `glare`
(tipping the polyline corner finder). Tiny-feature inputs and
tipping-point thresholds amplify the divergence; non-borderline images
(99% of the dataset) are unaffected.

**Why we accept this residual:**

- CLAUDE.md "Replace with OpenCV" explicitly approves
  `cv::findContours` for contour extraction. This is a project-level
  architectural decision made at start, not a porting oversight.
- Cost is 5 borderline images out of 562 → ~0.4pp aggregate impact.
  Aggregate is already at -0.08pp from Java; closing these residuals
  buys 0.4pp at high cost.
- Alternatives rejected: see [`docs/decisions/01_cv_findcontours_substitution.md`](../../docs/decisions/01_cv_findcontours_substitution.md).

When a downstream consumer's input distribution makes monitor/glare-
style images dominant (e.g. shelf-photo scenarios with reflective
glare), the ADR spells out the conditions under which we'd revisit
the substitution.

### 2. Small-N "+" outliers — `high_version` +2.70pp, `perspective` +2.86pp, `noncompliant` +3.85pp, `bright_spots` +2.06pp

These four categories' deltas come in *positive* (we beat Java
slightly) but outside the strict ±2pp band. They are denominator-
noise artifacts on small-N categories.

| category | GT count | 1-image swing | observed delta |
|---|---:|---:|---:|
| `noncompliant`  | 26 | 3.85pp | +3.85pp (1 image) |
| `high_version`  | 37 | 2.70pp | +2.70pp (1 image) |
| `perspective`   | 35 | 2.86pp | +2.86pp (1 image) |
| `bright_spots`  | 97 | 1.03pp | +2.06pp (2 images) |

Each delta corresponds to ≤2 images that decode in C++ but not in
Java. The cycle-(b) polygon-tuning settings (looser `maxSideError`,
tighter `cornerScorePenalty`) cleared `brightness` and `pathological`
into the band but pushed these four slightly past Java parity. Both
"slightly worse" and "slightly better" are within the noise band of a
small-N category — the absolute count of images is what matters, and
we're talking about 1-2 images out of 26-97 GT.

CLAUDE.md goal text uses "**~2%**" with a tilde; on small-N categories
where ±1 image is already 2.7-3.85pp, the strict ±2pp gate is below
the noise floor of the measurement. We don't tune these — chasing
them would risk regressing a high-N category for cosmetic small-N
gain.

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
| glare         |   32.08% | 28.30% |   -3.77pp  |   (cv::findContours residual) |
| high_version  |   40.54% | 43.24% |   +2.70pp  |   (small-N) |
| lots          |   99.76% | 99.76% |   +0.00pp  | ✓ |
| monitor       |   82.35% | 70.59% |  -11.76pp  |   (cv::findContours residual) |
| nominal       |   89.74% | 89.74% |   +0.00pp  | ✓ |
| noncompliant  |    3.85% |  7.69% |   +3.85pp  |   (small-N) |
| pathological  |   43.48% | 43.48% |   +0.00pp  | ✓ |
| perspective   |   80.00% | 82.86% |   +2.86pp  |   (small-N) |
| rotations     |   96.24% | 96.24% |   +0.00pp  | ✓ |
| shadows       |   85.00% | 85.00% |   +0.00pp  | ✓ |
| **AGGREGATE** | **74.40%** | **74.32%** | **-0.08pp** | |

11 of 17 categories within ±2pp band. 6 residuals: `monitor` -11.76 +
`glare` -3.77 (the cv::findContours-substitution residuals — see ADR
01) + 4 small-N "+" outliers (`bright_spots` +2.06, `high_version`
+2.70, `noncompliant` +3.85, `perspective` +2.86 — denominator-noise
on 26-97-GT categories where ±1 image swings 2-4pp; `bright_spots`
also has a small cv::findContours-interaction component).

---

## Performance

After parity close-out the C++ port ran a profile-driven perf push.
The push ran in three episodes — ADR 02 stopped at the cycle-1 win;
ADR 03 resumed for two more profile-confirmed cycles; ADR 04 added
one more under a stricter cluster-coverage gate. This section captures
the **as-shipped state** at the end of ADR 04, plus the five-state
perf progression and the rationale for why each cycle landed where it
did. The full per-cycle reasoning lives in the ADRs:
[ADR 02](../../docs/decisions/02_perf_stop_after_cycle1.md) (cycle 1
banked, cycle 2 stopped),
[ADR 03](../../docs/decisions/03_perf_findhomography_and_sampler_cycles.md)
(cycles 3 + 4 banked, cluster-coverage gate introduced), and
[ADR 04](../../docs/decisions/04_perf_cycle5_gridToImage_inline.md)
(cycle 5 banked under the gate; surgical-fix floor for v1).

### The four banked optimisations

**Cycle 1 — inlined single-point homography (commits `340d038` +
`bfbc2e2`).** `QrCodeBinaryGridToPixel::imageToGrid` / `gridToImage`
(and the two internal callers in `removeFeatureWithLargestError` /
`computeTransform`) were originally implemented as
`cv::perspectiveTransform` over a freshly-heap-allocated 1-element
`cv::Mat<Point2d>`. Profile on the worst-case `high_version` image
showed >70% of CPU time in `cv::Mat::create` / `cv::Mat::release` /
allocator chains, all reached from `gridToImage`. The bit sampler
(`QrCodeBinaryGridReader::readBit`) calls `gridToImage` 5× per module
bit, so a Version-40 QR speculative read burns ~156k mat allocations.
Replaced with a hand-rolled 3×3 mat-vec + perspective divide that is
bit-identical to OpenCV's `perspectiveTransform_64f` on the fast path
(`|w| > FLT_EPSILON`) and matches OpenCV's `(0, 0)` zero-fill on the
degenerate branch. See `src/sampler/qr_code_binary_grid_to_pixel.md`.

**Cycle 3 — explicit DLT via `cv::SVD::solveZ` (`23c1327`).** Profile
on `lots/image00X` showed `cv::findHomography(method=0)` doing ~14%
of `lots` decode time inside an LM-refinement chain
(`cv::LMSolverImpl::run` → `cv::solve` → `cv::JacobiImpl_<double>`).
CLAUDE.md and the sampler doc claimed `method=0` was "pure DLT, no
iterative refinement" — incorrect for OpenCV when `npoints > 4`.
BoofCV's `GenerateHomographyLinear` → `HomographyDirectLinearTransform`
runs **only** the DLT (and its `normalize` flag is dead-coded in
`process()`). Replaced the `cv::findHomography` call with an explicit
2N×9 design-matrix build + `cv::SVD::solveZ` — same algorithm
BoofCV's `SolveNullSpaceSvd_DDRM` runs. **Closed the -0.08pp
aggregate parity residual** ADR 02 had logged as the ship state — the
LM refinement was actively diverging from BoofCV's pure DLT.

**Cycle 4 — sampler hot path (`ea93854`).** Profile on `high_version`
pinned `QrCodeBinaryGridReader::sampleNearest` and
`readBitIntensity` as the dominant remaining cost (V40 = ~156k
`sampleNearest` calls per scan). Five surgical changes: (1)
`sampleNearest` moved to header `inline` so the 5 calls per bit
collapse to inline arithmetic + a single pixel read each; (2)
`std::floor(x)` → `static_cast<int32_t>(x)` (matches Java's `(int)x`
exactly; the subsequent clamp folds the floor/cast difference to the
same byte); (3) `image_.at<uint8_t>(iy, ix)` →
`image_.ptr<uint8_t>(iy)[ix]` (after clamping, indices are in-bounds;
`at()` adds debug bounds + step multiplication `ptr<>` doesn't); (4)
`readBitIntensity` collapsed 5× `push_back` into one `resize(base+5)`
+ 5 indexed writes (caller pre-`reserve()`s, so the resize is
non-allocating); (5) removed unused `<cmath>`. **Universal speedup**:
every category 1.32–1.36× faster — the hot path runs in every decode.

**Cycle 5 — `gridToImage` / `imageToGrid` inlined into header
(`7063ec2`).** ADR 03 introduced a cluster-coverage gate: profile
across `bright_spots` / `lots` / `high_version` / `nominal` before
declaring victory. The four-cluster pass found `gridToImage` at
33–35% self time on V40 (`high_version/image029` + `image012`,
profile-stable across the cluster) — symmetric to cycle-4's
`sampleNearest` move but in a different file. Pre-cycle-5 the methods
lived in the .cpp and forwarded to a file-local `applyHomography`
helper; both layers were `inline`-tagged but invisible across the
.cpp/.hpp boundary, so every call from
`QrCodeBinaryGridReader::readBitIntensity` / `readBit` and
`QrCodeAlignmentPatternLocator` was a real function call. Cycle 5
moves both bodies into the header with the 3×3 mat-vec + perspective
divide spelled out at the call site (no nested `applyHomography`
indirection — the compiler doesn't always inline through both layers).
The slow `adjustWithFeatures` branch is kept out-of-line as
`applyAdjustment(row, col, pixel)` (dead on every decode call after
cycle 3, would bloat call sites if inlined). Same arithmetic, same
`FLT_EPSILON` gate, same `(0, 0)` zero-fill — bit-identical. Per-image
deltas: `lots/image001` 174.1 → 166.3 ms/iter (-4.5%);
`high_version/image029` 61.1 → 56.7 ms/iter (-7.2%). Aggregate
regression-set delta -0.43% — **cycle 5 is the surgical-fix floor for
v1**.

### Five-state perf progression

| commit    | label                                            | decoder-only sum | C++/Java | Δ vs prior |
|-----------|--------------------------------------------------|-----------------:|---------:|-----------:|
| `bda1650` | pre-perf (parity ship)                           |          ~74.2 s |    9.27× |          — |
| `bfbc2e2` | cycle 1 — `perspectiveTransform` inline          |           45.9 s |    5.73× |      1.62× |
| `23c1327` | cycle 3 — explicit DLT via `cv::SVD::solveZ`     |           43.5 s |    5.44× |  1.05× + parity 0.00pp |
| `ea93854` | cycle 4 — sampler hot path                       |           32.5 s |    4.05× |      1.34× |
| `7063ec2` | cycle 5 — `gridToImage` / `imageToGrid` inline   |        **32.1 s**|**4.01×** |      1.01× |

Two parity states banked alongside:

- pre-cycle-3: 74.32% (-0.08pp from Java).
- post-cycle-3: **74.40%** byte-identical to Java baseline. Held
  through cycle 4.

### Final per-category timing (post-`7063ec2`)

Same 562-image regression set, decoder-only sums per category
(release `-O3` run); `ratio` is C++/Java.

| category      | java_ms | cpp_ms | ratio (C++/Java) |
|---------------|--------:|-------:|-----------------:|
| blurred       |   760.1 | 1859.8 |            2.45× |
| bright_spots  |  1458.5 |12611.2 |            8.65× |
| brightness    |  1011.4 | 5235.6 |            5.18× |
| close         |   762.1 | 1858.7 |            2.44× |
| curved        |   803.0 | 4094.8 |            5.10× |
| damaged       |   207.4 |  601.8 |            2.90× |
| **decoding**  |    69.0 |   32.5 |       **0.47×** (2.1× faster than Java) |
| glare         |   443.1 | 1205.5 |            2.72× |
| high_version  |   331.4 |  526.9 |            1.59× (was 44.4× pre-cycle-1) |
| lots          |   744.6 | 1053.0 |            1.41× |
| monitor       |   436.4 |  886.7 |            2.03× |
| nominal       |   382.2 | 1032.5 |            2.70× |
| noncompliant  |    62.6 |  103.7 |            1.66× |
| pathological  |    10.9 |   13.3 |            1.21× |
| perspective   |    53.7 |   95.8 |            1.78× |
| rotations     |   299.6 |  528.0 |            1.76× |
| shadows       |   168.8 |  327.1 |            1.94× |
| **SUM**       |  8004.8 |32067.0 |        **4.01×** |

End-to-end wall clock (load + decode + JSON serialise, 562 images):
**~37.7 s** C++ vs **22.7 s** Java. Decoder-only sum: **32.1 s** vs
**8.0 s**.

### Why the remaining gap is what it is — and why we stop here

The two slowest categories (`bright_spots` 8.75×, `brightness` 5.20×)
are exactly the noisy-binarisation categories ADR 02 pinned to
`cv::findContours` time — **~95% of decode CPU time inside
`cv::findContours`** on those images, specifically
`cv::ContourScanner_::findFirstBoundingContour` plus the surrounding
`icvFetchContourEx` / `findNextX` / `contourScan` / per-contour
storage push_back / tree traversal. Not in our wrapper code, not in
any pixel-access pattern we control. Cycles 3, 4, and 5 didn't change
those numbers materially (each brought a per-bit-loop speedup that all
categories benefit from, including these — `bright_spots` dropped
from 11.65× → 8.75× → 8.65× across the cycles for the same reason all
others did).

Cycle 5's four-cluster profile (per ADR 03's gate, executed in ADR 04)
confirmed the surgical-fix floor: the new top hot spots are now either
ADR-01-locked (`cv::findContours`), parity-load-bearing
(`cv::SVD::solveZ` from cycle 3's DLT — reverting it would re-open
the closed -0.08pp parity residual), or verbatim BoofCV ports
(`ThresholdBlockOtsu` is forbidden to reshape under the
"Verbatim vs idiomize" rule). None are surgical-fixable for v1.

Cutting `cv::findContours` cost without breaking parity would require
porting BoofCV's `LinearContourLabelChang2004` — the same
architectural fork already discussed in ADR 01 (parity) and ADRs
02–04 (perf). That is a multi-cycle commitment (~600 LOC + tests +
algo doc + full parity re-validation), not a "one fix,
profile-confirmed" cycle. **Per ADR 04 the v1 decision is to bank the
four perf cycles and stop at the surgical-fix floor.**

Note that porting `LinearContourLabelChang2004` would likely *also*
clear the `monitor -11.76` and `glare -3.77` parity residuals from
ADR 01. If a downstream consumer needs sub-Java perf on the noisy
categories OR sub-Java parity on `monitor`/`glare`, ADRs 01 + 02 + 03
\+ 04 together justify revisiting the fork.

---

## Cross-references

- Upstream BoofCV file:
  `main/boofcv-recognition/src/main/java/boofcv/alg/fiducial/qrcode/QrCodeDecoderImage.java`
- Upstream JUnit:
  `main/boofcv-recognition/src/test/java/boofcv/alg/fiducial/qrcode/TestQrCodeDecoderImage.java`
- ISO/IEC 18004:2015 — QR Code bar code symbology specification.
  Sections referenced:
  - §7.9 (format information mask `FORMAT_MASK = 0b101010000010010`).
  - §7.10 (version information).
  - Annex C (error correction codeword arrangement and de-interleave).
  - Annex E (alignment pattern coordinates).
- Pinned upstream tag: see `UPSTREAM_VERSION` at repo root.
- Related stages this orchestrator depends on (in runtime order, the
  inverse of porting order):
  - step 7c: `QrCodePositionPatternDetector` →
    `QrCodePositionPatternGraphGenerator` produces the
    `PositionPatternNode` graph this orchestrator consumes.
  - step 8: `QrCodeAlignmentPatternLocator` localises alignment
    patterns once the version is known.
  - step 5: `QrCodeBinaryGridReader` + `QrCodeBinaryGridToPixel`
    homography sampling.
  - step 4: `QrCodePolynomialMath` BCH decoders +
    `QrCodeMaskPattern` mask XOR.
  - step 3: `QrCodeDecoderBits` mode dispatch + payload extraction
    on RS-corrected codewords.
  - step 2: `ReedSolomonCodes_U8` per-block RS correction (consumed by
    `QrCodeDecoderBits::applyErrorCorrection`).
  - step 1: `GaliosFieldOps` GF(2^8) primitives (consumed by RS).
