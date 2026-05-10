# Pixel-level grid sampler — `QrCodeBinaryGridReader`

Companion to `boofcv_qr/qr_code_binary_grid_reader.hpp`. Step 5.

## Summary

Wraps a `QrCodeBinaryGridToPixel` plus a `cv::Mat` source image. Given grid coordinates, reads pixel values via the homography. Three read styles:

- **`read(row, col) → float`** — single nearest-neighbour sample.
- **`readBit(row, col) → 0|1`** — five samples (centre + four ±0.2-module neighbours) thresholded against the per-QR threshold and majority-voted.
- **`readBitIntensity(row, col, intensity)`** — appends the raw 5 sample intensities to a caller-owned `std::vector<float>`, used by the post-RS threshold-tuning pass in `QrCodeDecoderImage::readBitIntensityAndThresholdDownRight`.

Java is templated on image type; QR uses `GrayU8` exclusively, and CLAUDE.md commits to `cv::Mat CV_8UC1` at the top level, so we drop the template.

## Algorithm description

### Sample-and-clamp

`sampleNearest(x, y)` truncates the floating-point pixel toward zero (matching BoofCV's `NearestNeighborPixel_U8.get(x, y)` which uses Java's `(int)x` cast — also truncation toward zero), clamps to the image bounds (matching `BorderType.EXTENDED`), and returns the byte as a `float`.

The cast `static_cast<int32_t>(x)` matches Java's `(int)` exactly. Pre-cycle-4 the C++ used `std::floor`, which differs from Java's truncation only for `x ∈ (-1, 0)` — but the subsequent `if (ix < 0) ix = 0` clamp folds both behaviours to `0` for those inputs, so the output was already byte-identical to Java. Switching to the cast is a perf win, not a parity change.

`sampleNearest` is `inline` in the header (not the .cpp) so the 5 calls per bit from `readBit` / `readBitIntensity` collapse to inline arithmetic + single pixel reads. The pixel read uses `image_.ptr<std::uint8_t>(iy)[ix]` rather than `image_.at<std::uint8_t>(iy, ix)` — `at()` does an extra row-stride multiplication and a debug-mode bounds check that we don't need (we already clamped). Profile-confirmed: on a Version-40 QR (177×177 modules, ~156k `sampleNearest` calls per scan) this cut decoder time across every category by ~25–35%.

### `readBit` — 5-sample voting

```
center = 0.5  (sample the middle of each module)
sample at (row + 0.3, col + 0.5)   → pixel01
sample at (row + 0.7, col + 0.5)   → pixel21
sample at (row + 0.5, col + 0.3)   → pixel10
sample at (row + 0.5, col + 0.7)   → pixel12
sample at (row + 0.5, col + 0.5)   → pixel00
total = sum of (pixel < threshold) across the 5 samples
return total >= 3
```

The 5-sample vote is more robust to small homography errors than a single read at the centre. The threshold itself comes from the per-finder local thresholds computed during binarization (step 6) and stored in `qr.threshCorner / threshDown / threshRight`; here we average the three.

### `readBitIntensity` — append-style 5-sample dump

Same 5 sample positions as `readBit`, but instead of voting it appends the raw `float` intensities to a caller-supplied `std::vector<float>`. The caller (`QrCodeDecoderImage::readBitIntensityAndThresholdDownRight`) accumulates `5 * locationBits.size()` floats across one full pass over the QR's locator bits, then iterates the buffer to compute the lower-right threshold (a Otsu-free fallback for the threshold tuning at the bottom-right of the QR, where the four corner `qr.threshXxx` values poorly characterise the image).

Cycle 4 replaced 5 sequential `intensity.push_back()` calls with one `intensity.resize(base + 5)` + indexed writes per call. The caller pre-`reserve()`s the full capacity so the resize is non-allocating; the win is collapsing 5 capacity-checks + 5 size-increments into 1 each. API contract is unchanged — callers still see "appends 5 floats per call".

### Lens distortion

BoofCV's reader supports an optional lens-distortion model. We don't port that — `setLensDistortion` would compose the homography with a calibration distortion model, useful for wide-FOV cameras. QR codes shot from typical sensors don't need it; the polygon-corner refinement in step 7 absorbs the residual distortion. Add via `cv::undistortPoints` if a consumer ever needs it.

## Why nearest-neighbour over bilinear

BoofCV explicitly says "use nearest neighbor to avoid shifting the location" — bilinear interpolation can drift the read off the module centre when the QR is at certain rotations. Nearest is what produces parity with the Java decode path; don't switch to bilinear without re-running the regression suite.

## Failure modes

- `setImage` rejects non-`CV_8UC1` Mats with `std::invalid_argument`.
- `image_` is a shallow `cv::Mat` reference — the underlying buffer must outlive the reader. Caller's responsibility.
- Off-image grid coords: clamp to the nearest border pixel via the EXTENDED-border emulation.

## Cross-references

- Upstream: `QrCodeBinaryGridReader.java`. We drop `setLensDistortion` (not needed for QR), drop the type template (commit to `CV_8UC1`), and inline the nearest-neighbour interpolation.
- Tests: covered transitively via `test_qr_code_binary_grid_to_pixel.cpp` (the homography is the load-bearing part); end-to-end coverage arrives at step 9.
- Used by: `QrCodeDecoderImage` (step 9 orchestrator) for both `setSquare` (single-finder fast path) and `setMarker` (full-feature path).
