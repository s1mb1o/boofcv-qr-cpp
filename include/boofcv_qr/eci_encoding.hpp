// Port of boofcv.alg.fiducial.qrcode.EciEncoding (BoofCV v1.3.0).
// Algorithm description: src/decoder/eci_encoding.md.

#ifndef BOOFCV_QR_ECI_ENCODING_HPP
#define BOOFCV_QR_ECI_ENCODING_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace boofcv_qr {

class EciEncoding {
public:
    // BINARY is BoofCV-private and means "no encoding done; treat the
    // bytes opaquely." Use for ASCII-pass-through paths.
    static constexpr const char* BINARY = "binary";

    // Standard QR Code string encodings.
    static constexpr const char* UTF8 = "UTF8";
    static constexpr const char* ISO8859_1 = "ISO8859_1";
    static constexpr const char* JIS = "JIS";

    // True if every byte sequence in `message` parses as a valid UTF-8
    // codepoint chain. Mirrors EciEncoding.isValidUTF8.
    static bool isValidUTF8(const std::vector<std::uint8_t>& message);

    // Map an ECI designator (per ECI spec / ZXing's table) to a Java-style
    // charset name. We don't actually do charset conversion in C++ — the
    // returned string is a label the caller can hand to a higher layer
    // that does. Throws std::invalid_argument on unknown designators.
    static std::string getEciCharacterSet(int32_t designator);

private:
    EciEncoding() = delete;
};

}  // namespace boofcv_qr

#endif  // BOOFCV_QR_ECI_ENCODING_HPP
