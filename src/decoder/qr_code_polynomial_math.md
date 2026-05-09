# Format / version BCH codecs — `QrCodePolynomialMath`

Companion to `boofcv_qr/qr_code_polynomial_math.hpp`. Step 4.

## Summary

QR codes carry two short metadata fields encoded with BCH codes:

- **Format info** (`BCH(15,5)`): 2 bits of error-correction level + 3 bits of mask pattern, encoded with generator `0b10100110111` and XOR-masked with `FORMAT_MASK = 0b101010000010010` to break runs of zeros. Stored twice, near the top-left finder + along the right/bottom finders.
- **Version info** (`BCH(18,6)`): 6 bits of version number, encoded with generator `0b1111100100101`. Only present on versions 7+.

`QrCodePolynomialMath` provides encode / check / correct for both. Correction is by brute force minimum Hamming distance over the small message space (32 / 64 candidates). Returns `-1` on ambiguity.

## Algorithm description

### `bitPolyModulus(data, generator, totalBits, dataBits)`

Schoolbook GF(2) polynomial division. Walks the data bits MSB-down; whenever the i-th data bit is 1, XORs the generator shifted by i positions. The remainder (low `errorBits = totalBits - dataBits` bits) is the BCH check.

### `correctDCH(N, observed, generator, totalBits, dataBits)`

Enumerate all `N` possible `dataBits`-wide messages, compute each one's full codeword (`message << errorBits XOR bitPolyModulus(...)`), measure popcount of XOR with `observed`. Return the one with minimum Hamming distance, or `-1` if two messages tie.

For format (`N=32`) and version (`N=64`) this is ~50–100 ns per call — easily fast enough.

### `decodeFormatMessage(message, qr)`

After `correctFormatBits` returns the 5-bit format code:
- top 2 bits → `ErrorLevel` (L=01, M=00, Q=11, H=10)
- bottom 3 bits → mask pattern code → `QrCodeMaskPattern` singleton

## Why brute force

For 5-bit and 6-bit messages the search space is tiny. The cleaner alternative (Berlekamp-Massey on a small BCH code) buys nothing: same numerical answer, more code, harder to reason about ambiguity. BoofCV's comment notes a "TODO replaced correctDCH() with a non-brute force method" but never did — and we don't either.

## Failure modes

- **Ambiguous correction returns -1.** Caller must check; the orchestrator treats this as `Failure::FORMAT` or `Failure::VERSION`.
- **Inputs with bits set above `totalBits`** are not validated. Garbage in, garbage out.

## Cross-references

- Upstream: `QrCodePolynomialMath.java`.
- Spec: ISO/IEC 18004:2015 §7.9.2 (format info), §7.10 (version info), §C.2 (BCH generators).
- Tests: `tests/unit/test_qr_code_polynomial_math.cpp` (mirrors the JUnit suite).
