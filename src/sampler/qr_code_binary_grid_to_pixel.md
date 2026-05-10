# Grid ↔ pixel homography — `QrCodeBinaryGridToPixel`

Companion to `boofcv_qr/qr_code_binary_grid_to_pixel.hpp`. Step 5.

## Summary

Holds two `cv::Matx33d` matrices: `H` maps **image pixel → grid (col, row)**, `Hinv` is its inverse. Three setup paths:

- `setTransformFromSquare(square)` — 4 image points + the canonical finder corners (0,0), (7,0), (7,7), (0,7). Used by the simplest finder-pattern-only sampling. Solved with `cv::getPerspectiveTransform`.
- `addAllFeatures(...)` + `computeTransform()` — the full set of 12 finder corners (3 patterns × 4 corners) plus optional alignment patterns. With ≥ 5 points we use `cv::findHomography(..., 0)` (DLT, no RANSAC) — same algorithm BoofCV's `GenerateHomographyLinear` runs.
- (Future, deferred to step 7) `setTransformFromLinesSquare` — direction-vector-constrained DLT for the case when only 3 finders + line directions are reliable.

After the homography is in place, callers translate single points via `imageToGrid` / `gridToImage`, both implemented as a hand-rolled 3×3 matrix-vector multiply with perspective divide (file-local `applyHomography`). Mathematically identical to `cv::perspectiveTransform` over a 1-element `cv::Mat` — the formula `(M00*x + M01*y + M02)/w, (M10*x + M11*y + M12)/w` with `w = M20*x + M21*y + M22` matches OpenCV's `perspectiveTransform_64f` arithmetic order, and operates entirely in `double`, so output is bit-identical at the IEEE-754 level. The reason for the hand-roll is profile-confirmed allocation overhead: `cv::perspectiveTransform` per single point allocates a `cv::Mat`, sets up an `NAryMatIterator`, and dispatches per-element — sample profiling on the `high_version` worst-case image showed `cv::Mat::create` / `cv::Mat::release` / allocator chains accounted for ~70% of CPU time, since `QrCodeBinaryGridReader::readBit` calls `gridToImage` 5× per module bit (≈ 156k calls per Version-40 QR scan). Per CLAUDE.md, **no `cv::warpPerspective`** is used in the sampling path — interpolation, rounding, and border behaviour would change sampled bits versus BoofCV.

## Algorithm description

The classes in BoofCV split responsibilities differently than we do:

| BoofCV | C++ |
|---|---|
| `GenerateHomographyLinear` | `cv::getPerspectiveTransform` (4-pt) / `cv::findHomography(..., 0)` (N-pt) |
| `HomographyDirectLinearTransform` | (deferred to step 7) custom SVD-based DLT for line correspondences |
| `HomographyPointOps_F64.transform(H, x, y, dst)` | inlined 3×3 mat-vec + perspective divide (was `cv::perspectiveTransform`; replaced post-9b for perf) |
| `Homography2D_F64.invert(Hinv)` | `cv::invert(H, Hinv)` |

`removeFeatureWithLargestError` is greedy outlier rejection: transform every grid coord through `Hinv`, find the pair with the largest reprojection-error (squared pixel distance), drop it if error > 4 (i.e., > 2 pixels). Used by the orchestrator to harden the homography against a single bad finder corner.

`adjustWithFeatures` adds a residual lookup: for each `gridToImage` query, find the nearest control pair and add its (image - predicted) residual to the result. A simple, local nearest-neighbour distortion correction over the homography's global fit. Disabled by default; enable by `setAdjustWithFeatures(true)` before calling `computeTransform`.

## Why this approach

- **`cv::getPerspectiveTransform`** is the OpenCV-native 4-point homography. Same DLT-based math as BoofCV's `GenerateHomographyLinear`. Numerical results agree to within float-precision noise.
- **`cv::findHomography(..., 0)`** with method `0` is plain DLT — no RANSAC, no Lagrange optimisation. BoofCV's path is the same.
- **Inlined 3×3 mat-vec + perspective divide** for single-point `imageToGrid` / `gridToImage` calls. Earlier the implementation went through `cv::perspectiveTransform` over a 1-element `cv::Mat`, but profiling revealed the per-call `cv::Mat` allocation/free was the project's #1 hot spot — since the bit sampler hits `gridToImage` 5× per module (≈ 156k calls for a Version-40 QR), this dominated decode CPU on `high_version`. The inlined math is the same `(Mx + b) / (m20 x + m21 y + m22)` that OpenCV's `perspectiveTransform_64f` runs, just without the wrapping `cv::Mat`/iterator/dispatch infrastructure. Bit-identical output (same operations, same order, all `double`). `cv::perspectiveTransform` is still the right call when the caller has a `cv::Mat` of points already; we use it for nothing else, so the change is local.

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
