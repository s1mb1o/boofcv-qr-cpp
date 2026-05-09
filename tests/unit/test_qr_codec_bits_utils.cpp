// Mirrors boofcv-recognition/src/test/java/boofcv/alg/fiducial/qrcode/
// TestQrCodeCodecBitsUtils.java. Upstream: BoofCV v1.3.0.

#include "boofcv_qr/eci_encoding.hpp"
#include "boofcv_qr/packed_bits.hpp"
#include "boofcv_qr/qr_codec_bits_utils.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using boofcv_qr::EciEncoding;
using boofcv_qr::PackedBits8;
using boofcv_qr::QrCodeCodecBitsUtils;

namespace {

// Mirrors checkBinaryBytes in TestQrCodeCodecBitsUtils.java.
void checkBinaryBytes(int32_t length, std::optional<std::string> forceEncoding) {
    std::vector<std::uint8_t> data(static_cast<std::size_t>(length), 0);
    for (int32_t i = 0; i < length; i++) {
        data[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
    }

    PackedBits8 packed;
    QrCodeCodecBitsUtils::encodeBytes(data, length, 8, packed);
    QrCodeCodecBitsUtils alg(forceEncoding, EciEncoding::ISO8859_1);

    alg.decodeByte(packed, 0, 8);

    if (forceEncoding.has_value())
        EXPECT_EQ(*forceEncoding, alg.selectedByteEncoding);
    else
        EXPECT_EQ(EciEncoding::ISO8859_1, alg.selectedByteEncoding);

    const std::string& found = alg.workString;
    EXPECT_EQ(static_cast<std::size_t>(length), found.size());
    for (int32_t i = 0; i < length; i++) {
        EXPECT_EQ(static_cast<char>(data[static_cast<std::size_t>(i)]),
                  found[static_cast<std::size_t>(i)]);
    }
}

}  // namespace

TEST(QrCodeCodecBitsUtils, checkAlphaNumericLookUpTable) {
    EXPECT_EQ(45u, std::string_view(QrCodeCodecBitsUtils::ALPHANUMERIC).size());
}

TEST(QrCodeCodecBitsUtils, alphanumericToValues) {
    auto found = QrCodeCodecBitsUtils::alphanumericToValues("14AE%*+-./:");
    std::vector<std::uint8_t> expected = {1, 4, 10, 14, 38, 39, 40, 41, 42, 43, 44};
    EXPECT_EQ(expected, found);
}

TEST(QrCodeCodecBitsUtils, valueToAlphanumeric) {
    std::vector<std::uint8_t> input = {1, 4, 10, 14, 38, 39, 40, 41, 42, 43, 44};
    std::string expected = "14AE%*+-./:";
    for (std::size_t i = 0; i < input.size(); i++) {
        char c = QrCodeCodecBitsUtils::valueToAlphanumeric(input[i]);
        EXPECT_EQ(expected[i], c);
    }
}

// Encode raw bytes, decode them back without hints; bytes round-trip.
TEST(QrCodeCodecBitsUtils, binaryAutoEncoding) {
    checkBinaryBytes(255, std::nullopt);
}

// With BINARY hint: bytes round-trip exactly even when they could parse
// as UTF-8 (which would otherwise modify them).
TEST(QrCodeCodecBitsUtils, binaryBytesHint) {
    checkBinaryBytes(150, std::optional<std::string>(EciEncoding::BINARY));
}

// Numeric round-trip: encode digits, decode, get the same digits back.
TEST(QrCodeCodecBitsUtils, numericRoundTrip) {
    std::vector<std::uint8_t> digits = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0};
    PackedBits8 packed;
    QrCodeCodecBitsUtils::encodeNumeric(digits,
                                        static_cast<int32_t>(digits.size()),
                                        10, packed);

    QrCodeCodecBitsUtils alg;
    int32_t consumed = alg.decodeNumeric(packed, 0, 10);
    EXPECT_GT(consumed, 0);
    EXPECT_EQ("1234567890", alg.workString);
}

// Alphanumeric round-trip.
TEST(QrCodeCodecBitsUtils, alphanumericRoundTrip) {
    std::string source = "HELLO QR 1234";
    auto values = QrCodeCodecBitsUtils::alphanumericToValues(source);
    PackedBits8 packed;
    QrCodeCodecBitsUtils::encodeAlphanumeric(values,
                                             static_cast<int32_t>(values.size()),
                                             9, packed);

    QrCodeCodecBitsUtils alg;
    int32_t consumed = alg.decodeAlphanumeric(packed, 0, 9);
    EXPECT_GT(consumed, 0);
    EXPECT_EQ(source, alg.workString);
}

// Kanji round-trip via raw Shift_JIS bytes (since C++ has no Charset).
// Use a single Shift_JIS byte pair from the Annex H range:
//   0x88 0x9F  -> "亜" in Shift_JIS (within 0x8140..0x9FFC range).
TEST(QrCodeCodecBitsUtils, kanjiRoundTripBytes) {
    std::vector<std::uint8_t> shiftJisBytes = {0x88, 0x9F};
    PackedBits8 packed;
    QrCodeCodecBitsUtils::encodeKanji(shiftJisBytes, 1, 8, packed);

    QrCodeCodecBitsUtils alg;
    int32_t consumed = alg.decodeKanji(packed, 0, 8);
    EXPECT_GT(consumed, 0);
    ASSERT_EQ(2u, alg.workString.size());
    EXPECT_EQ(static_cast<char>(0x88), alg.workString[0]);
    EXPECT_EQ(static_cast<char>(0x9F), alg.workString[1]);
}

TEST(QrCodeCodecBitsUtils, flipBits8) {
    EXPECT_EQ(0b10000000, QrCodeCodecBitsUtils::flipBits8(0b00000001));
    EXPECT_EQ(0b00000001, QrCodeCodecBitsUtils::flipBits8(0b10000000));
    EXPECT_EQ(0b11110000, QrCodeCodecBitsUtils::flipBits8(0b00001111));
}

TEST(QrCodeCodecBitsUtils, containsAlphaNumeric) {
    EXPECT_FALSE(QrCodeCodecBitsUtils::containsAlphaNumeric("0123456789"));
    EXPECT_TRUE(QrCodeCodecBitsUtils::containsAlphaNumeric("ABC"));
}

TEST(QrCodeCodecBitsUtils, containsByte) {
    EXPECT_FALSE(QrCodeCodecBitsUtils::containsByte("HELLO"));  // all alphanumeric
    EXPECT_TRUE(QrCodeCodecBitsUtils::containsByte("Hello"));   // lowercase isn't alphanumeric
}
