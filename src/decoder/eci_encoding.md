# Extended Channel Interpretation (ECI) helpers — `EciEncoding`

Companion to `boofcv_qr/eci_encoding.hpp`. Step 3 (b).

## Summary

`EciEncoding` is a small static utility with two responsibilities:

1. **`isValidUTF8(bytes)`** — heuristic that returns true if every byte sequence parses as a UTF-8 codepoint. Used by the byte-mode decoder to auto-detect UTF-8 payloads (the QR spec says BYTE mode uses ISO-8859-1 by default, but most encoders ignore that and write UTF-8 directly).
2. **`getEciCharacterSet(designator)`** — maps a 6-digit ECI designator code (per the ECI specification, e.g. 26 for UTF-8) to a Java-style charset label. The label is a *string*, not a usable codec — neither Java nor we actually decode the bytes here; we just record the announced encoding so a higher layer can re-interpret if it has the matching codec library.

Plus four constants for the most common labels: `BINARY` (BoofCV-private "no encoding"), `UTF8`, `ISO8859_1`, `JIS`.

## Algorithm description

### `isValidUTF8`

Walks the byte stream, decoding the leading byte's prefix to determine how many continuation bytes follow:

```
0xxxxxxx          1 byte  (ASCII)
110xxxxx 10xx...  2 bytes
1110xxxx 10xx... 10xx...  3 bytes
11110xxx 10xx... 10xx... 10xx...  4 bytes
```

Each continuation byte must start with `10`. Fails otherwise. Returns true only when the loop walks exactly to the end of the buffer.

### `getEciCharacterSet`

A `switch` over the designator integer returning the matching label string. Unknown designators throw `std::invalid_argument` (Java throws `IllegalArgumentException`).

The table comes from ZXing — BoofCV's comment notes that the publicly-available QR Code spec is missing this list; the authoritative ISO is paywalled. We trust ZXing's table; if a real-world QR refuses to decode because of an unknown designator, add it here.

## Why this approach

- **No actual charset decoding.** Implementing iconv-like multi-byte→Unicode conversion in-house for 30+ encodings is out of scope and not where the QR pipeline's value is. The label gets stored on the result; the consumer (pricetag-vision) can do `boost::locale::conv::to_utf<char>(rawBytes, label)` or equivalent if it cares.
- **`isValidUTF8` is a heuristic, not a parser.** It treats every byte that fits the UTF-8 byte structure as valid even if the resulting codepoint is in a private-use area or otherwise weird. Good enough for "is this UTF-8 vs Latin-1?" — the actual question we're answering.
- **No error-recovery on invalid UTF-8.** A single mid-string byte error fails the whole check. That matches Java; tests expect it (`damaged[2] |= 0xC0`).

## Failure modes and known limits

- **`isValidUTF8` accepts overlong encodings.** The strict UTF-8 RFC forbids them; we don't check. QR codes that legitimately use overlong sequences are vanishingly rare and the false-positive risk (treating Latin-1 as UTF-8) is what we're optimising against, so this is fine.
- **`getEciCharacterSet` returns labels that aren't directly usable by C++.** Names like `Cp437`, `SJIS`, `Cp1252` are Java charset identifiers. Standard C++ has no equivalent. Document for downstream consumers.
- **Designator > 999999** is theoretically out of range per the ECI spec but isn't bounded here — we just throw on unknown values regardless of magnitude.

## Tunable parameters

None. The mapping is fixed by the ECI spec.

## Integration points for downstream recovery

This module is leaf code; there are no hooks to inject. If a consumer needs to support a brand-new ECI designator, edit the table directly.

For the BYTE-mode auto-detection path: the consumer can override behaviour by setting `forceEncoding` on `QrCodeCodecBitsUtils` (constructor arg). When set, `isValidUTF8` is not consulted.

## Cross-references

- **Upstream BoofCV file** (pinned `v1.3.0`): `boofcv-recognition/src/main/java/boofcv/alg/fiducial/qrcode/EciEncoding.java`.
- **Tests:** `tests/unit/test_eci_encoding.cpp` (mirrors `TestEciEncoding.java` plus extra coverage of the designator table).
- **Specs:**
  - ECI character-set designators: AIM ECI Specification (1996), Annex A.
  - UTF-8 encoding rules: RFC 3629.
- **Used by:** `QrCodeCodecBitsUtils::decodeByte` (auto-detection) and the future `QrCodeDecoderBits::decodeEci` (designator lookup).
