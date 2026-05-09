# QR mode encoders & decoders — `QrCodeCodecBitsUtils`

Companion to `boofcv_qr/qr_codec_bits_utils.hpp`. Step 3's main payload — the per-mode codecs that translate between QR's bit-level representations and a usable string. Plus the encoders, kept for round-trip testing and for any future encoder use case (we don't ship an encoder, but the encoder side covers ~25% of this file and gives the decoder tests a clean way to build their inputs).

## Summary

QR codes encode payloads using one of four "modes" announced by a 4-bit header:

| Mode | bits | Bits / unit | Domain |
|------|------|-------------|--------|
| Numeric | `0001` | 10 / 3 digits | `0..9` |
| Alphanumeric | `0010` | 11 / 2 chars | 45-char alphabet |
| Byte | `0100` | 8 / byte | arbitrary bytes |
| Kanji | `1000` | 13 / character | JIS X 0208 |

`QrCodeCodecBitsUtils` provides one `decode<MODE>` and one `encode<MODE>` per mode, plus byte-mode encoding-detection helpers and a few static utilities (`alphanumericToValues`, `valueToAlphanumeric`, `flipBits8`, `containsKanji`/`containsByte`/`containsAlphaNumeric`).

The orchestrator that calls these in sequence is `QrCodeDecoderBits` (step 4), which knows how to read length fields per QR version and how to handle ECI segments.

## Algorithm description

### Numeric mode (ISO 18004 §6.4.3)

Digits are grouped 3-at-a-time into a 10-bit binary number. The trailing 1 or 2 digits get 4 or 7 bits respectively.

```
length = read(lengthBits)
while length >= 3:
    chunk = read(10)            // 0..999
    valA = chunk / 100
    valB = (chunk - valA*100) / 10
    valC = chunk - valA*100 - valB*10
    append valA, valB, valC as ASCII digits
if length == 2: read 7 bits  -> 2 digits
if length == 1: read 4 bits  -> 1 digit
```

Encoder mirrors this exactly.

### Alphanumeric mode (ISO 18004 §6.4.4)

Two characters pack into 11 bits via `value = c1 * 45 + c2`. The 45-char alphabet is `0..9 A..Z space $%*+-./:`.

```
length = read(lengthBits)
while length >= 2:
    chunk = read(11)             // 0..2024
    c1 = chunk / 45
    c2 = chunk - c1*45
    append ALPHANUMERIC[c1], ALPHANUMERIC[c2]
if length == 1: read 6 bits  -> 1 char
```

`alphanumericToValues(string)` is the encoder's inverse: ASCII char to its index in the 45-char alphabet.

### Byte mode (ISO 18004 §6.4.5)

Trivially reads 8 bits per byte. The interesting work happens in `selectByteEncoding`:

```
if encodingEci is set:                 // an earlier ECI segment chose the encoding
    return encodingEci
if forceEncoding is set:               // caller-imposed override
    return forceEncoding
if isValidUTF8(rawData):               // auto-detect
    return UTF8
return defaultEncoding                 // typically ISO8859_1
```

The result is recorded in `selectedByteEncoding` and the bytes are appended to `workString`. **C++ deviation**: we don't run a Java-style charset decode. The bytes go into `workString` as-is. For ASCII / ISO-8859-1 / UTF-8 this is byte-equivalent to Java's behaviour (a `std::string` is byte-compatible with how Java represents those encodings in its String). For other encodings (`Cp1252`, `SJIS`, etc.) the consumer must re-interpret using the reported `selectedByteEncoding` label.

### Kanji mode (ISO 18004 §6.4.6, Annex H)

Each Kanji character takes 13 bits. The decoded 13-bit value maps to a Shift_JIS code point via a fixed offset based on the input range:

```
chunk = read(13)
letter = ((chunk / 0xC0) << 8) | (chunk % 0xC0)
if letter < 0x1F00:
    letter += 0x8140        // 0x8140..0x9FFC range
else:
    letter += 0xC140        // 0xE040..0xEBBF range
```

The recovered 2-byte Shift_JIS code is appended to the byte stream. **C++ deviation**: `workString` ends up holding the raw Shift_JIS bytes rather than Unicode characters. Consumers that want Unicode kanji must run their own Shift_JIS → UTF-8 conversion (e.g. `boost::locale`, `iconv`, or a small lookup-table for the limited QR range).

### Byte-stream bit ordering and `flipBits8`

The Reed-Solomon layer (step 2) operates on bytes whose bit order is reversed compared to the codec layer. `flipBits8` reverses the bit order of a single byte; the array overload does it in place. `QrCodeDecoderBits` calls `flipBits8` twice per RS block — once before correction, once after — so that the codec sees naturally-ordered bits. Keep this verbatim; the symmetry is load-bearing.

## Why this approach

- **Static encoders.** The encode side doesn't carry state; it's a pure transformation from byte arrays to bit streams. Match Java's static surface so future encoder consumers can call without instantiating.
- **Instance state for decoders.** `failureCause`, `selectedByteEncoding`, `encodingEci`, `forceEncoding`, `defaultEncoding`, `workString` all need to persist across multiple `decode*` calls within one QR (the decoder loop reads many segments). Hence the class.
- **`workString` is `std::string`, not `std::u8string` / `std::wstring`.** ASCII / ISO-8859-1 / UTF-8 all fit fine in a byte sequence; `std::wstring` would imply Unicode-aware semantics we deliberately don't have.
- **No charset library dependency.** A faithful port of Java's `Charset` machinery (which reaches into ICU territory) would be a major dependency — and the consumer is going to apply its own re-interpretation anyway. Bytes-as-bytes is the right contract.
- **`isKanji(c)` checks for non-ASCII** (`!canEncodeAscii`). This mirrors Java's idiom of using ISO-8859-1's `CharsetEncoder.canEncode` to discriminate "this character would survive a single-byte encoding" — for ISO-8859-1 that's basically `c <= 0x7F` plus the high-bit Latin-1 range. We narrow to ASCII because our `workString` is byte-stream — we can't actually distinguish "Latin-1 character" from "first byte of UTF-8 character" without knowing the encoding. ASCII is the safe lower bound: anything above 0x7F gets flagged as kanji-candidate. The encoder side uses `containsKanji` to decide between BYTE and KANJI mode; over-flagging means we'd choose Kanji when Byte would have worked, which is suboptimal but not wrong. Encoders aren't shipped in our deliverable so the worst case is theoretical.

## Failure modes and known limits

- **`MESSAGE_OVERFLOW`** failure when the bit-stream ends mid-payload. Set `failureCause` and return `-1`. Caller must check.
- **`STRING_ENCODING_UNAVAILABLE`** is wired up in the Java original; we never set it because we don't run charset conversion. If we add a real Shift_JIS decoder later, that path becomes reachable.
- **Kanji range gaps** — codes outside `0x8140..0x9FFC` and `0xE040..0xEBBF` (per Annex H). The `decodeKanji` algorithm maps every 13-bit input to one of those ranges by construction, so this is more of a generator-side concern.
- **`encodeKanji` throws on out-of-range input bytes** (`std::invalid_argument`). Symmetric to the decoder's range partition.
- **`alphanumericToValues` throws on characters outside the 45-char alphabet.** The encoder relies on the caller having checked via `containsAlphaNumeric`.
- **`workString` accumulates across calls.** The decoder caller must clear it before each QR (`workString.clear()`); we don't auto-clear because some consumers want to concatenate segments.

## Tunable parameters

Constructor takes `(forceEncoding, defaultEncoding)`:

| Parameter | Typical | Effect |
|-----------|---------|--------|
| `forceEncoding` | `std::nullopt` | When set, BYTE auto-detection is bypassed. Use to force `BINARY` (raw byte mode), `UTF8`, or any consumer-known encoding. |
| `defaultEncoding` | `EciEncoding::ISO8859_1` | Fallback when bytes don't pass UTF-8 validation and no ECI was announced. The QR spec says ISO-8859-1; some retailers use Cp1252. |

The `encodingEci` field is mutated by the orchestrator when an ECI segment is seen — public for that reason.

## Integration points for downstream recovery

The four `decode<MODE>` methods are the integration points named by CLAUDE.md "Public API design — Stages must be callable in isolation." A consumer who has corrected codewords and wants to extract just the alphanumeric portion can call `decodeAlphanumeric` directly without instantiating the full pipeline.

For known-prefix recovery: caller pre-loads `workString` with the known-prefix string, then calls the appropriate decoder for the rest. This is exactly the use case that motivated the public mutable surface.

For multi-frame fusion: caller builds a single corrected `PackedBits8` from multiple frames' codewords (perhaps majority-voted byte-by-byte), then runs the codec utils once.

## Cross-references

- **Upstream BoofCV file** (pinned `v1.3.0`): `boofcv-recognition/src/main/java/boofcv/alg/fiducial/qrcode/QrCodeCodecBitsUtils.java`.
- **Tests:** `tests/unit/test_qr_codec_bits_utils.cpp` (mirrors `TestQrCodeCodecBitsUtils.java` plus round-trip tests for each mode).
- **Spec:** ISO/IEC 18004:2015 §6.4 (data encoding modes, including the alphanumeric table in §6.4.4 Table 5 and the kanji algorithm in §6.4.6 / Annex H).
- **Depends on:** `PackedBits8` (bit-level I/O), `EciEncoding` (auto-detection), `Mode`/`Failure` enums (state).
- **Feeds into:** `QrCodeDecoderBits` (step 4 — orchestrator that splits codewords by mode header and dispatches to the right `decode<MODE>`).
