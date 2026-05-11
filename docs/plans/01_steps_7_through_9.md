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
5. Tests: mirror `TestSquareNode.java`, `TestSquareEdge.java`,
   `TestSquareGraph.java`, and any `TestPositionPattern*`. (CLAUDE.md
   "Workflow per file" mandates mirroring the corresponding JUnit
   tests for every ported file.)

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
accuracy.

**Parity gating during 7b**: `tests/regression/score.py` consumes
the detector summary JSON produced by the Java baseline harness;
the equivalent C++ CLI summary doesn't exist until step 9 wires
the orchestrator. So during 7b, gate on **component parity tests
instead** — each ported file's JUnit cases must pass byte-for-byte,
and add cross-implementation tests where the same input is fed to
the Java reference (`tools/java_reference/Baseline` extended with
intermediate-state dumps per CLAUDE.md "Intermediate-state dumps
for debugging parity failures") and the C++ port, and the
intermediate outputs (contour list, polygon corner coordinates,
refined polygon coordinates) compared. Defer the per-category
±2% dataset gate to step 9 where it belongs.

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

The actual Java algorithm (do **not** describe it as a 5×5 signature
search — that path exists as `localize()` but is **commented out** in
the live code, see `localizePositionPatterns()` lines 112–148):

1. **Initialize expected positions** from `VERSION_INFO[v].alignment` —
   produces the `lookup[]` array of `Alignment` entries with
   `moduleX`/`moduleY` set, skipping the three corners that overlap
   the finder patterns.
2. **For each expected position, in row-major order**: compute an
   adjustment `(adjX, adjY)` from previously-found alignment patterns
   in the same row (left neighbour) and column (above neighbour) —
   `adj = (predicted_module - found_module)`. Carries homography drift
   forward across the grid.
3. **`centerOnSquare(a, moduleY + 0.5 + adjY, moduleX + 0.5 + adjX)`** —
   coarse centring on the 5×5 alignment square via grey-pixel scoring.
4. **`meanshift(a, moduleFound.y, moduleFound.x)`** — subpixel
   refinement via a few mean-shift iterations on the local greyscale.

The edge-scan `localize()` method is in the source but disabled
(commented out at line 139). Port it for completeness/reachability
via configuration but do **not** wire it into the default flow.

Tests: mirror `TestQrCodeAlignmentPatternLocator.java`. **Add the
geometry fields to `QrCode`** at this point (`ppCorner`, `ppDown`,
`ppRight` populated by step 7c above; `alignment[]` populated here;
`Hinv` populated by step 9 below).

Estimate: 1 work-day.

### 9. Top-level orchestrator + dataset regression

`qrcode/QrCodeDecoderImage.java` (625 LOC).

**Runtime decode order** (mirrors `QrCodeDecoderImage.decode()`
starting at line 226 of the Java source — port verbatim):

1. **`extractFormatInfo(qr)`** — read the format-info bits using a
   homography seeded from the 3 finder-pattern corners alone (no
   alignment yet, no version yet); BCH-correct via
   `QrCodePolynomialMath::correctFormatBits`; populate `qr.error`
   and `qr.mask`. Returns `false` → `Failure::FORMAT`.
2. **`extractVersionInfo(qr)`** — for QR ≥ v7, read the version-info
   bits and BCH-correct. For v1..v6, version is inferred from the
   distance between the finders. Returns `false` → `Failure::VERSION`.
3. **`alignmentLocator.process(gray, qr)`** — step-8 alignment-pattern
   localisation. Needs the rough homography from step 1's finder
   corners + the now-known version. Returns `false` →
   `Failure::ALIGNMENT`.
4. **Iterative transform + read raw data** (up to 6 attempts):
   - `gridReader.setMarker(qr)` then `getTransformGrid().addAllFeatures(qr)`
     (= 12 finder corners + alignment centres).
   - On each retry after the first, call `removeFeatureWithLargestError()`
     to drop the worst-fitting correspondence (typically a damaged
     outside finder corner). Stop if no removal reduces the error.
   - `computeTransform()` then `readRawData(qr)` (sample every data
     module via the homography → fill `qr.rawbits`).
   - `decoder.applyErrorCorrection(qr)` (steps 2 + 4 already wired).
   - On any failure, retry with one more pair removed.
5. **`decoder.decodeMessage(qr)`** — only after RS succeeds. Mode
   dispatch (steps 3 + 4 already wired). Failure here is captured in
   `qr.failureCause` but the orchestrator returns `true` because the
   QR was at least RS-correctable.

**Do not** describe this as "bit sampling → format/mask/RS/mode" —
that wording inverts the order (format/mask are extracted *before*
sampling, not after) and would steer the port away from Java parity.

Plus considerable bookkeeping for `bitsTransposed`, multi-attempt
decoding (the 6-iteration retry loop above), and the multi-stage
homography (rough → refined-with-alignment → outlier-rejected).

This is also where the **CLAUDE.md "Public API design" mandates that
have been deferred since step 4 finally land**:

- **Stage-isolation public entry points** (CLAUDE.md line 25):
  `find_finders(const cv::Mat& binary)`, `sample_bit_matrix(corners,
  version)`, `extract_raw_codewords(...)`, `rs_correct(...)`,
  `decode_message(...)`. Each must be callable directly without going
  through the top-level orchestrator. Add gtests that exercise each
  in isolation with hand-built inputs.
- **Strategy injection** (CLAUDE.md line 26): `std::function`-typed
  hooks at construction time for the RS decoder and the
  alignment-pattern detector. Add tests that verify a custom
  injection runs in place of the default (e.g. a known-prefix RS
  stub).
- **Polygon-only mode** (CLAUDE.md line 27): `detect_polygons_only()`
  that stops after the alignment-pattern stage and returns candidate
  quadrilaterals + finder triplets without paying RS / mode-decode
  cost. Add a gtest.
- **Raw codewords + erasure positions in the public result**
  (CLAUDE.md line 28): add `std::vector<uint8_t> rawCodewords`,
  `std::vector<int32_t> rsErrorLocations`, and per-block
  `BlockStatus blockStatus[]` to `QrCode`; thread them through
  `QrCodeDecoderBits::applyErrorCorrection` (which currently only
  surfaces the aggregate `totalBitErrors` — the codex review on
  step 4 flagged this).
- **`setTransformFromLinesSquare`** from step 5 finally gets used
  here (the rough-homography pre-version path inside
  `extractFormatInfo`).
- **`setMarkerUnknownVersion`** on the grid reader: trivial follow-on
  once `setTransformFromLinesSquare` exists.
- **pybind11-friendliness check** (CLAUDE.md line 32): the public API
  should compose into a Python binding without rewriting. Add a smoke
  pybind11 module under `tools/python/` that wraps the orchestrator
  and confirms `cv::Mat` round-trips through cv2's buffer protocol.

These are not "after-the-fact retrofits" — they are part of the
step-9 deliverable, with explicit test tasks per item.

**Step-9 commit boundary**: end-to-end pipeline with `cv::Mat
input → std::vector<QrCode> output`, plus the public stage-level API
listed above with isolated gtests for each entry point. New
`tools/cli/` binary that runs the full
`boofcv-qrcodes` regression
set and emits a summary JSON in the same shape as
`tools/java_reference/Baseline.java` so `tests/regression/score.py`
can compare them. CI gate per CLAUDE.md: per-category read rate
within 2% of the Java baseline in `tests/baseline.json`.

Estimate: 3–4 work-days for the orchestrator + public-API surface +
gtests, plus 2–4 days of regression-iteration to close the gap if
early runs come in below the ±2% target.

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
