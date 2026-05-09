# QR result struct + per-version capacity table — `QrCode`

Companion to `boofcv_qr/qr_code.hpp` / `src/decoder/qr_code.cpp`. Step 4.

## Summary

`QrCode` is the data container that travels through the decode pipeline: input fields are populated by progressively higher-level stages, and the public surface is what consumers receive at the end. This step contributes the data-only fields (`version`, `error`, `mask`, `rawbits`, `corrected`, `message`, `byteEncoding`, `failureCause`, `mode`, `totalBitErrors`, `bitsTransposed`, plus the four `thresh*` doubles), the `ErrorLevel` enum, the `BlockInfo` / `VersionInfo` types, and the populated `VERSION_INFO[1..40]` table (ISO 18004 Tables 9 + E.1).

The geometry fields (`ppCorner`, `ppDown`, `ppRight`, `bounds`, `Hinv`, `alignment[]`) are intentionally deferred — they need OpenCV's `cv::Point2d` / `cv::Matx33d`, which we add at step 6 with the binarization layer. They will land alongside the finder-pattern detector (step 7) that first writes them.

## Algorithm description

The struct is straight data plumbing — no algorithms run inside. Two pieces of logic:

1. **`VersionInfo::totalDataBytes(error)`** — for a given QR version + ECC level, returns the number of data bytes in the message area. The formula `dataCodewords * blocks + (dataCodewords + 1) * blocksB` accounts for the "two block sizes" allowed by ISO 18004 (some versions use a mix of N-byte and (N+1)-byte data blocks to fit the total codeword count). The `blocksB` count comes from `(codewords - codewords_per_block * blocks) / (codewords_per_block + 1)`.

2. **`VERSION_INFO[]` initialiser** — 40 entries, each listing total codewords, alignment-pattern positions, and per-error-level `BlockInfo` (codewords-per-block, data-codewords-per-block, blocks). Verbatim from BoofCV's `QrCode.java` static block, which itself transcribes ISO 18004 Tables 9 and E.1.

## Why this approach

- **Mutable public fields** match the Java original. Callers stitch the struct together stage-by-stage; an immutable design would require N-stage builder objects which is more ceremony than the codebase's culture supports.
- **`std::array<VersionInfo, MAX_VERSION + 1>` indexed from 1** wastes one slot for parity with Java's `VERSION_INFO[1..40]`. Worth ~150 bytes; keeps the indexing byte-identical to the Java source for grep-ability.
- **`ErrorLevel` as a strongly-typed enum class** with explicit values (L=01, M=00, Q=11, H=10) — the bit assignment is deliberately not in numeric order per ISO Table 12 (so format-info bits encode each level with a specific 2-bit pattern). Don't reorder.

## Failure modes

- **`getNumberOfDataBytes()` with `version == -1`** indexes out of bounds. Caller must have a valid version before calling.
- **`totalDataBytes(error)` with an unmapped `ErrorLevel`** throws — shouldn't happen with the populated table.
- **`reset()` does not reset the geometry fields** because they don't exist yet. Update when step 7 adds them.

## Cross-references

- Upstream: `boofcv-recognition/src/main/java/boofcv/alg/fiducial/qrcode/QrCode.java` (data fields + tables).
- Spec: ISO/IEC 18004:2015 §6.5.1 (capacity), Table 9 (block layout), Table E.1 (alignment positions).
- Tests: `tests/unit/test_qr_code.cpp`.
- Feeds into: every higher stage. `QrCodeDecoderBits` (step 4 also) is the first consumer — reads `version`/`error`, populates `corrected`/`message`/`byteEncoding`/`mode`/`failureCause`/`totalBitErrors`.
