# DetectPolygonFromContour — black-blob to polygon detector

Companion to `boofcv_qr/polygon/detect_polygon_from_contour.hpp`. Step 7b (part 2) of the BoofCV port — wraps the contour finder, the polyline corner-finder (`PolylineSplitMerge` from part 1), an edge-intensity false-positive filter, and the bookkeeping that turns a binary image into a list of candidate polygons.

## Summary

Given a binary image (`CV_8UC1`, 0/1, foreground = dark module per CLAUDE.md) and the original gray-scale image, find every dark blob whose external contour fits a polygon of `[minSides, maxSides]` corners with low side-fit error and high contour-edge contrast. Each result carries the source contour, the polygon corners (integer pixel), the corner indices into the contour, edge-intensity diagnostics, and per-corner border-touch flags.

Downstream stages — `RefinePolygonToGray` (subpixel corner refinement, next file) and the QR finder-pattern detector (`QrCodePositionPatternDetector`) — consume this output. The QR finder pattern is itself three nested square contours; the inner two land here as "internal" contours within the parent blob. Step 7c re-uses those inside the same blob, which is why the contour topology must preserve external + internal grouping.

## Algorithm

`process(gray, binary)` runs five stages per blob:

1. **Contour extraction.** Use `LinearContourLabelChang2004::process(binary, labeled)` (verbatim port of `boofcv.alg.filter.binary.LinearContourLabelChang2004` — see `src/binary/linear_contour_label_chang2004.md`). The port emits per-blob `ContourPacked` headers indexing into a shared `PackedSetsPoint2D_I32`. We re-bundle each blob into a `Contour` struct (`std::vector<cv::Point2i> external` + `std::vector<std::vector<cv::Point2i>> internal`) — same shape as BoofCV's `boofcv.alg.filter.binary.Contour`. `buildContoursFromPort()` uses `PackedSetsPoint2D_I32::appendSetTo()` to materialise each stored contour block-by-block while preserving the exact point sequence. Connectivity rule is 8-connected (`ConfigQrCode.polygon.detector.contourRule = ConnectRule.EIGHT`). The upper cap (`maximumContour_`) is pushed into the labeller; the lower cap stays as a downstream filter in `findCandidateShapes`, matching Java's `BinaryContourFinder` configuration in `SquareLocatorPatternDetectorBase`. Wiped (over-cap) externals come back as size-0 sets and are skipped in `buildContoursFromPort`.

2. **Per-blob pre-filtering** (drops cheaply-detected non-polygons before the expensive corner-finder runs):
   - **Length cut:** contour pixel-count below `minimumContourPixels` is rejected. The threshold comes from `ConfigLength.computeNegMaxI(sqrt(W*H))` on the configured `minimumContour`, clamped to ≥ 4. (For QR `ConfigQrCode` overrides this to `fixed(40)`.)
   - **Border touch rule:** if `canTouchBorder=false` and any contour pixel is on `x∈{0,W-1}` or `y∈{0,H-1}`, reject.
   - **`PolygonHelper.filterContour` hook** (optional): caller can short-circuit a contour for any reason. Used by the QR finder-pattern subclass which doesn't yet exist in the port.
   - **Edge-intensity sanity check** (optional, gated by `contourEdgeThreshold > 0`): `ContourEdgeIntensity` samples the gray image inside and outside the contour at `30` evenly-spaced points, walking 1 pixel along the local tangent normal in each direction. If `|edgeOutside - edgeInside| < contourEdgeThreshold`, reject as low-contrast noise.

3. **Polygon fitting.** Call `PolylineSplitMerge::process(contour)` (the corner-finder from part 1) and read its `getBestPolyline()`. The returned `splits[]` are indices into the contour where the corners are. `PolylineSplitMerge`'s tunables (min/max sides, convex flag, side-error gate, corner-score penalty, etc.) are forwarded by the public `setNumberOfSides()`, `setConvex()` API.

4. **Orientation classification.** With pre-fit corners, compute `UtilPolygons2D_I32.isCCW(polygonPixel)` (sign-of-cross-product vote across all corner triplets). If `outputClockwiseUpY == isCCW` we flip the order in place — note "+y up" matches geometric convention; image-coordinate "+y down" inverts the visual sense (mirroring upstream comment). For QR we set `outputClockwiseUpY=false` (`ConfigQrCode.polygon.detector.clockwise = false`) → output is CCW in +y-up = CW in image coords, which is how the finder-pattern stage downstream expects things. Edge-inside / edge-outside diagnostics get swapped if `isCCW` was wrong, then the white-blob filter runs: `edgeInside > edgeOutside` rejects (we want black squares).

