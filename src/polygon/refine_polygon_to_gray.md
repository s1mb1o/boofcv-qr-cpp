# RefinePolygonToGray chain — subpixel polygon refinement

Companion to `boofcv_qr/polygon/{snap_to_line_edge,refine_polygon_to_gray,detect_polygon_binary_gray_refine}.hpp`. Step 7b (part 3) of the BoofCV port — the subpixel corner-refinement stage that turns the integer-pixel polygons from `DetectPolygonFromContour` into the subpixel-accurate quads consumed by the QR finder-pattern detector and downstream homography sampler.

## Summary

After the contour stage produces a polygon with integer-pixel corners, those corners are still off by up to ~1 pixel because the binary image is a thresholded version of the gray. `RefinePolygonToGrayLine` works directly on the gray image: each side of the polygon is independently snapped to the underlying intensity-edge using a weighted polar line fit, then the corners are recomputed as the intersections of the refined lines. Iterates to convergence (10 passes, 0.01 px tolerance for QR's `ConfigRefinePolygonLineToImage`).

`DetectPolygonBinaryGrayRefine` is the wrapper. It runs the contour stage, optionally adjusts each polygon for thresholding bias (`AdjustPolygonForThresholdBias` — undoes the floor() that binary discretisation injects), and provides `refine(Info)` and `refineAll()` that re-fit each candidate via `RefinePolygonToGrayLine`. An `EdgeIntensityPolygon` quality gate runs before and after refinement: refinement is rolled back if the refined edge's contrast is significantly worse than the pre-refine value (Java's heuristic: `after*1.5 > before` keeps the refined version, otherwise revert).

## Algorithm

### `SnapToLineEdge.refine(a, b, line)` (the primitive)

For one polygon side bounded by image-coord points `a`, `b`:

1. **Local coordinate system.** Compute `center = (a+b)/2` and `localScale = |a - center|`. All sample positions and the fitted line are computed in `(p - center)/localScale` coordinates so the regression doesn't blow up numerically on long sides.
2. **Tangent-side sampling.** The tangent vector pointing from the line into the dark interior of a clockwise-oriented polygon (image coords, +y down) is `(slopeY, -slopeX)/r`. We walk `lineSamples=30` (QR default) evenly spaced positions along the line, and at each one walk `2*radialSamples + 2 = 4` (QR default `radialSamples=1`) samples in the tangent direction. Each sample is the **line integral over a 1-pixel-long segment** in the tangent direction (`ImageLineIntegral.compute`) — that's the BoofCV-specific bit you can't substitute with `cv::cornerSubPix` or even `cv::Sobel`.
3. **Weights = absolute step.** For each adjacent pair of tangent line-integrals, the weight is `|sample0 - sample1|`. Strong intensity steps along the tangent get more influence in the line fit. Weight 0 (no step) is dropped.
4. **Polar line fit.** `FitLine_F64.polar(samplePts, weights, polar)` — closed-form weighted-least-squares line fit to a `LinePolar2D_F64` `(angle, distance)`. Convert to general form `LineGeneral2D_F64 (A, B, C)` and undo the local scale via `localToGlobal`.

`SnapToLineEdge` requires ≥ 4 sample points (otherwise the regression is rank-deficient — return false).

### `RefinePolygonToGrayLine.refine(input, output)`

