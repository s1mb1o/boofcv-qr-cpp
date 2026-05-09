// Subset of boofcv.alg.fiducial.qrcode.QrCode (Mode + Failure enums).
// Upstream: BoofCV v1.3.0.
//
// The full QrCode struct is large (VERSION_INFO[] table, BlockInfo,
// ErrorLevel, plus the runtime fields populated by the orchestrator) and is
// only meaningful once Step 4 (format/version/mask) is in place. Step 3
// needs just these two enums for codec-bits machinery, so they live in
// their own header to avoid pulling everything else along.

#ifndef BOOFCV_QR_QR_MODE_HPP
#define BOOFCV_QR_QR_MODE_HPP

#include <cstdint>

namespace boofcv_qr {

// Encoding mode advertised by the 4-bit mode header. Bit values come from
// ISO/IEC 18004 §6.4.1; UNKNOWN / MIXED are BoofCV-private placeholders.
enum class Mode : int32_t {
    UNKNOWN = -1,
    MIXED = -2,
    NUMERIC = 0b0001,
    ALPHANUMERIC = 0b0010,
    BYTE = 0b0100,
    KANJI = 0b1000,
    ECI = 0b0111,
    STRUCTURE_APPENDED = 0b0011,
    FNC1_FIRST = 0b0101,
    FNC1_SECOND = 0b1001,
};

// Mode_lookup: mirrors QrCode.Mode.lookup(int). Returns UNKNOWN when the
// 4-bit pattern doesn't correspond to a known mode.
inline Mode Mode_lookup(int32_t bits) {
    switch (bits) {
        case static_cast<int32_t>(Mode::NUMERIC): return Mode::NUMERIC;
        case static_cast<int32_t>(Mode::ALPHANUMERIC): return Mode::ALPHANUMERIC;
        case static_cast<int32_t>(Mode::BYTE): return Mode::BYTE;
        case static_cast<int32_t>(Mode::KANJI): return Mode::KANJI;
        case static_cast<int32_t>(Mode::ECI): return Mode::ECI;
        case static_cast<int32_t>(Mode::STRUCTURE_APPENDED): return Mode::STRUCTURE_APPENDED;
        case static_cast<int32_t>(Mode::FNC1_FIRST): return Mode::FNC1_FIRST;
        case static_cast<int32_t>(Mode::FNC1_SECOND): return Mode::FNC1_SECOND;
        default: return Mode::UNKNOWN;
    }
}

// Where in the decode pipeline a failure occurred. Mirrors
// QrCode.Failure exactly.
enum class Failure : int32_t {
    NONE,
    FORMAT,
    VERSION,
    ALIGNMENT,
    READING_BITS,
    ERROR_CORRECTION,
    UNKNOWN_MODE,
    READING_PADDING,
    MESSAGE_OVERFLOW,
    DECODING_MESSAGE,
    KANJI_UNAVAILABLE,
    STRING_ENCODING_UNAVAILABLE,
};

}  // namespace boofcv_qr

#endif  // BOOFCV_QR_QR_MODE_HPP
