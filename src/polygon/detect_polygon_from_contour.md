# DetectPolygonFromContour — black-blob to polygon detector

Companion to `boofcv_qr/polygon/detect_polygon_from_contour.hpp`. Step 7b (part 2) of the BoofCV port — wraps the contour finder, the polyline corner-finder (`PolylineSplitMerge` from part 1), an edge-intensity false-positive filter, and the bookkeeping that turns a binary image into a list of candidate polygons.

## Summary

Given a binary image (`CV_8UC1`, 0/1, foreground = dark module per CLAUDE.md) and the original gray-scale image, find every dark blob whose external contour fits a polygon of `[minSides, maxSides]` corners with low side-fit error and high contour-edge contrast. Each result carries the source contour, the polygon corners (integer pixel), the corner indices into the contour, edge-intensity diagnostics, and per-corner border-touch flags.

Downstream stages — `RefinePolygonToGray` (subpixel corner refinement, next file) and the QR finder-pattern detector (`QrCodePositionPatternDetector`) — consume this output. The QR finder pattern is itself three nested square contours; the inner two land here as "internal" contours within the parent blob. Step 7c re-uses those inside the same blob, which is why the contour topology must preserve external + internal grouping.

## Algorithm

`process(gray, binary)` runs five stages per blob:

1. **Contour extraction.** Use `cv::findContours(binary, contours, hierarchy, cv::RETR_CCOMP, cv::CHAIN_APPROX_NONE)` per CLAUDE.md "OpenCV substitution policy". `RETR_CCOMP` gives a 2-level hierarchy: top-level entries are external blob boundaries, their children are internal hole boundaries. We re-bundle these into a `Contour` struct (`std::vector<cv::Point2i> external` + `std::vector<std::vector<cv::Point2i>> internal`) — same shape as BoofCV's `boofcv.alg.filter.binary.Contour`. The connectivity rule is 8-connected (matches `ConfigQrCode.polygon.detector.contourRule = ConnectRule.EIGHT` and OpenCV's `findContours` default — they align).

2. **Per-blob pre-filtering** (drops cheaply-detected non-polygons before the expensive corner-finder runs):
   - **Length cut:** contour pixel-count below `minimumContourPixels` is rejected. The threshold comes from `ConfigLength.computeNegMaxI(sqrt(W*H))` on the configured `minimumContour`, clamped to ≥ 4. (For QR `ConfigQrCode` overrides this to `fixed(40)`.)
   - **Border touch rule:** if `canTouchBorder=false` and any contour pixel is on `x∈{0,W-1}` or `y∈{0,H-1}`, reject.
   - **`PolygonHelper.filterContour` hook** (optional): caller can short-circuit a contour for any reason. Used by the QR finder-pattern subclass which doesn't yet exist in the port.
   - **Edge-intensity sanity check** (optional, gated by `contourEdgeThreshold > 0`): `ContourEdgeIntensity` samples the gray image inside and outside the contour at `30` evenly-spaced points, walking 1 pixel along the local tangent normal in each direction. If `|edgeOutside - edgeInside| < contourEdgeThreshold`, reject as low-contrast noise.

3. **Polygon fitting.** Call `PolylineSplitMerge::process(contour)` (the corner-finder from part 1) and read its `getBestPolyline()`. The returned `splits[]` are indices into the contour where the corners are. `PolylineSplitMerge`'s tunables (min/max sides, convex flag, side-error gate, corner-score penalty, etc.) are forwarded by the public `setNumberOfSides()`, `setConvex()` API.

4. **Orientation classification.** With pre-fit corners, compute `UtilPolygons2D_I32.isCCW(polygonPixel)` (sign-of-cross-product vote across all corner triplets). If `outputClockwiseUpY == isCCW` we flip the order in place — note "+y up" matches geometric convention; image-coordinate "+y down" inverts the visual sense (mirroring upstream comment). For QR we set `outputClockwiseUpY=false` (`ConfigQrCode.polygon.detector.clockwise = false`) → output is CCW in +y-up = CW in image coords, which is how the finder-pattern stage downstream expects things. Edge-inside / edge-outside diagnostics get swapped if `isCCW` was wrong, then the white-blob filter runs: `edgeInside > edgeOutside` rejects (we want black squares).

5. **Polygon record assembly.** Build a `DetectedInfo` with the corner array (`polygon`, undistorted; `polygonDistorted` is identical when no lens distortion is set), the source contour, the per-corner border-touch booleans (`determineCornersOnBorder`), the edge intensities, and the `splits[]` array. Reject below `minimumArea` (computed as `(minimumContourPixels/4)²` — chord of a degenerate skinny shape).

