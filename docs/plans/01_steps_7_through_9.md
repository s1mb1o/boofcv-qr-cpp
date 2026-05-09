# Plan: Steps 7–9 of the BoofCV QR port

Date: 2026-05-09
Author: Claude (this session)
Status: ready for follow-up session

## Context

Steps 1–6 are complete and committed (see `ChangeLog.md` and `git log`). At
the end of step 6 the project has:

- A green C++17 + GoogleTest build (124/124 unit tests pass).
- The Java baseline harness producing zero quality drift on the locked
  `tests/baseline.json` (74.40% aggregate detection rate, BoofCV 1.3.0
  on the 562-image / 1258-GT `boofcv-qrcodes` regression set).
- All algorithmic layers from binarization down through Reed-Solomon,
  mode decoding, format/version BCH, and the homography-based grid
  sampler.

Steps 7 (polygon + finder pattern detection), 8 (alignment pattern
detection), and 9 (top-level orchestrator) are **not yet started**.
This plan exists because step 7 alone is a multi-thousand-line
verbatim-port effort — comparable in scope to all of steps 1–6
combined — and the user opted to leave it for a follow-up session
rather than accept a simplified polygon stage.

## Sizing the work (line counts from BoofCV v1.3.0)

| Component | Java LOC | Notes |
|---|---:|---|
| **Step 7 (polygon + finder)** | ~3,500 (excluding test code) | |
| `boofcv.alg.fiducial.calib.squares.SquareNode` | 162 | Trivial port |
| `boofcv.alg.fiducial.calib.squares.SquareGraph` | 218 | Trivial port |
| `boofcv.alg.fiducial.calib.squares.SquareEdge` | 82 | Trivial port |
| `qrcode.PositionPatternNode` | 39 | Trivial port |
| `qrcode.QrCodePositionPatternDetector` | 219 | Wires polygon detector to graph generator |
| `qrcode.QrCodePositionPatternGraphGenerator` | 176 | Triplet-finding logic |
| `qrcode.SquareLocatorPatternDetectorBase` | 147 | Base class |
| `boofcv.alg.shapes.polygon.DetectPolygonBinaryGrayRefine` | 321 | Wraps the contour-to-polygon chain |
| `boofcv.alg.shapes.polygon.DetectPolygonFromContour` | 638 | Polygon detector entry point |
| `boofcv.alg.shapes.polyline.splitmerge.PolylineSplitMerge` | 907 | **Hardest piece** — corner finder |
| Plus the `RefinePolygonToGray` chain | ~1,000 | Subpixel corner refinement |
| **Step 8 (alignment pattern)** | ~310 | |
| `qrcode.QrCodeAlignmentPatternLocator` | 311 | Compact; mostly self-contained |
| **Step 9 (orchestrator)** | ~625 | |
| `qrcode.QrCodeDecoderImage` | 625 | Wires everything together |
| **Plus their JUnit suites** | ~1,500–2,000 | |

