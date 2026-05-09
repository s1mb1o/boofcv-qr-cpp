// Port of boofcv.alg.fiducial.qrcode.QrCodeCodecBitsUtils. Verbatim per
// CLAUDE.md; loop structure, variable names, and inline comments mirror
// the Java source. Charset handling differs (no Java Charset machinery in
// C++) — see the header for the convention.
//
// Algorithm description: src/decoder/qr_codec_bits_utils.md.

#include "boofcv_qr/qr_codec_bits_utils.hpp"

#include <cstring>
#include <stdexcept>
#include <string_view>

namespace boofcv_qr {

namespace {

constexpr std::string_view kAlphanumeric =
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";

// True if `c` is in [0x00, 0x7F]. Mirrors Java's
// `ISO-8859-1` charset encoder check, which BoofCV uses to identify
// non-ASCII characters as "kanji-ish" candidates for kanji segmentation.
bool canEncodeAscii(char c) {
    return static_cast<std::uint8_t>(c) <= 0x7F;
}

}  // namespace

int32_t QrCodeCodecBitsUtils::decodeNumeric(const PackedBits8& data,
                                            int32_t bitLocation,
                                            int32_t lengthBits) {
    int32_t length = data.read(bitLocation, lengthBits, true);
    bitLocation += lengthBits;

    while (length >= 3) {
        if (data.size < bitLocation + 10) {
            failureCause = Failure::MESSAGE_OVERFLOW;
            return -1;
        }
        int32_t chunk = data.read(bitLocation, 10, true);
        bitLocation += 10;

        int32_t valA = chunk / 100;
        int32_t valB = (chunk - valA * 100) / 10;
        int32_t valC = chunk - valA * 100 - valB * 10;

        workString.push_back(static_cast<char>(valA + '0'));
        workString.push_back(static_cast<char>(valB + '0'));
        workString.push_back(static_cast<char>(valC + '0'));

        length -= 3;
    }

    if (length == 2) {
        if (data.size < bitLocation + 7) {
            failureCause = Failure::MESSAGE_OVERFLOW;
            return -1;
        }
        int32_t chunk = data.read(bitLocation, 7, true);
        bitLocation += 7;

        int32_t valA = chunk / 10;
        int32_t valB = chunk - valA * 10;
        workString.push_back(static_cast<char>(valA + '0'));
        workString.push_back(static_cast<char>(valB + '0'));
    } else if (length == 1) {
        if (data.size < bitLocation + 4) {
            failureCause = Failure::MESSAGE_OVERFLOW;
            return -1;
        }
        int32_t valA = data.read(bitLocation, 4, true);
        bitLocation += 4;
        workString.push_back(static_cast<char>(valA + '0'));
    }
    return bitLocation;
}

int32_t QrCodeCodecBitsUtils::decodeAlphanumeric(const PackedBits8& data,
                                                 int32_t bitLocation,
                                                 int32_t lengthBits) {
    int32_t length = data.read(bitLocation, lengthBits, true);
    bitLocation += lengthBits;

    while (length >= 2) {
        if (data.size < bitLocation + 11) {
            failureCause = Failure::MESSAGE_OVERFLOW;
            return -1;
        }
        int32_t chunk = data.read(bitLocation, 11, true);
        bitLocation += 11;

        int32_t valA = chunk / 45;
        int32_t valB = chunk - valA * 45;

        workString.push_back(valueToAlphanumeric(valA));
        workString.push_back(valueToAlphanumeric(valB));
        length -= 2;
    }

    if (length == 1) {
        if (data.size < bitLocation + 6) {
            failureCause = Failure::MESSAGE_OVERFLOW;
            return -1;
        }
        int32_t valA = data.read(bitLocation, 6, true);
        bitLocation += 6;
        workString.push_back(valueToAlphanumeric(valA));
    }
    return bitLocation;
}

int32_t QrCodeCodecBitsUtils::decodeByte(const PackedBits8& data,
                                         int32_t bitLocation,
                                         int32_t lengthBits) {
    int32_t length = data.read(bitLocation, lengthBits, true);
    bitLocation += lengthBits;

    if (length * 8 > data.size - bitLocation) {
        failureCause = Failure::MESSAGE_OVERFLOW;
        return -1;
    }

    std::vector<std::uint8_t> rawdata(static_cast<std::size_t>(length), 0);
    for (int32_t i = 0; i < length; i++) {
        rawdata[static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>(data.read(bitLocation, 8, true));
        bitLocation += 8;
    }

    selectedByteEncoding = selectByteEncoding(rawdata);

    // C++ deviation: we don't run a Java-Charset decode here. The bytes
    // are appended verbatim to workString. This is byte-equivalent to
    // Java's `new String(rawdata, encoding)` for ASCII / ISO-8859-1 /
    // UTF-8 (where Java's String stores codepoints derived from the same
    // bytes). For other encodings the consumer should re-interpret using
    // `selectedByteEncoding`.
    workString.reserve(workString.size() + rawdata.size());
    for (std::size_t i = 0; i < rawdata.size(); i++) {
        workString.push_back(static_cast<char>(rawdata[i]));
    }
    return bitLocation;
}

std::string QrCodeCodecBitsUtils::selectByteEncoding(
    const std::vector<std::uint8_t>& rawData) const {
    if (encodingEci.has_value())
        return *encodingEci;

    if (forceEncoding.has_value())
        return *forceEncoding;

    if (EciEncoding::isValidUTF8(rawData))
        return EciEncoding::UTF8;

    return defaultEncoding;
}

int32_t QrCodeCodecBitsUtils::decodeKanji(const PackedBits8& data,
                                          int32_t bitLocation,
                                          int32_t lengthBits) {
    int32_t length = data.read(bitLocation, lengthBits, true);
    bitLocation += lengthBits;

    std::vector<std::uint8_t> rawdata(static_cast<std::size_t>(length) * 2, 0);

    for (int32_t i = 0; i < length; i++) {
        if (data.size < bitLocation + 13) {
            failureCause = Failure::MESSAGE_OVERFLOW;
            return -1;
        }
        int32_t letter = data.read(bitLocation, 13, true);
        bitLocation += 13;

        letter = ((letter / 0x0C0) << 8) | (letter % 0x0C0);

        if (letter < 0x01F00) {
            // In the 0x8140 to 0x9FFC range
            letter += 0x08140;
        } else {
            // In the 0xE040 to 0xEBBF range
            letter += 0x0C140;
        }
        rawdata[static_cast<std::size_t>(i) * 2] =
            static_cast<std::uint8_t>(letter >> 8);
        rawdata[static_cast<std::size_t>(i) * 2 + 1] =
            static_cast<std::uint8_t>(letter);
    }

    // C++ deviation: store the recovered Shift_JIS bytes as-is in
    // workString. Java does `new String(rawdata, "Shift_JIS")` to convert
    // to UTF-16; we have no equivalent. Caller can re-decode if needed.
    workString.reserve(workString.size() + rawdata.size());
    for (std::size_t i = 0; i < rawdata.size(); i++) {
        workString.push_back(static_cast<char>(rawdata[i]));
    }
    return bitLocation;
}

bool QrCodeCodecBitsUtils::isKanji(char c) {
    return !canEncodeAscii(c);
}

bool QrCodeCodecBitsUtils::containsKanji(const std::string& message) {
    for (std::size_t i = 0; i < message.size(); i++) {
        if (isKanji(message[i]))
            return true;
    }
    return false;
}

bool QrCodeCodecBitsUtils::containsByte(const std::string& message) {
    for (std::size_t i = 0; i < message.size(); i++) {
        if (kAlphanumeric.find(message[i]) == std::string_view::npos)
            return true;
    }
    return false;
}

bool QrCodeCodecBitsUtils::containsAlphaNumeric(const std::string& message) {
    for (std::size_t i = 0; i < message.size(); i++) {
        int32_t c = static_cast<int32_t>(message[i]) - '0';
        if (c < 0 || c > 9)
            return true;
    }
    return false;
}

std::vector<std::uint8_t> QrCodeCodecBitsUtils::alphanumericToValues(
    const std::string& data) {
    std::vector<std::uint8_t> output(data.size(), 0);
    for (std::size_t i = 0; i < data.size(); i++) {
        char c = data[i];
        auto value = kAlphanumeric.find(c);
        if (value == std::string_view::npos)
            throw std::invalid_argument(
                std::string("Unsupported character '") + c + "' = " +
                std::to_string(static_cast<int32_t>(c)));
        output[i] = static_cast<std::uint8_t>(value);
    }
    return output;
}

char QrCodeCodecBitsUtils::valueToAlphanumeric(int32_t value) {
    if (value < 0 || static_cast<std::size_t>(value) >= kAlphanumeric.size())
        throw std::runtime_error(
            "Alphanumeric: Value out of range. value=" + std::to_string(value));
    return kAlphanumeric[static_cast<std::size_t>(value)];
}

std::uint8_t QrCodeCodecBitsUtils::flipBits8(int32_t x) {
    int32_t b = 0;
    for (int32_t i = 0; i < 8; i++) {
        b <<= 1;
        b |= (x & 1);
        x >>= 1;
    }
    return static_cast<std::uint8_t>(b);
}

void QrCodeCodecBitsUtils::flipBits8(std::vector<std::uint8_t>& array,
                                     std::size_t size) {
    for (std::size_t j = 0; j < size; j++) {
        array[j] = flipBits8(static_cast<int32_t>(array[j]));
    }
}

void QrCodeCodecBitsUtils::encodeNumeric(
    const std::vector<std::uint8_t>& numbers, int32_t length,
    int32_t lengthBits, PackedBits8& packed) {
    packed.append(length, lengthBits, false);

    int32_t index = 0;
    while (length - index >= 3) {
        int32_t value = numbers[static_cast<std::size_t>(index)] * 100 +
                        numbers[static_cast<std::size_t>(index + 1)] * 10 +
                        numbers[static_cast<std::size_t>(index + 2)];
        packed.append(value, 10, false);
        index += 3;
    }
    if (length - index == 2) {
        int32_t value = numbers[static_cast<std::size_t>(index)] * 10 +
                        numbers[static_cast<std::size_t>(index + 1)];
        packed.append(value, 7, false);
    } else if (length - index == 1) {
        int32_t value = numbers[static_cast<std::size_t>(index)];
        packed.append(value, 4, false);
    }
}

void QrCodeCodecBitsUtils::encodeAlphanumeric(
    const std::vector<std::uint8_t>& numbers, int32_t length,
    int32_t lengthBits, PackedBits8& packed) {
    packed.append(length, lengthBits, false);

    int32_t index = 0;
    while (length - index >= 2) {
        int32_t value = numbers[static_cast<std::size_t>(index)] * 45 +
                        numbers[static_cast<std::size_t>(index + 1)];
        packed.append(value, 11, false);
        index += 2;
    }
    if (length - index == 1) {
        int32_t value = numbers[static_cast<std::size_t>(index)];
        packed.append(value, 6, false);
    }
}

void QrCodeCodecBitsUtils::encodeBytes(const std::vector<std::uint8_t>& data,
                                       int32_t length, int32_t lengthBits,
                                       PackedBits8& packed) {
    packed.append(length, lengthBits, false);
    for (int32_t i = 0; i < length; i++) {
        packed.append(static_cast<int32_t>(
                          data[static_cast<std::size_t>(i)]),
                      8, false);
    }
}

void QrCodeCodecBitsUtils::encodeKanji(const std::vector<std::uint8_t>& bytes,
                                       int32_t length, int32_t lengthBits,
                                       PackedBits8& packed) {
    packed.append(length, lengthBits, false);

    for (int32_t i = 0; i < length * 2; i += 2) {
        int32_t byte1 = bytes[static_cast<std::size_t>(i)];
        int32_t byte2 = bytes[static_cast<std::size_t>(i + 1)];
        int32_t code = (byte1 << 8) | byte2;
        int32_t adjusted;
        if (code >= 0x8140 && code <= 0x9ffc) {
            adjusted = code - 0x8140;
        } else if (code >= 0xe040 && code <= 0xebbf) {
            adjusted = code - 0xc140;
        } else {
            throw std::invalid_argument(
                "Invalid byte sequence. At " + std::to_string(i / 2));
        }
        int32_t encoded = ((adjusted >> 8) * 0xc0) + (adjusted & 0xff);
        packed.append(encoded, 13, false);
    }
}

}  // namespace boofcv_qr
