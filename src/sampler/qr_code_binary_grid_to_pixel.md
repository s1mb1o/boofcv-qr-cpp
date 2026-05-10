# Grid ↔ pixel homography — `QrCodeBinaryGridToPixel`

Companion to `boofcv_qr/qr_code_binary_grid_to_pixel.hpp`. Step 5.

## Summary

Holds two `cv::Matx33d` matrices: `H` maps **image pixel → grid (col, row)**, `Hinv` is its inverse. Three setup paths:

- `setTransformFromSquare(square)` — 4 image points + the canonical finder corners (0,0), (7,0), (7,7), (0,7). Used by the simplest finder-pattern-only sampling. Solved with `cv::getPerspectiveTransform`.
- `addAllFeatures(...)` + `computeTransform()` — the full set of 12 finder corners (3 patterns × 4 corners) plus optional alignment patterns. With ≥ 5 points we build the 2N×9 DLT design matrix in place and solve via `cv::SVD::solveZ` — same algorithm BoofCV's `GenerateHomographyLinear` → `HomographyDirectLinearTransform` → `SolveNullSpaceSvd_DDRM` runs. We deliberately do **not** use `cv::findHomography(..., 0)` here: despite the `method=0` flag claiming "no RANSAC, plain DLT", OpenCV adds an iterative Levenberg–Marquardt refinement when `npoints > 4` (`cv::LMSolverImpl::run` → `cv::solve` → `cv::JacobiImpl_<double>` chain, confirmed in profile). That refinement diverges from BoofCV's pure-DLT path and was the source of the residual -0.08pp aggregate parity gap pre-cycle-3 — replacing it with the explicit `cv::SVD::solveZ` brought the aggregate decode rate from 74.32% to 74.40%, byte-matching Java.
- (Future, deferred to step 7) `setTransformFromLinesSquare` — direction-vector-constrained DLT for the case when only 3 finders + line directions are reliable.

After the homography is in place, callers translate single points via `imageToGrid` / `gridToImage`, both implemented as a hand-rolled 3×3 matrix-vector multiply with perspective divide (file-local `applyHomography`). Bit-identical to `cv::perspectiveTransform_64f` on the fast path (`|w| > FLT_EPSILON`); matches OpenCV's `(0, 0)` zero-fill fallback on the degenerate branch where `|w| ≤ FLT_EPSILON`. Note OpenCV's 64f path uses `FLT_EPSILON` as the singular-w gate (not `DBL_EPSILON`) and computes the reciprocal once before multiplying the two numerators — we mirror both. Degenerate inputs are unreachable on plausible QR finder-pattern homographies (`w` is O(1) for valid detections); the guard exists for parity with OpenCV, not because it fires in practice. The reason for the hand-roll is profile-confirmed allocation overhead: `cv::perspectiveTransform` per single point allocates a `cv::Mat`, sets up an `NAryMatIterator`, and dispatches per-element — sample profiling on the `high_version` worst-case image showed `cv::Mat::create` / `cv::Mat::release` / allocator chains accounted for ~70% of CPU time, since `QrCodeBinaryGridReader::readBit` calls `gridToImage` 5× per module bit (≈ 156k calls per Version-40 QR scan). Per CLAUDE.md, **no `cv::warpPerspective`** is used in the sampling path — interpolation, rounding, and border behaviour would change sampled bits versus BoofCV.

## Algorithm description

The classes in BoofCV split responsibilities differently than we do:

| BoofCV | C++ |
|---|---|
| `GenerateHomographyLinear` (4-pt) | `cv::getPerspectiveTransform` |
| `GenerateHomographyLinear` → `HomographyDirectLinearTransform` (N-pt) | hand-built 2N×9 design matrix + `cv::SVD::solveZ` (post-cycle-3; see "Why this approach") |
| `HomographyDirectLinearTransform` (line variant) | `setTransformFromLinesSquare`: hand-built point/line design matrix + `cv::SVDecomp` |
| `HomographyPointOps_F64.transform(H, x, y, dst)` | inlined 3×3 mat-vec + perspective divide (was `cv::perspectiveTransform`; replaced post-9b for perf) |
| `Homography2D_F64.invert(Hinv)` | `cv::invert(H, Hinv)` |
| `AdjustHomographyMatrix.adjust` (post-DLT scale + sign canon) | not needed; downstream operations are projectively invariant |

