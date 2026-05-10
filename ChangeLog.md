# ChangeLog

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