Conservative estimate: **~8,000 lines of source + ~2,000 lines of test**
to port verbatim, plus algorithm docs. The deepest piece is
`PolylineSplitMerge` (BoofCV's split-merge corner-finding algorithm) —
unique to BoofCV, no straightforward OpenCV equivalent, and CLAUDE.md
explicitly forbids substituting `cv::approxPolyDP` because the
polygon stage's subpixel accuracy directly drives the QR sampling
parity.

## Recommended sequencing for the follow-up session

### 7a. Square graph utilities (small, no dependencies)

Files to port verbatim, in order:

1. `boofcv-recognition/.../calib/squares/SquareNode.java`
   → `include/boofcv_qr/squares/square_node.hpp` + `src/polygon/square_node.cpp`
2. `boofcv-recognition/.../calib/squares/SquareEdge.java`
   → `include/boofcv_qr/squares/square_edge.hpp`
3. `boofcv-recognition/.../calib/squares/SquareGraph.java`
   → `include/boofcv_qr/squares/square_graph.hpp` + `src/polygon/square_graph.cpp`
4. `boofcv-recognition/.../qrcode/PositionPatternNode.java`
   → `include/boofcv_qr/position_pattern_node.hpp`
5. Tests: mirror `TestSquareGraph.java` and any `TestPositionPattern*`.

Each of these uses `cv::Point2d` / `std::array<cv::Point2d, 4>` per
CLAUDE.md type mappings. Total: ~500 lines of source. Estimate: 0.5
work-day.

### 7b. Polygon-from-contour stack (the deep dive)

Order matters because the dependencies cascade:

1. `boofcv-feature/.../shapes/polyline/splitmerge/PolylineSplitMerge.java`
   (907 LOC, **the hardest single file**). Pure-C++ port; uses no OpenCV
   beyond `cv::Point2i` for contour pixels. Mirror its JUnit suite.
2. `boofcv-feature/.../shapes/polygon/DetectPolygonFromContour.java`
   (638 LOC). Wraps `PolylineSplitMerge` + `cv::findContours` with
   `RETR_CCOMP`/`RETR_TREE` per CLAUDE.md "OpenCV substitution
   policy". Hierarchy parsing is non-trivial: BoofCV's
   `LinearContourLabelChang2004` returns a parent-child structure
   we must reconstruct from OpenCV's `hierarchy` Mat.
3. Subpixel refinement chain (`RefinePolygonToGrayLine`,
   `LineGray2D_F64`, etc., ~1,000 LOC). Important for parity:
   subpixel-accurate corners are what makes BoofCV beat naïve
   contour-approx-based detectors.
4. `boofcv-feature/.../shapes/polygon/DetectPolygonBinaryGrayRefine.java`
   (321 LOC). Top-level wrapper.

Estimate: 3–5 work-days. Carries the highest parity risk — corner
accuracy here propagates into homography accuracy and bit-sampling
accuracy. **Run `tests/regression/score.py` after every commit in
this segment** and gate on per-category drift < 5%.

### 7c. Finder pattern detector

Files (verbatim, in order):

1. `qrcode/SquareLocatorPatternDetectorBase.java`
   (147 LOC). Base class.
2. `qrcode/QrCodePositionPatternDetector.java`
   (219 LOC). Wires polygon stage to finder candidates.
3. `qrcode/QrCodePositionPatternGraphGenerator.java`
   (176 LOC). Pairs finder candidates into triplets via the
   `SquareGraph` infrastructure from 7a.

Estimate: 1 work-day. Tests: mirror
`TestQrCodePositionPatternDetector.java` and
`TestQrCodePositionPatternGraphGenerator.java`.

**Step-7 commit boundary**: at this point the binary → polygon →
finder chain is functional. Wire a smoke test that loads a clean
nominal-category QR image, runs the chain, and asserts ≥ 1 finder
triplet detected.

### 8. Alignment pattern detector

`qrcode/QrCodeAlignmentPatternLocator.java` (311 LOC, self-contained).
The algorithm: given a coarse homography from the 3 finders + the QR
version, look up the expected alignment-pattern positions from
`VERSION_INFO[v].alignment`, locally search a 5×5 module window
around each expected position for a 5×5 alignment-pattern signature
(black centre + white ring + black ring + white background), and
refine the centre subpixel.

Tests: mirror `TestQrCodeAlignmentPatternLocator.java`. **Add the
geometry fields to `QrCode`** at this point (`ppCorner`, `ppDown`,
`ppRight` populated by step 7c above; `alignment` populated here).

Estimate: 1 work-day.

### 9. Top-level orchestrator + dataset regression

`qrcode/QrCodeDecoderImage.java` (625 LOC). Wires:

```
binary → polygon → finder → version-detection (decoder bits)
       → alignment (with rough homography) → final homography
       → bit sampling → format/mask/RS/mode (already done in steps 2–4)
```

Plus considerable bookkeeping for `bitsTransposed`, multi-attempt
decoding, threshold-feedback between binarization and grid sampling.
This is also where:

- `setTransformFromLinesSquare` from step 5 finally gets used (the
  unknown-version sampling path).
- The `std::function` strategy injection hooks called out by codex
  in step 4 (RS strategy, alignment-pattern strategy) should land,
  per CLAUDE.md "Public API design".
- The per-block decode status / RS error positions field that codex
  flagged on step 4 should be added to `QrCode` and threaded through
  `QrCodeDecoderBits::applyErrorCorrection`.

**Step-9 commit boundary**: end-to-end pipeline with `cv::Mat
input → std::vector<QrCode> output`. Runs the full
`pricetag-vision-datasets/data/external/boofcv-qrcodes` regression
set via a new `tools/cli/` binary, score with the existing
`tests/regression/score.py`. CI gate per CLAUDE.md: per-category
read rate within 2% of the Java baseline in `tests/baseline.json`.

Estimate: 3–4 work-days, plus regression-iteration time to close the
gap if early runs come in below the ±2% target.

## Total estimate

| Phase | Days |
|---|---:|
| 7a: Square utilities | 0.5 |
| 7b: Polygon stack | 3–5 |
| 7c: Finder pattern detector | 1 |
| 8: Alignment locator | 1 |
| 9: Orchestrator + regression iteration | 3–4 |
| **Total** | **8–11 work-days** |

Add ~20–30% for parity-iteration time: regression numbers will
likely come in below ±2% on first end-to-end run, especially on
the harder categories (`damaged`, `bright_spots`, `glare`,
`noncompliant`). Triage by running `tools/java_reference/Baseline`
with stage-by-stage intermediate dumps and diffing — see CLAUDE.md
"Intermediate-state dumps for debugging parity failures".

## Codex-review carry-overs from earlier steps

These are public-API / design issues codex flagged that haven't been
addressed yet because they need the orchestrator to exist first.
Address during step 9:

- **RS strategy injection hook** (codex on step 4). Add a
  `std::function<bool(...)>` member to `QrCodeDecoderBits` that
  callers can override for known-prefix RS / clipped-QR fallback
  recovery pipelines. Default: the current `ReedSolomonCodes_U8`
  call.
- **Per-block decode status + RS error/erasure positions in
  `QrCode`** (codex on step 4). Add `std::vector<BlockStatus>
  blockStatus` and surface `errorLocations` from RS so multi-frame
  fusion / known-prefix recovery has the per-block diagnostic data
  CLAUDE.md "Public API design" calls out.
- **`setTransformFromLinesSquare`** (codex on step 5). Port BoofCV's
  line-correspondence DLT — needed for the unknown-version sampling
  path that the orchestrator uses before version info is decoded.
  Implementation: build a 2N×9 design matrix from N point + line
  correspondences, solve via `cv::SVDecomp`, pick the smallest
  singular value's right-singular vector.
- **`setMarkerUnknownVersion` on the grid reader** (codex on step
  5). Trivial follow-on once `setTransformFromLinesSquare` exists.

## What will NOT be ported (known final scope)

These remain out of scope for the QR-decode-only deliverable:

- `QrCodeEncoder` / `QrCodeGenerator*` (encoder side). Only the
  static `getLengthBits*` helpers from `QrCodeEncoder` are needed
  by the decoder; those are already in `qr_code_decoder_bits.cpp`.
- `MicroQrCode*` (Micro QR support).
- `QrCodeDistortedChecks`, `QrCodeDetectorPnP` (3-D pose estimation
  for AR-style applications).
- `boofcv-ip` modules unrelated to the binarization path:
  morphological ops, blob analysis, etc.
- BoofCV's lens-distortion stack on the grid reader.

These are documented as deferred in the relevant algorithm-doc
`*.md` files where they're referenced.