`removeFeatureWithLargestError` is greedy outlier rejection: transform every grid coord through `Hinv`, find the pair with the largest reprojection-error (squared pixel distance), drop it if error > 4 (i.e., > 2 pixels). Used by the orchestrator to harden the homography against a single bad finder corner.

`adjustWithFeatures` adds a residual lookup: for each `gridToImage` query, find the nearest control pair and add its (image - predicted) residual to the result. A simple, local nearest-neighbour distortion correction over the homography's global fit. Disabled by default; enable by `setAdjustWithFeatures(true)` before calling `computeTransform`.

## Why this approach

- **`cv::getPerspectiveTransform`** is the OpenCV-native 4-point homography. Same DLT-based math as BoofCV's `GenerateHomographyLinear` at the 4-point size. Numerical results agree to within float-precision noise.
- **Explicit `cv::SVD::solveZ` over a hand-built 2N×9 design matrix for the N>4 case.** OpenCV's `cv::findHomography(..., 0)` is *not* pure DLT despite the `method=0` argument. For `npoints > 4`, OpenCV runs an iterative Levenberg–Marquardt refinement on the DLT seed (`cv::LMSolverImpl::run` → `cv::solve` → `cv::JacobiImpl_<double>` — confirmed in profile data on `lots` and `high_version`). BoofCV's `GenerateHomographyLinear` → `HomographyDirectLinearTransform.process()` runs **only** the DLT (it constructs `HomographyDirectLinearTransform(true)` for normalisation, but the constructor flag is ignored — `shouldNormalize` is hard-coded `false` in `process()` regardless of the parameter). Cycle 3 replaced the OpenCV call with an explicit DLT: row-fill the same 2N×9 matrix Java's `addPoints2D` builds (rows 1+2 per pair: cols 3..5 = (-f.x, -f.y, -1), cols 6..8 = (s.y\*f.x, s.y\*f.y, s.y); cols 0..2 = (f.x, f.y, 1), cols 6..8 = (-s.x\*f.x, -s.x\*f.y, -s.x)), then `cv::SVD::solveZ(A, h)` returns the right-singular vector at the smallest singular value, reshaped row-major into the 3×3 H. Same algorithm BoofCV's `SolveNullSpaceSvd_DDRM` runs, no LM iterations. Effects: (a) closes the residual -0.08pp aggregate parity gap (74.32% → 74.40%, byte-matching Java); (b) eliminates the `cv::findHomography` LM hot spot — `lots` decoder time drops 1.79× (2618ms → 1460ms) since 60-QR images call `computeTransform` per detected QR.
- **No Hartley normalisation.** Mirrors BoofCV exactly. The dataset's pixel coordinates are large in magnitude (thousands of pixels), but the QR grid coordinates are small (0..N-1 with N ≤ 177), and the 12 finder corners + alignment centres are well-conditioned for the QR detection problem. Adding normalisation would deviate from BoofCV's behaviour and risk unbalancing the parity numbers we just tightened.
- **No post-DLT scale/sign canonicalisation.** BoofCV's `AdjustHomographyMatrix.adjust(...)` rescales H so the second singular value is 1 and flips sign so `p2^T·H·p1 > 0`. We skip this: the only consumers of H are `applyHomography` (perspective divide cancels any non-zero scalar multiple of H) and `cv::invert` (handles any sign / scale uniformly). The result of those operations is projectively invariant to scale and sign, so the adjustment is a no-op for our pipeline.
- **Inlined 3×3 mat-vec + perspective divide** for single-point `imageToGrid` / `gridToImage` calls. Earlier the implementation went through `cv::perspectiveTransform` over a 1-element `cv::Mat`, but profiling revealed the per-call `cv::Mat` allocation/free was the project's #1 hot spot — since the bit sampler hits `gridToImage` 5× per module (≈ 156k calls for a Version-40 QR), this dominated decode CPU on `high_version`. The inlined math is the same `(Mx + b) / (m20 x + m21 y + m22)` that OpenCV's `perspectiveTransform_64f` runs, just without the wrapping `cv::Mat`/iterator/dispatch infrastructure. Bit-identical output on the fast path (`|w| > FLT_EPSILON`); matches OpenCV's `(0, 0)` zero-fill on the degenerate branch (`|w| ≤ FLT_EPSILON`). The fast path is the only branch hit on plausible QR homographies. `cv::perspectiveTransform` is still the right call when the caller has a `cv::Mat` of points already; we use it for nothing else, so the change is local.
- **Header-inlined `imageToGrid` / `gridToImage` (cycle 5).** Pre-cycle-5 the two methods were defined in the .cpp and forwarded to a file-local `applyHomography` helper. Both layers were tagged `inline` but neither call site outside the .cpp could see through them — every call from `QrCodeBinaryGridReader::readBitIntensity` / `readBit` (5×/module) and `QrCodeAlignmentPatternLocator` was a real function call. Profile on `high_version/image029` (V40, 250 iters) showed `gridToImage` at 10.0% of decoder time on that cluster — the largest non-`findContours`, non-cycle-3-banked hot spot in the post-cycle-4 state. Cycle 5 moves the bodies into the header with the 3×3 mat-vec + perspective divide spelled out at the call site (no nested `applyHomography` indirection — the compiler doesn't always inline through both layers). The slow `adjustWithFeatures` branch of `gridToImage` (per-call nearest-pair lookup) is kept out-of-line as `applyAdjustment(row, col, pixel)`: the predicate `adjustWithFeatures && !adjustments.empty()` is dead on every decode call after cycle 3, and inlining the loop would bloat every call site with code that never runs. Same-state shootout, best of 3: `lots/image001` (60-QR, V≤7) 174.1 → 166.3 ms/iter (-4.5%), `hv/image029` (V40 candidates) 61.1 → 56.7 ms/iter (-7.2%). Aggregate decoder-only sum holds **0.00pp byte-identical** parity to Java baseline 74.4038%. Bit-identical output on the fast path: same `(Mx + b) / (m20 x + m21 y + m22)` arithmetic with the same `FLT_EPSILON` gate, no reordering of operands.

## Deferred from upstream

- **`setTransformFromLinesSquare(qr)`**: the line-correspondence DLT.
  Java uses this in `QrCodeDecoderImage` BEFORE the QR's version is known,
  to estimate the rough homography from the 3 finder patterns alone (3
  point correspondences + 4 line directions). Without it the orchestrator
  cannot do version-detection reads on a fresh QR. Lands at step 9 (the
  orchestrator that uses it); see [src/decoder/qr_code_decoder_image.md] when
  that file exists. Not exercised by the step-5 deliverable. The
  consequence today: a consumer who tries to call `setMarkerUnknownVersion`
  on the C++ reader has nothing to call yet — they must use
  `setTransformFromSquare` (single finder) or `addAllFeatures` (post-
  alignment-detection) until step 9.
- **`HomographyDirectLinearTransform` line variant**: same scope as
  `setTransformFromLinesSquare` — only useful for the line-DLT path.

These deferrals are documented in the header.

## Failure modes

- `< 4` correspondences in `computeTransform` → throws.
- `removeOutsideCornerFeatures` requires exactly the 12 finder corners in the expected order (post-`addAllFeatures`); throws if `pairs2D.size() < 12`.
- Degenerate point configurations (collinear) → `cv::findHomography` returns an empty matrix; we don't currently check, the result is silently garbage. Not exercised by the QR pipeline (the 12 corners are far from collinear).

## Cross-references

- Upstream: `QrCodeBinaryGridToPixel.java`.
- Spec: ISO/IEC 18004:2015 §6.5.1 (module coords), §6.5.2 (finder positions), §A.3 (alignment).
- Tests: `tests/unit/test_qr_code_binary_grid_to_pixel.cpp` — synthetic 4-corner setup (the upstream tests use `QrCodeEncoder` + `QrCodeGeneratorImage`, neither of which we ship).
