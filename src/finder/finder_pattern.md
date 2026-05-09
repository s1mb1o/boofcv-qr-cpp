# QR finder-pattern detector chain

Step 7c of the BoofCV port. Three classes turn the candidate-polygon list from `DetectPolygonBinaryGrayRefine` (step 7b/3) into a graph of QR finder patterns (the three "L"-corner squares with the 1:1:3:1:1 black/white ratio).

## Summary

A QR finder pattern is three nested concentric black/white squares: a 7-module-wide black ring on the outside, a 5-module-wide white ring inside that, and a 3-module-wide black centre stone. When scanned along its centerline, the runs of black/white pixels follow the ratio 1:1:3:1:1 — that is the defining feature this detector keys on.

The chain has three stages:

1. **`SquareLocatorPatternDetectorBase`** — abstract base wrapping `DetectPolygonBinaryGrayRefine` (step 7b/3). Configures the wrapped polygon detector for "convex 4-sided shapes, output CCW in image coords (`outputClockwiseUpY=false`)", binds an EXTENDED-border bilinear image sampler, and runs the polygon stage. `findLocatorPatternsFromSquares()` is the abstract hook — concrete subclasses turn the `DetectedInfo` list into their own marker-specific node type.
2. **`QrCodePositionPatternDetector`** — extends the base. Each candidate polygon is gated by a 1:1:3:1:1 line-scan check (`checkPositionPatternAppearance`); survivors get an extra refinement pass and a `PositionPatternNode` is materialised with the local `grayThreshold = (edgeInside + edgeOutside) / 2` and the polygon's geometric centre computed via `SquareGraph::computeNodeInfo`.
3. **`QrCodePositionPatternGraphGenerator`** — for each candidate finder pattern, search nearby finder patterns (within `1.2 × maximumQrCodeWidth`) and connect them in pairs whose geometry is consistent with two adjacent finders of the same QR code. The result is a graph where each node is one finder pattern and each edge is a likely "neighbour-in-the-same-QR" relationship. Step 9's orchestrator walks this graph to assemble triplets.

## Algorithm details

### `SquareLocatorPatternDetectorBase::process(gray, binary)`

1. Configure the contour detector via `configureContourDetector(gray)`: max contour length is `maxContourFraction * min(W, H)` (default `4/3`); inner contours are disabled (`saveInnerContour=false`). Both are perf optimizations — they don't affect correctness but they reduce work substantially on large inputs by pruning runaway external contours and skipping the inside-contour list (which the QR finder doesn't use).
2. Bind `interpolate.setImage(gray)` so the subclass can sample the gray image during its appearance-check.
3. `squareDetector.process(gray, binary)` — runs the contour stage and the threshold-bias adjust per polygon.
4. `findLocatorPatternsFromSquares()` — abstract hook; QR's subclass walks the polygon list and gates on the 1:1:3:1:1 appearance check.

### `QrCodePositionPatternDetector::checkPositionPatternAppearance`

For each candidate 4-corner polygon, two centerlines are scanned (horizontal-through-midpoints and vertical-through-midpoints). Each centerline samples 46 points: 9 stripes of 5 samples each + 1 trailing. Intensity is gray-image bilinear (EXTENDED border). Each sample is thresholded against `grayThreshold` (the average of inside/outside edge intensity recovered by the contour stage). The thresholded scan is run-length-encoded and tested against the 1:1:3:1:1 pattern at every offset:

- 5 consecutive runs starting on a black run.
- The first and last black runs each have lengths in `[0.4 × adjacent_white, 3 × adjacent_white]`.
- The centre black run has length in `[black0+black2, 2*(black0+black2)]`.

If either of the two centerlines passes, the polygon is accepted as a finder pattern.

### `QrCodePositionPatternGraphGenerator::process`

