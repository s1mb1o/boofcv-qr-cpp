// Port of boofcv.alg.fiducial.qrcode.EciEncoding. Verbatim per
// CLAUDE.md; comments preserved from the Java source.
//
// Algorithm description: src/decoder/eci_encoding.md.

#include "boofcv_qr/eci_encoding.hpp"

#include <stdexcept>

namespace boofcv_qr {

bool EciEncoding::isValidUTF8(const std::vector<std::uint8_t>& message) {
    std::size_t index = 0;
    while (index < message.size()) {
        // determine the number of bytes per letter
        int32_t letterSize;
        int32_t value = message[index];
        if (value >> 3 == 0b1111'0) {
            letterSize = 4;
        } else if (value >> 4 == 0b1110) {
            letterSize = 3;
        } else if (value >> 5 == 0b110) {
            letterSize = 2;
        } else if ((value >> 7) == 0) {
            letterSize = 1;
        } else {
            return false;
        }
        // all multibyte UTF-8 characters start with 0b10xx_xxxx
        for (int32_t i = 1; i < letterSize; i++) {
            if (index + static_cast<std::size_t>(i) >= message.size())
                return false;
            if ((message[index + static_cast<std::size_t>(i)] & 0xFF) >> 6 !=
                0b10)
                return false;
        }
        index += static_cast<std::size_t>(letterSize);
        if (index == message.size())
            return true;
    }
    return false;
}

std::string EciEncoding::getEciCharacterSet(int32_t designator) {
    // Table from ZXing — see EciEncoding.java in BoofCV for context.
    switch (designator) {
        case 0:
        case 2: return "Cp437";
        case 1:
        case 3: return "ISO8859_1";
        case 4: return "ISO8859_2";
        case 5: return "ISO8859_3";
        case 6: return "ISO8859_4";
        case 7: return "ISO8859_5";
        case 8: return "ISO8859_6";
        case 9: return "ISO8859_7";
        case 10: return "ISO8859_8";
        case 11: return "ISO8859_9";
        case 12: return "ISO8859_10";
        case 13: return "ISO8859_11";
        case 14: return "ISO8859_12";
        case 15: return "ISO8859_13";
        case 16: return "ISO8859_14";
        case 17: return "ISO8859_15";
        case 18: return "ISO8859_16";
        case 20: return "SJIS";
        case 21: return "Cp1250";
        case 22: return "Cp1251";
        case 23: return "Cp1252";
        case 24: return "Cp1256";
        case 25: return "UnicodeBigUnmarked";
        case 26: return "UTF8";
        case 27:
        case 170: return "ASCII";
        case 28: return "Big5";
        case 29: return "GB18030";
        case 30: return "EUC_KR";
        default:
            throw std::invalid_argument(
                "Unknown ECI designator " + std::to_string(designator));
    }
}

}  // namespace boofcv_qr
