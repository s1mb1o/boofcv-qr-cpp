# QR mask patterns — `QrCodeMaskPattern`

Companion to `boofcv_qr/qr_code_mask_pattern.hpp`. Step 4.

## Summary

The QR encoder XORs a fixed, location-dependent mask onto every data module to break up runs of like-coloured modules (which would create false-positive finder-pattern matches and confuse the decoder). The mask is one of 8 fixed functions of `(row, col)`, identified by a 3-bit code in the format info. The decoder applies the same mask in reverse.

This module ports the 8 mask formulas verbatim from BoofCV / ISO 18004 Table 10. Each is a `QrCodeMaskPattern` subclass with a virtual `apply(row, col, bitValue)`.

## Algorithm description

The 8 patterns:

| Code | Formula | Effect |
|------|---------|--------|
| 000  | `(row + col) % 2 == 0` | checkerboard |
| 001  | `row % 2 == 0` | horizontal stripes |
| 010  | `col % 3 == 0` | vertical bars every 3rd column |
| 011  | `(row + col) % 3 == 0` | diagonal bars |
| 100  | `(row/2 + col/3) % 2 == 0` | 2x3 tiles |
| 101  | `(row*col)%2 + (row*col)%3 == 0` | scattered |
| 110  | `((row*col)%2 + (row*col)%3) % 2 == 0` | scattered (different parity) |
| 111  | `((row*col)%3 + (row+col)%2) % 2 == 0` | scattered (yet another parity) |

The Java original spells the XOR step as `bitValue ^ ((~mask) & 0x1)` for the parity-based forms (where `mask` is `0` when the cell should be flipped), preserved verbatim in the port.

## Why singletons

The mask is stateless. Java uses `static final QrCodeMaskPattern M000 = new M000();` which is a singleton via static init. Our C++ port uses static-local-in-accessor (Meyer's singleton) for thread-safe lazy init. `QrCode::mask` is a non-owning `const QrCodeMaskPattern*` that points to one of these.

## Failure modes

- **`lookupMask(int)` with code outside `[0,7]`** throws `std::runtime_error`. Format-info BCH correction guarantees the code is in range when called from the orchestrator.
- **`apply()` is undefined for negative `row`/`col`** (modulo on negatives is implementation-defined). The decoder grid sampler walks 0..moduleCount-1 so this never triggers.

## Cross-references

- Upstream: `QrCodeMaskPattern.java`.
- Spec: ISO/IEC 18004:2015 §7.8.2 (mask conditions), Table 10 (the 8 formulas).
- Tests: `tests/unit/test_qr_code_mask_pattern.cpp` (ports all 9 JUnit cases — exhaustive 0/1 check + per-mask hand-computed values).
