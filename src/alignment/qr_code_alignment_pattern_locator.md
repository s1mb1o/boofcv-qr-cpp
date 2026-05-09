# QrCodeAlignmentPatternLocator — alignment-pattern subpixel locator

Companion to `boofcv_qr/alignment/qr_code_alignment_pattern_locator.hpp`. Step 8 of the BoofCV QR port — the stage that takes a QR with finder-pattern geometry already populated and locates its (zero or more) alignment patterns to subpixel accuracy.

## Summary

For every QR version ≥ 2, the QR spec defines one or more alignment patterns at known grid coordinates inside the marker. Their job at decode time is to anchor the perspective sampling against image-distortion drift far from the three finder patterns. This stage runs **after** the orchestrator has wired up `QrCodeBinaryGridReader::setMarker(...)` with finder-pattern corner coordinates (so the homography seed is good enough to predict where each alignment pattern *should* be), then refines each prediction down to subpixel accuracy.

The output lands on `QrCode::alignment[]` (the new field added in this commit). One entry per non-corner-overlapping cell in `VERSION_INFO[v].alignment × VERSION_INFO[v].alignment`. The three corner cells `(0,0)`, `(0,n-1)`, `(n-1,0)` are skipped because the finder patterns already cover those positions.

## Algorithm

Per `QrCodeAlignmentPatternLocator.process(image, qr)`:

1. **Bind the grid reader.** `reader.setImage(image)` + `reader.setMarker(qr, …)` gives the locator a homography seed it can use to map between grid (module) coordinates and image pixels.

2. **Initialise the expected positions** (`initializePatterns(qr)`). Iterate every `(row, col)` in `VERSION_INFO[v].alignment`. The three corners `(0,0)`, `(0,n-1)`, `(n-1,0)` are pushed as `nullptr` placeholders (so the row/col indexing of `lookup[]` matches the grid layout exactly), the rest become `Alignment` entries with `moduleX = where[col]`, `moduleY = where[row]`. For v1, the table is empty and `process()` returns true immediately.

3. **Locate each alignment pattern in row-major order** (`localizePositionPatterns`). Per cell:
   - **Adjustment seed.** If a previously-found pattern exists in the same row (`lookup[(row-1) * size + col]`) or column (`lookup[row * size + col-1]`), compute `adj = predicted_module - found_module` from it. Carries homography drift forward across the grid.
   - **`centerOnSquare(a, moduleY + 0.5 + adjY, moduleX + 0.5 + adjX)`** — coarse subpixel centring. Runs ≤ 10 iterations of a 3×3 grey-image gradient walk: at each step, sample a 3×3 grid around the current guess, compute approximate `(dx, dy) = sum_right - sum_left` and `sum_down - sum_up`, and if the magnitude `r = sqrt(dx² + dy²)` is smaller than the previous best, step toward `(bestX, bestY) + step * (dx, dy) / r`; else shrink `step` by 0.75. Returns the best `(moduleFound.x, moduleFound.y)` found.
   - **`meanshift(a, moduleY, moduleX)`** — final subpixel refinement. 10 iterations of an 8×8 mean-shift on a `[-1.5, +1.5]` module window: at each pixel, weight `w = (r > 0.5) ? (v - threshold) : (threshold - v)` (clamped to ≥ -10), centroid update `(guess.x, guess.y) += step * (sumX, sumY) / total`, with `step *= 0.7` decay.

4. **Set `pixel`** for each alignment via `reader.gridToImage(moduleFound.y, moduleFound.x, pixel)` — converts the refined module-space coordinate to an image-pixel coordinate.

The Java `localize()` method (an edge-scan fallback that finds the alignment box edges via `greatestDown` / `greatestUp` 1D scans) is **commented out at line 139** of the upstream source, just before the `meanshift` call. We port it for completeness/reachability via a config flag (`setUseEdgeScan(true)`); the default flow is `centerOnSquare` + `meanshift`.

## Why this approach

- **Two-stage refinement** instead of a single optimiser: `centerOnSquare` is robust to bad seeds because the gradient-walk move can be large, but its precision is limited by the 3×3 sampling stencil. `meanshift` then takes over with fine-grained subpixel-accurate centroid steps. Mixing the two is more robust than either alone — `centerOnSquare` can land inside the wrong module without `meanshift`'s pull toward the dark stone; `meanshift` alone diverges if the seed is off by more than ~1 module.
- **Adjustment seeding from already-located neighbours** is a poor man's RANSAC: when the homography is good, `adj` ≈ 0; when it's drifting, the previously-found pattern's residual gets carried forward as a translation hint. The Java code's note "this algorithm is a bit brittle and is a good target for further improvement" applies — but this is what BoofCV ships and the parity baseline assumes.
- **`localize()` is commented out** in upstream because (per the Java comments) it has known issues with corners that aren't perfectly aligned. We follow upstream and don't wire it into the default flow, keeping it as a reachable code path for parity diagnostics.