1. **Sanity check.** Each side must be ≥ `2*cornerOffset + 2` pixels long (default `cornerOffset=1`, so ≥ 4 pixels). Smaller and the refinement step bites into the corner aliasing zone and diverges.
2. **Initial general-form lines.** Compute one `LineGeneral2D_F64` per side from the integer-pixel input.
3. **Iterate (max 10 iterations).** Per iteration:
   - For each side: compute `(adjA, adjB)` by stepping `cornerOffset` away from each corner along the side direction (the line endpoints are aliased, so we don't sample there). Pass `(adjA, adjB)` to `SnapToLineEdge.refine` to get a refined `general[i]` line.
   - **Divergence guard.** If the corner of `general[k] ∩ general[i]` has moved more than `maxCornerChangePixel = 2` pixels from the pre-iteration corner, revert this side's refinement (keep `before`).
   - Recompute `current` corners as `general[i] ∩ general[(i+1)%N]` via `UtilShapePolygon.convert`.
   - Convergence test: if every corner moved < `convergeTolPixels = 0.01` (squared comparison: `< 0.01²`), break. Otherwise `previous = current` and loop.

Returns `true` if at least one iteration's refinement was kept.

### `DetectPolygonBinaryGrayRefine.process(gray, binary)`

1. **Contour stage** — `DetectPolygonFromContour::process(gray, binary)`. This produces the candidate `Info` list with integer-pixel polygons.
2. **Threshold-bias adjustment** — if `adjustForThresholdBias=true` (QR default), each polygon is shifted-and-re-cornered via `AdjustPolygonForThresholdBias`. The shift undoes the half-pixel skew that binary thresholding introduces on edges with sub-pixel boundaries. Polygons that lose corners during the adjustment (sides become parallel) are removed.

`refine(Info)`: gates on edge-contrast-before-vs-after (`EdgeIntensityPolygon`) and rolls back refinement if it diverged. `refineAll()`: invoke `refine(Info)` for every candidate.

`getPolygons(out, infoOut)`: returns just the polygons with `Info::computeEdgeIntensity() ≥ minimumRefineEdgeIntensity` (QR default `6`).

## Why this approach over alternatives

- **`cv::cornerSubPix`**. CLAUDE.md "Forbidden moves" line 142 forbids it. Sub-pixel corner finding via image-gradient + Hessian (cornerSubPix) reduces detection rate on real QR images by ~3-4 percentage points (BoofCV regression). The line-integral-based sampling here is BoofCV's specific innovation: it fits each side INDEPENDENTLY against the local edge, then derives corners from line intersections — that's much more robust to the corner-aliasing and asymmetric-blur conditions a real QR finder pattern presents.
- **`cv::Sobel + line fitting`**. Could approximate — but the line-integral primitive computes a perpendicular-band sum, not a single-pixel gradient, which is robust to sub-pixel mis-alignment of the initial side estimate.
- **Iterative least-squares without the `localScale` trick**. Numerical conditioning of the polar-line normal equations degrades when the sample coordinates are large (e.g. `x ≈ 1000`). BoofCV's `localScale` trick is what keeps the regression accurate. Drop it and you'll see drift on far-from-origin polygons.

## Failure modes

- **Side too short.** `checkShapeTooSmall` returns true → `refine()` returns false. Caller keeps the original polygon.
- **Sample window outside image.** `SnapToLineEdge.computePointsAndWeights` skips line positions whose sample window extends past the image bounds; if all are skipped (line entirely on the border), we return false. `RefinePolygonToGrayLine`'s iteration loop reverts the line to its pre-iteration estimate, leaving sides along the border untouched. This is what makes the `fitWithEdgeOnBorder` JUnit test pass.
- **Divergence past `maxCornerChangePixel`.** Per-side guard rolls back. Line is unchanged for that iteration; other sides may still progress.
- **Two parallel adjacent lines.** `UtilShapePolygon.convert` returns false (intersection at infinity); `RefinePolygonToGrayLine.refine` returns false.
- **All weights zero.** `FitLine_F64.polar` returns `null` (we throw `std::runtime_error("All weights were zero")`, mirroring Java's `RuntimeException`). Means the refinement is being run on a uniform region — programmer error, not a runtime data condition.

## Tunable parameters (with QR defaults from `ConfigRefinePolygonLineToImage`)

| Parameter | Default | Effect |
|---|---|---|
| `cornerOffset` | 1.0 | Pixels to step in from each corner before sampling. Higher = avoids aliasing but loses short-side support. |
| `lineSamples` | 30 | How many positions along the side to sample. |
| `sampleRadius` | 1 | Tangent-direction sample count per position is `2*r + 2`. |
| `maxIterations` | 10 | EM-style refine outer loop cap. |
| `convergeTolPixels` | 0.01 | Per-corner movement under which we declare convergence. (BoofCV's default is 0.2; QR's is 0.01 — tighter.) |
| `maxCornerChangePixel` | 2.0 | Per-side divergence guard. |
| `minimumRefineEdgeIntensity` | 6.0 (QR) | Post-refine pruning gate on `edgeOutside - edgeInside`. |

## Public API design

Per CLAUDE.md "Public API design":

- `RefinePolygonToGrayLine`/`SnapToLineEdge`/`DetectPolygonBinaryGrayRefine` are concrete classes, not templates (we commit to `cv::Mat CV_8UC1`).
- `RefinePolygonToGray` (the abstract interface) exists so callers can swap in a different refinement strategy. Default factory wires `RefinePolygonToGrayLine`.
- `DetectPolygonBinaryGrayRefine::getDetector()` exposes the underlying contour stage by reference (for the QR finder-pattern detector at step 7c, which sets a `PolygonHelper` on it).
- `getPolygonInfo()` returns `const std::vector<DetectedInfo>&` (value-typed; matches the storage in `DetectPolygonFromContour`).
- All injectable strategy hooks (`PolygonHelper`, the refine algorithm itself) take `std::shared_ptr` per CLAUDE.md line 32.

## Cross-references

- Upstream BoofCV (pinned `v1.3.0`):
  - `boofcv-feature/.../shapes/polygon/RefinePolygonToGray.java` (interface)
  - `boofcv-feature/.../shapes/polygon/RefinePolygonToGrayLine.java` (concrete impl)
  - `boofcv-feature/.../shapes/polygon/UtilShapePolygon.java` (line-intersection helper)
  - `boofcv-feature/.../shapes/polygon/DetectPolygonBinaryGrayRefine.java` (wrapper)
  - `boofcv-feature/.../shapes/polygon/AdjustPolygonForThresholdBias.java`
  - `boofcv-feature/.../shapes/edge/SnapToLineEdge.java` (the kernel)
  - `boofcv-feature/.../shapes/edge/EdgeIntensityPolygon.java` (post-refine quality gate)
  - `boofcv-feature/.../shapes/edge/ScoreLineSegmentEdge.java` (used by the gate)
  - `boofcv-feature/.../shapes/edge/BaseIntegralEdge.java` (image+integral plumbing)
  - `boofcv-ip/.../alg/interpolate/ImageLineIntegral.java` (the line-integral primitive)
  - `boofcv-feature/.../factory/shape/ConfigRefinePolygonLineToImage.java` (config)
  - `georegression-0.28.2.jar` `Distance2D_F64`, `UtilLine2D_F64.convert`, `Intersection2D_F64.intersection`, `FitLine_F64.polar`, `UtilPolygons2D_F64.removeAdjacentDuplicates` (formulas inlined; cited at the call site)
- Tests: `tests/unit/test_refine_polygon_to_gray.cpp` mirrors:
  - `TestImageLineIntegral.java` (line-integral algorithmic tests — pure, image-only, easy to port)
  - `TestSnapToLineEdge.java` (`computePointsAndWeights`, `localToGlobal`, `easy_aligned`, `computePointsAndWeights_border`)
  - `TestRefinePolygonToGrayLine.java` (`alignedSquare`, `fit_perfect_affine` — synthesised perfect rectangles)
- Used by: step 7c (`QrCodePositionPatternDetector` runs `DetectPolygonBinaryGrayRefine.refineAll()` then reads `Info.polygon`).

## What changed vs Java

- **Template parameter `<T extends ImageGray<T>>`** dropped throughout the chain. We commit to `cv::Mat CV_8UC1` per CLAUDE.md "Type mappings".
- **`ImageLineIntegral.image`** uses `cv::Mat` directly (with `at<uint8_t>(y, x)` access — `(y, x) = (row, col)` per CLAUDE.md "Coordinate conventions"). The `GImageGray` indirection is dropped.
- **`BaseIntegralEdge`** distortion machinery (`GImageGrayDistorted`, `setTransform`) is **deferred** — same pattern as `DetectPolygonFromContour::setLensDistortion`. The image is sampled directly via `cv::Mat::at` until distortion lands.
- **`DogArray<Point2D_F64>`** → `std::vector<cv::Point2d>`. `// TODO(perf): recycle` markers where the Java pool is reused.
- **Geometric helpers inlined**: `lineGeneralFromTwoPoints`, `lineGeneralIntersection`, `polarToGeneral`, `fitLinePolar` (weighted), `removeAdjacentDuplicatesPolygon`. Each cites its georegression-0.28.2 origin in a comment, mirroring the bytecode-decompiled formulas.
- **`MovingAverage`** uses the same inline helper introduced for `DetectPolygonFromContour`.
- **Lens distortion** (`setLensDistortion`, `setTransform`, `clearLensDistortion`) deferred — stub no-ops at the public surface so callers can call them harmlessly.
- **`RefinePolygonToContour`** **not ported** — the Java factory wires `refineContour=null` for QR (it's an alternative refinement stage that contour-only callers use). Documented; `refine()` skips that branch.
- **`AdjustBeforeRefineEdge functionAdjust`** hook **not ported** — unused by QR, no compelling test case.
- **`VerbosePrint`** dropped throughout.