5. **Polygon record assembly.** Build a `DetectedInfo` with the corner array (`polygon`, undistorted; `polygonDistorted` is identical when no lens distortion is set), the source contour, the per-corner border-touch booleans (`determineCornersOnBorder`), the edge intensities, and the `splits[]` array. Reject below `minimumArea` (computed as `(minimumContourPixels/4)²` — chord of a degenerate skinny shape).

## OpenCV substitution policy

Cycle B of the LinearContour port retired the prior `cv::findContours(RETR_CCOMP, CHAIN_APPROX_NONE)` substitution. The polygon path now consumes the verbatim BoofCV port `LinearContourLabelChang2004` (see `src/binary/linear_contour_label_chang2004.md`). The port emits BoofCV-native winding (external CW in image, internal CCW) and start pixel (topmost-leftmost foreground) directly, so the `770f210` per-contour reversal + topmost-leftmost rotation workaround has been removed.

Historical note: the `cv::findContours` substitution was the project's original choice at step 7b (commit `770f210`). It was retired because:

- ADR 01 documented a ~0.4pp aggregate parity cost (`monitor` -11.76pp, `glare` -3.77pp) that the per-pixel encoding cascade was contributing to.
- ADR 04 (perf cycle 5 close-out) recorded ~95% of CPU on `bright_spots` / `brightness` / `curved` was inside `cv::findContours` — the dominant non-cycle-3-banked hot spot.

Cycle B's measured impact:

- **Parity:** byte-identical to the pre-cycle-B state across all 17 categories. Monitor / glare residuals did NOT close (re-attributed to upstream binarizer divergence — see `src/binary/linear_contour_label_chang2004.md` "Integration points for downstream recovery"). Aggregate held at +0.00pp byte-identical to Java baseline.
- **Perf:** ~2× total-wallclock speed-up across `qrcodes_v3` (36.9s → 18.6s). Per-category mean-ms drops on the cv::findContours-dominated categories: `bright_spots` 394.10 → 73.39 ms (-81%); `brightness` 186.99 → 64.06 ms (-66%); `curved` 81.90 → 29.25 ms (-64%).

## Failure modes

- **Empty blob list** — clean image, no foreground. Returns empty result. Not an error.
- **Convex flag mismatch** — `ConfigQrCode` sets `convex=true`. A non-convex shape (e.g. the connected outer ring of a finder pattern when binarisation noise bridges the outer black to the white interior) is silently dropped; expected behaviour.
- **Input is not mutated.** `LinearContourLabelChang2004` copies the input binary into an internal 1-pixel-bordered scratch buffer (then writes its `0xFF` sentinel only into that scratch). The caller's `binary` `cv::Mat` is read-only from this stage's perspective.
- **Lens distortion** — deferred; `setLensDistortion` is a no-op stub. The corner output is in raw image coordinates. Document this in the public header.

## Tunable parameters (with QR defaults from `ConfigQrCode`)

| Parameter | QR default | Effect |
|---|---|---|
| `minimumSides`, `maximumSides` | 4, 4 (QR finder pattern) | Forwarded to `PolylineSplitMerge`. |
| `outputClockwiseUpY` | `false` (clockwise=false) | Polygon corner order. |
| `canTouchBorder` | `false` | Drop contours that touch the image border. |
| `contourEdgeThreshold` | `3.0` (QR) | Min `|edgeInside - edgeOutside|` to keep. |
| `tangentEdgeIntensity` | `1.5` (QR) | Pixels off the contour to sample. |
| `convex` | `true` (forwarded to PolylineSplitMerge) | Reject concave shapes early. |
| `minimumContour` | `fixed(40)` (QR) | Pixel-count cut on contour length. |
| `maximumContour` | `fixed(-1)` = unlimited; QR's `SquareLocatorPatternDetectorBase` sets a per-image cap | Pushed into the labeller — contours exceeding the cap come back as empty sets and are skipped in `buildContoursFromPort`. The lower cap (`minimumContour`) is enforced downstream in `findCandidateShapes` to match Java's `BinaryContourFinder` configuration. |

## Public API design

Per CLAUDE.md "Public API design":