## Failure modes

- **Bad initial homography** — the orchestrator passes finder-corner coordinates to `setMarker(qr, …)`. If those are off by more than ~1 module width, the prediction is too far off the actual alignment pattern's location and `centerOnSquare` walks to a wrong local minimum. Returns `true` with a wrong `pixel`/`moduleFound`. The caller has no signal that this happened — only the downstream homography-residual check at step 9 catches it.
- **`v == 1`** — no alignment patterns; `process()` returns `true` after setting `qr.alignment.clear()`.
- **`v < 1` or `v > 40`** — undefined; caller's responsibility to validate before calling. We don't gate on it because the upstream Java doesn't either.

## Tunable parameters (with QR defaults)

The Java source has no public knobs — every loop count, step size, and decay rate is hard-coded. We mirror that.

| Parameter | Hard-coded value | Effect |
|---|---|---|
| `centerOnSquare` iterations | 10 | Coarse subpixel walk steps. |
| `centerOnSquare` step decay | × 0.75 on bad steps | Shrink size when no progress. |
| `meanshift` iterations | 10 | Subpixel refinement steps. |
| `meanshift` step decay | × 0.7 per iter | Convergence rate. |
| `meanshift` window | 8×8 samples on `[-1.5, +1.5]` modules | Mean-shift kernel size. |
| `meanshift` weight floor | ≥ -10 | Clamp on background-pixel weight. |

## Public API per CLAUDE.md "Public API design"

- `process(const cv::Mat&, QrCode&, finder_geometry…)` returns `bool`. Result lands on the `QrCode::alignment[]` member.
- `setLensDistortion` is a no-op stub (same deferral as steps 5/7b).
- `setUseEdgeScan(bool)` toggles the commented-out Java path on; default false (matches Java).
- All internal storage is value-typed (`std::vector<Alignment*> lookup_` is internal-only; the public surface only exposes `QrCode::alignment` which is `std::vector<Alignment>`).
- No raw pointers in public signatures.

## Cross-references

- Upstream: `boofcv-recognition/src/main/java/boofcv/alg/fiducial/qrcode/QrCodeAlignmentPatternLocator.java` (311 LOC).
- Tests: `tests/unit/test_qr_code_alignment_pattern_locator.cpp`. The `simple` and `withLensDistortion` Java JUnit cases need `QrCodeEncoder` + `QrCodeGeneratorImage` (deferred — encoder side); we substitute synthetic-image-rendering equivalents using `cv::rectangle`. The static helpers (`greatestDown`, `greatestUp`, `initializePatterns`) port directly.
- Used by: step 9's `QrCodeDecoderImage` orchestrator. The locator is invoked once per candidate QR after the finder-pattern triplet is identified and a rough homography is set up.

## What changed vs Java

- **Template `<T extends ImageGray<T>>`** dropped. Commit to `cv::Mat CV_8UC1` per CLAUDE.md.
- **`QrCode.Alignment`** ported as a nested `Alignment` struct on `QrCode`. Mirrors the Java fields field-for-field plus `threshold` (which Java declares but doesn't always populate). Added in this same commit.
- **`reader.setMarker(qr)` (Java's 1-arg form)** is replaced by our existing 6-arg `setMarker(qr, ppCorner, ppRight, ppDown, alignmentCenters, alignmentGridCoords)`. Java reads `qr.ppCorner` etc. directly; our `QrCode` doesn't have those geometry fields yet (deferred to step 9). The orchestrator owns them; the locator's `process()` accepts them as separate parameters.
- **`FastArray<Alignment>` lookup table** → `std::vector<Alignment*>`. Pointers refer into `qr.alignment`, which is grown contiguously by `initializePatterns`; **the vector must not be re-grown** after `initializePatterns` returns, since pointers would dangle. The Java code respects this constraint implicitly (it never calls `qr.alignment.grow()` between `initializePatterns` and `localizePositionPatterns`); we preserve the same call ordering.
- **`LensDistortionNarrowFOV`** deferred. `setLensDistortion` is a no-op stub.
- **`localize()`** ported but reachable only via `setUseEdgeScan(true)`. Default false (matches the upstream commented-out path).
- **Helpers `greatestDown` / `greatestUp`** ported as `static` private methods. Same signatures.