1. Reset the underlying `SquareGraph` (recycles edges).
2. For each finder pattern `f`:
   - Compute the maximum possible inter-finder distance for this candidate: `maximumQrCodeWidth = f.largestSide × (17 + 4*maxVersionQR - 7) / 7`. Search radius is `1.2 × maximumQrCodeWidth` (squared for the brute-force NN).
   - Run nearest-neighbour search over the candidate list (squared centre-to-centre distance ≤ search radius).
   - For each NN result, call `considerConnect(f, neighbour)` which:
     - Uses `SquareGraph::findSideIntersect` to find the side of each square the line connecting the two centres pierces.
     - Verifies the piercing is near the side midpoint (offset ≤ 0.35 of side length).
     - Checks the intersected sides have similar lengths (`|a - b| / max(a, b) ≤ 0.25`).
     - Checks the two sides are within 45° of parallel (`SquareGraph::almostParallel`).
     - Checks the *other* sides aren't more parallel — the smallest-side / largest-side ratio across both nodes must be ≤ 1.3 to reject 90°-rotated cases.
     - Computes a score `lineLength × (1 + acuteAngle + sideOffset / 2)` and registers the connection via `SquareGraph::checkConnect` (which keeps only the best edge per side).

After this pass the graph holds candidate finder-pattern neighbour pairs. Step 9's orchestrator searches for nodes with exactly 2 neighbours (= corner of an L-shape) to identify QR triplets.

## Why these choices

- **Image-coordinate convention.** Same image-CW = math-CCW interpretation we settled on in step 7b. `outputClockwiseUpY = false` matches `ConfigQrCode`'s `polygon.detector.clockwise = false`.
- **Brute-force NN instead of `KdTreeSquareNode` over `ddogleg::NearestNeighbor`.** BoofCV uses ddogleg's KD-tree for asymptotic O(n log n). We don't pull in ddogleg. For QR-relevant inputs the candidate count is O(20) finder squares per image (typically much less), which makes brute-force O(n²) ≈ 400 distance evaluations per image — well under a millisecond. **Reachability of the same set of triplets is preserved** since both algorithms enumerate the same neighbour set; only the iteration order can differ. Iteration order doesn't affect output because `SquareGraph::checkConnect` is order-insensitive (the best-score edge wins for each node side regardless of insertion order).
- **`KdTreeSquareNode::distance`** (ported in step 7a) is the squared-centre-to-centre distance functor — same metric both NN strategies use. The brute-force loop just calls it directly.
- **Two-line appearance check** instead of a finite-state-machine over the contour. The horizontal/vertical centerline scans key on the 1:1:3:1:1 ratio that makes QR finders distinctive — even mild perspective distortion preserves the *count* of black/white runs, only the lengths bend, and the `[0.4×, 3×]` ratio bands tolerate that.
- **`grayThreshold = (edgeInside + edgeOutside) / 2`**. The polygon-stage edge-intensity scorer already produced these — re-using them here avoids re-sampling the image.

## Failure modes

- **No finder-pattern triplet found** — graph empty after `process()`. Returns silently; step 9 reports `Failure::NO_FINDER_PATTERN`. Common causes: heavy blur, low contrast (filtered earlier by `minimumRefineEdgeIntensity`), or perspective distortion past 45° (the `almostParallel` check rejects).
- **`maxVersionQR` mis-set.** Defaults to `40` (the QR spec maximum). Lowering it tightens the NN search radius, which can drop legitimate finder pairs in close formations of small QRs.
- **`maxContourFraction` mis-tuned.** Default `4/3` — small enough that absurdly large external contours (e.g. an entire page border) get pruned, large enough that two finders on a single sheet of paper aren't.
- **Lens distortion deferred** — `setLensDistortion` is a no-op stub. The base class's interpolate-distortion-aware sampler is documented but bypassed; the QR pipeline runs in raw image coordinates.

## Tunable parameters (with QR defaults from `ConfigQrCode`)

| Parameter | Default | Effect |
|---|---|---|
| `maxContourFraction` | 4/3 | Caps external contour length at `(4/3) * min(W, H)`. |
| `maxVersionQR` | 40 | Upper bound on QR version → NN search radius. |
| (`SquareGraph` parallelThreshold) | 45° | From step 7a. |

