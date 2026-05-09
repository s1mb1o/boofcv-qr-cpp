# Codeword orchestrator — `QrCodeDecoderBits`

Companion to `boofcv_qr/qr_code_decoder_bits.hpp`. Step 4.

## Summary

`QrCodeDecoderBits` is the runtime-order step between RS correction and the codec utils. Given a `QrCode` populated with `version`, `error`, and `rawbits`, it:

1. Splits `rawbits` into per-block message + ECC pairs (per ISO 18004 §8.6 block layout for the version+ECC combination).
2. Applies Reed-Solomon correction to each block (uses `ReedSolomonCodes_U8`).
3. Reassembles the corrected codewords into `qr.corrected`.
4. Walks `qr.corrected` as a bit stream, reading 4-bit mode headers and dispatching to the right `QrCodeCodecBitsUtils::decode<MODE>`.

Each public step (`applyErrorCorrection`, `decodeMessage`, `decodeEci`, `checkPaddingBytes`, `getLengthBitsXxx`) is callable in isolation per CLAUDE.md "Public API design".

## Algorithm description

### `applyErrorCorrection(qr)`

The block layout for QR is:

- `numBlocksA` blocks of `wordsBlockDataA + wordsEcc` bytes each.
- `numBlocksB` blocks of `(wordsBlockDataA+1) + wordsEcc` bytes each.
- `numBlocksB = (totalCodewords - wordsBlockAllA*numBlocksA) / (wordsBlockAllA+1)`.

Codewords are interleaved across all blocks (read column by column from the per-block buffers). `decodeBlocks()` walks blocks one at a time, copying with `copyFromRawData` (stride = total blocks) into local message + ecc buffers, flips bit order (RS expects MSB-first), runs RS correct, flips back, and writes to `qr.corrected` at the appropriate offset.

### `decodeMessage(qr)`

Wraps `qr.corrected` as a `PackedBits8` and walks it:

```
loop:
    read 4 bits → mode header
    if mode == 0 (escape) or fewer than 4 bits remain: break
    dispatch on mode → decode<MODE> → consumed N bits
    track encoding overrides via qr.byteEncoding / utils.encodingEci
align to next byte boundary; check padding pattern (0x37, 0x88) ...
```

The mode-header value `0` is the "end of message" terminator per ISO §8.4.

### `decodeEci(bits, location)`

ECI segments encode a charset designator with a variable-byte prefix:
- The first byte's leading 1-bits indicate how many additional bytes carry the designator (0/1/2/... for 1/2/3-byte designators per the ECI spec).
- Strip the leading 1-bits, concatenate the remaining bits as the designator.
- Look up the corresponding charset name via `EciEncoding::getEciCharacterSet`.

### `checkPaddingBytes(qr, lengthBytes)`

After the message terminator, QR fills the rest of the data area with the alternating pattern `0x37, 0x88` (decimal 55, 136). Strict enforcement requires knowing where each block boundary sits (the pattern restarts at each block); we accept either "continue the pattern" or "restart" at any byte. Mirrors BoofCV's comment about not implementing strict block-aware checking.

### Length-of-length-field bits

Mode header is followed by a length field whose width depends on QR version:

| Mode | v1-9 | v10-26 | v27-40 |
|------|------|--------|--------|
| Numeric | 10 | 12 | 14 |
| Alphanumeric | 9 | 11 | 13 |
| Byte | 8 | 16 | 16 |
| Kanji | 8 | 10 | 12 |

These tables come from `QrCodeEncoder.java` in BoofCV; we duplicate them here because the encoder isn't otherwise needed by the decoder-only port.

## Why this approach

- **Static `getLengthBits*` helpers** keep `QrCodeEncoder` out of the dependency graph for the decoder. We need the table; we don't need the rest of the encoder.
- **`flipBits8` twice** (before + after RS) is BoofCV's convention because RS treats codewords MSB-first while the codec layer reads LSB-first. Unifying the bit ordering across stages would touch every layer; the double-flip is local and parity-preserving.
- **`PackedBits8::wrap` to view `qr.corrected`** avoids a copy; `decideMessageBits` takes a `const&`.

## Failure modes

- `applyErrorCorrection` returns `false` if any RS block exceeds correction capacity. `qr.corrected` may be partially populated.
- `decodeMessage` catches `std::exception` and sets `Failure::DECODING_MESSAGE`. Mirrors Java's `try/catch(RuntimeException)`.
- Padding-check mismatch sets `Failure::READING_PADDING`.
- Unknown mode sets `Failure::UNKNOWN_MODE`. The orchestrator must observe `failureCause` after a `false` return.
- `STRUCTURE_APPENDED` mode is dispatched but the dispatch table doesn't handle it explicitly (falls into the default `UNKNOWN_MODE` bucket — true to Java, where structured-append isn't supported either).

## Cross-references

- Upstream: `QrCodeDecoderBits.java`, plus `QrCodeEncoder.getLengthBits*` for the tables.
- Spec: ISO/IEC 18004:2015 §6.5 (block layout), §8.4 (mode dispatch), §6.4.2 (ECI), §6.4.10 (padding), §6.4.7-9 (length fields).
- Tests: `tests/unit/test_qr_code_decoder_bits.cpp` (helper-level coverage; the encoder-dependent tests from upstream — `applyErrorCorrection` round-trip, `invalidEncoding` — are deferred until step 9 where we can build a full QR end-to-end through detection).
- Depends on: `ReedSolomonCodes_U8`, `QrCodeCodecBitsUtils`, `EciEncoding`, `PackedBits8`, `QrCode` (with VERSION_INFO populated), `QrCodeMaskPattern`.
