# QR data-bit traversal — `QrCodeCodeWordLocations`

Companion to `boofcv_qr/qr_code_codeword_locations.hpp`. Step 5.

## Summary

Given a QR version, produce two things:
1. A boolean grid: which (row, col) modules are reserved for finder/timing/format/version/alignment patterns ("feature mask") versus data.
2. The ordered sequence of data-bit module coordinates the spec walks, in the zigzag pattern from ISO 18004 §6.7.4.

The codec layer sees this list as `bits[i] = (col, row)` of the i-th data bit; it pairs each with the corresponding bit of the post-correction message.

## Algorithm description

### Feature mask (`featureMaskQrCode`)

Mark these regions as `true` in a `numModules × numModules` boolean grid:

- 9×9 finder area at top-left (`(0,0)..(8,8)`).
- 9×8 finder + format-info at top-right (`(0,N-8)..(7,N-1)`).
- 8×9 finder + format-info at bottom-left (`(N-8,0)..(N-1,8)`).
- The two timing patterns (the row-6 + col-6 stripes that connect the finders).
- For version ≥ 7, two 6×3 / 3×6 version-info blocks.
- All alignment patterns at coordinates given by `VERSION_INFO[version].alignment` × itself (5×5 each), excluding the three combinations that overlap the finders.

### Bit-traversal (`computeBitLocations`)

Snake from the bottom-right module up-and-left in 2-column pairs:

```
row = N-1, col = N-1, direction = -1
while col > 0:
    if col == 6: col -= 1     # timing pattern column - skip it entirely
    if not feature[row][col]:     emit (col, row)
    if not feature[row][col-1]:   emit (col-1, row)
    row += direction
    if row out of range:
        direction *= -1
        col -= 2
        row += direction
```

Each pair `(col, col-1)` walks vertically, skipping reserved modules; when it falls off the top or bottom, it shifts left two columns and reverses direction.

## Why this approach

- **Boolean grid + ordered traversal** matches BoofCV exactly. An alternative — computing `(col, row)` on the fly for the i-th data bit — would replicate the spec's zigzag rules per query and is harder to reason about.
- **Caching per version** is an obvious optimisation (BoofCV does it via `QrCode.LOCATION_BITS[version]`). We don't cache yet; each `qrcode(version)` call rebuilds the grid. The grid for version 40 is 177×177 = 31329 booleans; build cost is microseconds. Add caching when it shows up in profiling.

## Failure modes

- Versions outside `[1, 40]` access `VERSION_INFO[version].alignment` out of bounds. We don't bounds-check — `QrCode::VERSION_INFO()` is fixed-size and the orchestrator validates version before calling.

## Cross-references

- Upstream: `QrCodeCodeWordLocations.java`. We skipped `microqr` / `featureMaskMicroQr` (no MicroQR support in this port).
- Spec: ISO/IEC 18004:2015 §6.5 (functional patterns), §6.7.4 (data placement).
- Tests: `tests/unit/test_qr_code_codeword_locations.cpp` — bit-order spec example for version 2, full-fill check for versions 2..40, data-capacity checks against §6.5.1.
