# Grid ↔ pixel homography — `QrCodeBinaryGridToPixel`

Companion to `boofcv_qr/qr_code_binary_grid_to_pixel.hpp`. Step 5.

## Summary

Holds two `cv::Matx33d` matrices: `H` maps **image pixel → grid (col, row)**, `Hinv` is its inverse. Three setup paths:

- `setTransformFromSquare(square)` — 4 image points + the canonical finder corners (0,0), (7,0), (7,7), (0,7). Used by the simplest finder-pattern-only sampling. Solved with `cv::getPerspectiveTransform`.
- `addAllFeatures(...)` + `computeTransform()` — the full set of 12 finder corners (3 patterns × 4 corners) plus optional alignment patterns. With ≥ 5 points we use `cv::findHomography(..., 0)` (DLT, no RANSAC) — same algorithm BoofCV's `GenerateHomographyLinear` runs.
- (Future, deferred to step 7) `setTransformFromLinesSquare` — direction-vector-constrained DLT for the case when only 3 finders + line directions are reliable.

After the homography is in place, callers translate single points via `imageToGrid` / `gridToImage`, both implemented as `cv::perspectiveTransform` over a 1-element `cv::Mat`. Per CLAUDE.md, **no `cv::warpPerspective`** is used in the sampling path — interpolation, rounding, and border behaviour would change sampled bits versus BoofCV.

## Algorithm description

The classes in BoofCV split responsibilities differently than we do:

| BoofCV | C++ |
|---|---|
| `GenerateHomographyLinear` | `cv::getPerspectiveTransform` (4-pt) / `cv::findHomography(..., 0)` (N-pt) |
| `HomographyDirectLinearTransform` | (deferred to step 7) custom SVD-based DLT for line correspondences |
| `HomographyPointOps_F64.transform(H, x, y, dst)` | `cv::perspectiveTransform` |
| `Homography2D_F64.invert(Hinv)` | `cv::invert(H, Hinv)` |

`removeFeatureWithLargestError` is greedy outlier rejection: transform every grid coord through `Hinv`, find the pair with the largest reprojection-error (squared pixel distance), drop it if error > 4 (i.e., > 2 pixels). Used by the orchestrator to harden the homography against a single bad finder corner.

`adjustWithFeatures` adds a residual lookup: for each `gridToImage` query, find the nearest control pair and add its (image - predicted) residual to the result. A simple, local nearest-neighbour distortion correction over the homography's global fit. Disabled by default; enable by `setAdjustWithFeatures(true)` before calling `computeTransform`.

## Why this approach

- **`cv::getPerspectiveTransform`** is the OpenCV-native 4-point homography. Same DLT-based math as BoofCV's `GenerateHomographyLinear`. Numerical results agree to within float-precision noise.
- **`cv::findHomography(..., 0)`** with method `0` is plain DLT — no RANSAC, no Lagrange optimisation. BoofCV's path is the same.
- **`cv::perspectiveTransform` over a 1-point Mat** instead of writing the divide-by-w by hand keeps the maths in OpenCV; trade-off is some Mat construction overhead per call. Acceptable for the call frequency QR runs at.

## Deferred from upstream

- **`setTransformFromLinesSquare(qr)`**: the line-correspondence DLT. Used by the orchestrator when finder patterns are detected but the alignment pattern hasn't been refined yet. Will land in step 7 alongside the finder-pattern detector that supplies its inputs. Not exercised by the step-5 deliverable.
- **`HomographyDirectLinearTransform` line variant**: same — only used by `setTransformFromLinesSquare`.

These deferrals are documented in the header.

## Failure modes

- `< 4` correspondences in `computeTransform` → throws.
- `removeOutsideCornerFeatures` requires exactly the 12 finder corners in the expected order (post-`addAllFeatures`); throws if `pairs2D.size() < 12`.
- Degenerate point configurations (collinear) → `cv::findHomography` returns an empty matrix; we don't currently check, the result is silently garbage. Not exercised by the QR pipeline (the 12 corners are far from collinear).

## Cross-references

- Upstream: `QrCodeBinaryGridToPixel.java`.
- Spec: ISO/IEC 18004:2015 §6.5.1 (module coords), §6.5.2 (finder positions), §A.3 (alignment).
- Tests: `tests/unit/test_qr_code_binary_grid_to_pixel.cpp` — synthetic 4-corner setup (the upstream tests use `QrCodeEncoder` + `QrCodeGeneratorImage`, neither of which we ship).