- `getDetected()` returns `const std::vector<DetectedInfo>&` (value-typed, no `unique_ptr`).
- `process(gray, binary)` callable in isolation — no global / factory-bound state required.
- The `PolygonHelper` and `splitter` (override `PolylineSplitMerge` config) injection points exist via setters.

## Cross-references

- Upstream BoofCV (pinned `v1.3.0`):
  - `boofcv-feature/src/main/java/boofcv/alg/shapes/polygon/DetectPolygonFromContour.java` (638 LOC; this file)
  - `boofcv-feature/src/main/java/boofcv/alg/shapes/polygon/ContourEdgeIntensity.java` (140 LOC; ported in `src/polygon/contour_edge_intensity.cpp`)
  - `boofcv-feature/src/main/java/boofcv/alg/shapes/polygon/PolygonHelper.java` (interface; ported as a header-only abstract base)
  - `boofcv-feature/src/main/java/boofcv/abst/shapes/polyline/PointsToPolyline.java` (interface; ported as a header-only abstract base + a `PolylineSplitMergeAdapter` concrete impl in this file)
  - `boofcv-ip/src/main/java/boofcv/alg/filter/binary/Contour.java` (data struct; ported inline)
  - `boofcv-ip/src/main/java/boofcv/alg/filter/binary/LinearContourLabelChang2004.java` (ported in `src/binary/linear_contour_label_chang2004.cpp`; consumed here as of cycle B of the LinearContour port)
  - `georegression-0.28.2.jar` `Area2D_F64.polygonSimple` and `UtilPolygons2D_I32.isCCW` (both inlined)
- Tests: `tests/unit/test_detect_polygon_from_contour.cpp` (Java parity for `touchesBorder`, `determineCornersOnBorder`, `flip`, plus synthetic end-to-end rectangle and triangle detection cases that don't depend on the BoofCV factory infrastructure).
- Used by: step 7c (`QrCodePositionPatternDetector` runs this with QR-tuned config and consumes the resulting `DetectedInfo` list); step 9 (orchestrator).

## What changed vs Java

- **`BinaryContourFinder`** dependency is collapsed: instead of going through Java's `BinaryContourFinder` wrapper around `LinearContourLabelChang2004`, the polygon detector owns a `LinearContourLabelChang2004` directly and configures `maxContour` + `saveInternalContours` on it per call. Profiling timers (`milliContour` / `milliShapes`) preserved as `MovingAverage`-equivalent doubles.
- **`ContourPacked` + packed-set indirection — re-introduced on the labeller side, materialised on the consumer side.** The labeller stores points in a `PackedSetsPoint2D_I32` per BoofCV; `buildContoursFromPort` copies each set with `appendSetTo()` and materialises into per-blob `Contour { external, internal[] }` so that downstream stages (`PolylineSplitMerge`, `ContourEdgeIntensity`) keep their existing flat-vector consumption pattern. The cycle-A port was the introduction; the cycle-B materialisation step is the bridge.
- **Template `<T extends ImageGray<T>>`** dropped. We commit to `cv::Mat CV_8UC1` for both gray and binary, per CLAUDE.md "Type mappings".
- **`VerbosePrint`** dropped (we don't have a `boofcv.misc.VerbosePrint` infrastructure ported).
- **Lens distortion** (`setLensDistortion`, `removeDistortionFromContour`, `distToUndist` / `undistToDist`) deferred — same deferral pattern as in step 5's grid reader. The undistorted/distorted polygon fields are still populated, with both holding the same coordinates.
- **`MovingAverage`** ported as a tiny inline helper (one `double avg`, exponential decay).
- **`RefinePolygonToGray`** is the next file; not part of this port. Result corners are integer pixels.

## Deviations from upstream

- **`saveInternalContours_` defaults to `true`**. BoofCV's `polygonContour()` factory wires `LinearExternalContours` (`isSaveInternalContours = false`) and only the QR finder-pattern subclass re-enables internal contours. Our default is the more permissive variant: downstream recovery pipelines (per CLAUDE.md "Public API design") that need hole topology — including the QR finder-pattern detector at step 7c, which is the only consumer in this port — get it without an explicit setter call. Callers that want strict-Java parity for non-QR uses can call `setSaveInternalContours(false)`. The `donut` synthetic test in `tests/unit/test_detect_polygon_from_contour.cpp` exercises and asserts this default.
