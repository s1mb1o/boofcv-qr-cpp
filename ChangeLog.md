# ChangeLog

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

