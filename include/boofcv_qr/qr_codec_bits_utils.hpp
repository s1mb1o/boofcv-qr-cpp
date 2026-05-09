// Port of boofcv.alg.fiducial.qrcode.QrCodeCodecBitsUtils (BoofCV v1.3.0).
//
// Encodes / decodes QR per-mode payloads on top of PackedBits8: numeric,
// alphanumeric, byte (with ECI / UTF-8 detection), and kanji. The full
// orchestrator that splices these together (QrCodeDecoderBits) is
// deferred to step 4 since it needs version info.
//
// Algorithm description: src/decoder/qr_codec_bits_utils.md.
// IMPORTANT (charset handling): C++ has no built-in equivalent to Java's
// `new String(bytes, "Shift_JIS")`. For BYTE mode we accept the raw bytes
// and store them in `workString` as-is, returning the encoding label in
// `selectedByteEncoding` — the caller is responsible for any charset
// re-interpretation. For KANJI mode we recover Shift_JIS bytes per
// the Annex H algorithm and store those raw bytes; callers wanting Unicode
// chars must re-decode with their own charset library.

#ifndef BOOFCV_QR_QR_CODEC_BITS_UTILS_HPP
#define BOOFCV_QR_QR_CODEC_BITS_UTILS_HPP

#include "boofcv_qr/eci_encoding.hpp"
#include "boofcv_qr/packed_bits.hpp"
#include "boofcv_qr/qr_mode.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace boofcv_qr {

class QrCodeCodecBitsUtils {
public:
    // All possible values in alphanumeric mode. ISO/IEC 18004 §6.4.4.
    static constexpr const char* ALPHANUMERIC =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";

    QrCodeCodecBitsUtils() : forceEncoding(std::nullopt),
                             encodingEci(std::nullopt),
                             defaultEncoding(EciEncoding::ISO8859_1) {}

    QrCodeCodecBitsUtils(std::optional<std::string> forceEncoding_,
                         std::string defaultEncoding_)
        : forceEncoding(std::move(forceEncoding_)),
          defaultEncoding(std::move(defaultEncoding_)) {}

    // ---- Decoders. Return -1 on failure (with failureCause set). ----
    int32_t decodeNumeric(const PackedBits8& data, int32_t bitLocation,
                          int32_t lengthBits);
    int32_t decodeAlphanumeric(const PackedBits8& data, int32_t bitLocation,
                               int32_t lengthBits);
    int32_t decodeByte(const PackedBits8& data, int32_t bitLocation,
                       int32_t lengthBits);
    int32_t decodeKanji(const PackedBits8& data, int32_t bitLocation,
                        int32_t lengthBits);

    // ---- Encoders. Static, write into a PackedBits8. ----
    static void encodeNumeric(const std::vector<std::uint8_t>& numbers,
                              int32_t length, int32_t lengthBits,
                              PackedBits8& packed);
    static void encodeAlphanumeric(const std::vector<std::uint8_t>& numbers,
                                   int32_t length, int32_t lengthBits,
                                   PackedBits8& packed);
    static void encodeBytes(const std::vector<std::uint8_t>& data,
                            int32_t length, int32_t lengthBits,
                            PackedBits8& packed);
    static void encodeKanji(const std::vector<std::uint8_t>& bytes,
                            int32_t length, int32_t lengthBits,
                            PackedBits8& packed);

    // ---- Helpers ----
    static std::vector<std::uint8_t> alphanumericToValues(const std::string& data);
    static char valueToAlphanumeric(int32_t value);

    // ASCII-only check (mirrors `ISO-8859-1.canEncode(c)`-not-applicable).
    static bool isKanji(char c);
    static bool containsKanji(const std::string& message);
    static bool containsByte(const std::string& message);
    static bool containsAlphaNumeric(const std::string& message);

    // Reverse the bit order within each byte, in place.
    static std::uint8_t flipBits8(int32_t x);
    static void flipBits8(std::vector<std::uint8_t>& array, std::size_t size);

    // ---- Public state mirrors the Java surface. ----
    Failure failureCause = Failure::NONE;
    std::string workString;

    // The encoding selected when decoding a BYTE message.
    std::string selectedByteEncoding;

    // If forceEncoding is set, BYTE mode always uses it (overrides ECI).
    std::optional<std::string> forceEncoding;

    // The encoding announced by an ECI segment, if any. Public so the
    // orchestrator can populate it before decodeByte and clear it after.
    std::optional<std::string> encodingEci;

    // Used when no encoding is forced and the bytes don't pass UTF-8 validation.
    std::string defaultEncoding;

private:
    std::string selectByteEncoding(const std::vector<std::uint8_t>& rawData) const;
};

}  // namespace boofcv_qr

#endif  // BOOFCV_QR_QR_CODEC_BITS_UTILS_HPP
