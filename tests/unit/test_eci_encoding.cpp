// Mirrors boofcv-recognition/src/test/java/boofcv/alg/fiducial/qrcode/
// TestEciEncoding.java. Upstream: BoofCV v1.3.0.

#include "boofcv_qr/eci_encoding.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

using boofcv_qr::EciEncoding;

namespace {

// Convert a UTF-8 string literal to a byte vector. Source files are
// already UTF-8 so this is a straight memcpy.
std::vector<std::uint8_t> bytesOf(const std::string& s) {
    std::vector<std::uint8_t> out(s.size());
    for (std::size_t i = 0; i < s.size(); i++)
        out[i] = static_cast<std::uint8_t>(s[i]);
    return out;
}

}  // namespace

TEST(EciEncoding, isValidUTF8) {
    EXPECT_TRUE(EciEncoding::isValidUTF8(bytesOf("asdfafd")));
    EXPECT_TRUE(EciEncoding::isValidUTF8(bytesOf(u8"目asdfafd木要₹")));

    auto damaged = bytesOf(u8"a目sdfafd木");
    damaged[1] = 0b0110'0000;
    EXPECT_FALSE(EciEncoding::isValidUTF8(damaged));

    // mess up one of the extended byte characters by changing the first two bits
    damaged = bytesOf(u8"a目sdfafd木");
    damaged[2] = static_cast<std::uint8_t>(damaged[2] | 0b1100'0000);
    EXPECT_FALSE(EciEncoding::isValidUTF8(damaged));
}

// Give it a byte string of ISO-8859-1 high-bit characters and see if it
// says it's not UTF-8. The original Java test uses .getBytes(ISO_8859_1)
// to encode "asdfafdÿ¡£"; we hand-construct the matching byte vector.
TEST(EciEncoding, isValidUTF8_ISO_8859_1) {
    // ISO-8859-1 bytes for "asdfafdÿ¡£": ASCII + 0xFF, 0xA1, 0xA3.
    std::vector<std::uint8_t> bytes = {
        'a', 's', 'd', 'f', 'a', 'f', 'd', 0xFF, 0xA1, 0xA3,
    };
    EXPECT_FALSE(EciEncoding::isValidUTF8(bytes));
}

TEST(EciEncoding, getEciCharacterSet) {
    EXPECT_EQ("Cp437", EciEncoding::getEciCharacterSet(0));
    EXPECT_EQ("ISO8859_1", EciEncoding::getEciCharacterSet(1));
    EXPECT_EQ("UTF8", EciEncoding::getEciCharacterSet(26));
    EXPECT_EQ("ASCII", EciEncoding::getEciCharacterSet(170));
    EXPECT_THROW(EciEncoding::getEciCharacterSet(999), std::invalid_argument);
}