The wrapper config `ConfigPolygonDetector` from step 7b/3 is the broader knob — sides=4..4, convex=true, `clockwise=false` — set during the base ctor.

## Public API per CLAUDE.md "Public API design"

- `getPositionPatterns()` returns `const std::vector<PositionPatternNode>&` (value-typed storage; reference invalidated by next `process()`).
- `getSquareDetector()` returns a const reference to the wrapped `DetectPolygonBinaryGrayRefine` for inspection.
- Lens distortion stays as a documented no-op stub — same deferral pattern as steps 5/7b.
- All injection hooks (`PolygonHelper` indirectly via the wrapped detector) take `std::shared_ptr` per CLAUDE.md line 32.

## Cross-references

- Upstream BoofCV (pinned `v1.3.0`):
  - `boofcv-recognition/.../qrcode/SquareLocatorPatternDetectorBase.java` (147 LOC)
  - `boofcv-recognition/.../qrcode/QrCodePositionPatternDetector.java` (219 LOC)
  - `boofcv-recognition/.../qrcode/QrCodePositionPatternGraphGenerator.java` (176 LOC)
  - `boofcv-recognition/.../qrcode/PositionPatternNode.java` (ported in step 7a)
  - `boofcv-recognition/.../calib/squares/SquareGraph.java` (ported in step 7a)
  - `boofcv-recognition/.../calib/squares/SquareNode.java` + `KdTreeSquareNode` (ported in step 7a)
  - `georegression-0.28.2.jar` `UtilPoint2D_F64.mean(a, b, out)` and `UtilLine2D_F64.convert(LineSegment, LineParametric)` — formulas inlined.
  - `ddogleg` `NearestNeighbor` / `FactoryNearestNeighbor.kdtree` — replaced by brute-force O(n²) over the candidate list (documented above).
- Tests:
  - `tests/unit/test_square_locator_pattern_detector_base.cpp` — exercises the base's `process()` shape-validation and `configureContourDetector` plumbing.
  - `tests/unit/test_qr_code_position_pattern_detector.cpp` — mirrors `TestQrCodePositionPatternDetector.java` (`easy`, `checkPositionPatternAppearance`, `positionSquareIntensityCheck`).
  - `tests/unit/test_qr_code_position_pattern_graph_generator.cpp` — mirrors `TestQrCodePositionPatternGraphGenerator.java` (`considerConnect_positive`, `considerConnect_negative_rotated`).
- Used by: step 9 (`QrCodeDecoderImage` orchestrator).

## What changed vs Java

- **Template `<T extends ImageGray<T>>`** dropped throughout. Commit to `cv::Mat CV_8UC1` per CLAUDE.md.
- **`InterpolatePixelS<T>` + `BorderType.EXTENDED` bilinear** — inlined as a small helper in the detector that reads the gray `cv::Mat` with EXTENDED-border (clamp) bilinear sampling. Same formula as the one used in step 7b/3's `ContourEdgeIntensity`.
- **`MovingAverage profilingMS`** — uses the same inline exponential-decay update from steps 7b/2 and 7b/3.
- **`VerbosePrint`** dropped (no infra ported).
- **`LensDistortionNarrowFOV` + `InterpolatePixelDistortS`** — `setLensDistortion` is a stub; same deferral pattern as steps 5 / 7b.
- **`ddogleg::NearestNeighbor` over `KdTreeSquareNode`** — replaced with a brute-force O(n²) loop that calls `KdTreeSquareNode::distance`. Documented above.
- **`DogArray<PositionPatternNode>` recycling** — `std::vector<PositionPatternNode>` (value-typed storage; `// TODO(perf): recycle`).
- **`UtilPoint2D_F64.mean(a, b, out)` and `UtilLine2D_F64.convert(LineSegment, LineParametric)`** — inlined in the appearance-check function with comments citing the upstream formula.