## OpenCV substitution policy

Per CLAUDE.md "OpenCV substitution policy" line 145:

| Stage | OpenCV | BoofCV equivalent | Why this is OK |
|---|---|---|---|
| Contour extraction | `cv::findContours(RETR_CCOMP, CHAIN_APPROX_NONE)` | `LinearContourLabelChang2004` + `Contour` | Both produce the same external + internal blob topology with full per-pixel ordering. `RETR_CCOMP` matches BoofCV's connect-rule-8 default since OpenCV's `findContours` is 8-connected. |
| (none) | (none) | `boofcv.alg.shapes.polyline.splitmerge.PolylineSplitMerge` | This is *forbidden* to substitute. Used verbatim from part 1. |

`cv::findContours` mutates the input image. The port clones internally (one allocation per `process()`), so callers can pass a binary mat without surprise.

A previously-investigated edge case: BoofCV's contours and OpenCV's contours can differ slightly in the offset they assign to the first pixel of a vertical edge (BoofCV walks 8-connected with a specific tracer; OpenCV uses Suzuki's algorithm). For QR detection rates the difference is empirically below the parity floor, but if the regression baseline regresses by more than ~0.5 percentage points in a category dominated by tiny QRs, this is the first place to look.

**Winding direction:** OpenCV's `findContours` emits *external* contours **CCW in image coords** (CW in math) and *internal* contours **CW in image** (CCW in math). BoofCV's `LinearContourLabelChang2004` emits the OPPOSITE windings. The polyline corner finder's convex check (`PolylineSplitMerge::setSplitVariables`'s `isPositiveZ(a, b, c)`) is hard-coded for BoofCV's convention — under the OpenCV winding, the check rejects splits that would form a CONVEX corner instead of concave. So `buildContoursFromOpenCV` reverses each contour in place. Without the reversal, a perfect black square never gets a 4-corner fit (only the initial 3-corner triangle survives), which surfaced as the original failing JUnit-equivalent rectangle tests.

## Failure modes

- **Empty blob list** — clean image, no foreground. Returns empty result. Not an error.
- **Convex flag mismatch** — `ConfigQrCode` sets `convex=true`. A non-convex shape (e.g. the connected outer ring of a finder pattern when binarisation noise bridges the outer black to the white interior) is silently dropped; expected behaviour.
- **Contour clones** — `cv::findContours` modifies its input. We clone. Don't pass a const reference and expect aliased behaviour.
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
| `maximumContour` | `fixed(-1)` = unlimited | (Same.) Not gated in this port; only the min is enforced. |

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
  - `boofcv-ip/src/main/java/boofcv/alg/filter/binary/LinearContourLabelChang2004.java` (replaced by `cv::findContours`)
  - `georegression-0.28.2.jar` `Area2D_F64.polygonSimple` and `UtilPolygons2D_I32.isCCW` (both inlined)
- Tests: `tests/unit/test_detect_polygon_from_contour.cpp` (Java parity for `touchesBorder`, `determineCornersOnBorder`, `flip`, plus synthetic end-to-end rectangle and triangle detection cases that don't depend on the BoofCV factory infrastructure).
- Used by: step 7c (`QrCodePositionPatternDetector` runs this with QR-tuned config and consumes the resulting `DetectedInfo` list); step 9 (orchestrator).

## What changed vs Java

- **`BinaryContourFinder`** dependency replaced by `cv::findContours` per CLAUDE.md "OpenCV substitution policy". The Java `process()` method's `contourFinder.process(binary)` becomes our internal `findContours(...)` call; `contourFinder.getContours()` becomes iteration over our local `std::vector<Contour>`. Profiling timers (`milliContour` / `milliShapes`) preserved as `MovingAverage`-equivalent doubles.
- **`ContourPacked` + packed-set indirection** dropped. OpenCV gives us full point arrays per contour, so we store points directly inside `Contour`. `loadContour(externalIndex, ...)` → `contour.external` directly.
- **Template `<T extends ImageGray<T>>`** dropped. We commit to `cv::Mat CV_8UC1` for both gray and binary, per CLAUDE.md "Type mappings".
- **`VerbosePrint`** dropped (we don't have a `boofcv.misc.VerbosePrint` infrastructure ported).
- **Lens distortion** (`setLensDistortion`, `removeDistortionFromContour`, `distToUndist` / `undistToDist`) deferred — same deferral pattern as in step 5's grid reader. The undistorted/distorted polygon fields are still populated, with both holding the same coordinates.
- **`MovingAverage`** ported as a tiny inline helper (one `double avg`, exponential decay).
- **`RefinePolygonToGray`** is the next file; not part of this port. Result corners are integer pixels.
