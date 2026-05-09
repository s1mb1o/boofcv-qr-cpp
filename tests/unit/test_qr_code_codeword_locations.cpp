// Mirrors TestQrCodeCodeWordLocations.java (QR-only; MicroQR omitted —
// we don't ship MicroQR support).

#include "boofcv_qr/qr_code_codeword_locations.hpp"

#include <gtest/gtest.h>

using boofcv_qr::Point2I;
using boofcv_qr::QrCodeCodeWordLocations;

namespace {
int32_t distance2(const Point2I& a, int32_t x, int32_t y) {
    int32_t dx = a.x - x;
    int32_t dy = a.y - y;
    return dx * dx + dy * dy;
}

int32_t countFalse(const QrCodeCodeWordLocations& mask) {
    int32_t total = static_cast<int32_t>(mask.data.size());
    int32_t set = 0;
    for (bool b : mask.data) set += b ? 1 : 0;
    return total - set;
}
}  // namespace

// Spec example: select bit positions for version 2.
TEST(QrCodeCodeWordLocations, manualCheckOfBitOrderVersion2) {
    auto mask = QrCodeCodeWordLocations::qrcode(2);

    // Module D11
    EXPECT_EQ(0, distance2(mask.bits[8 * 10 + 0], 20, 11));
    EXPECT_EQ(0, distance2(mask.bits[8 * 10 + 1], 19, 11));
    EXPECT_EQ(0, distance2(mask.bits[8 * 10 + 2], 20, 10));
    EXPECT_EQ(0, distance2(mask.bits[8 * 10 + 3], 19, 10));
    EXPECT_EQ(0, distance2(mask.bits[8 * 10 + 4], 20, 9));
    EXPECT_EQ(0, distance2(mask.bits[8 * 10 + 5], 19, 9));
    EXPECT_EQ(0, distance2(mask.bits[8 * 10 + 6], 18, 9));
    EXPECT_EQ(0, distance2(mask.bits[8 * 10 + 7], 17, 9));

    // Module D14
    EXPECT_EQ(0, distance2(mask.bits[8 * 14 + 0], 16, 22));
    EXPECT_EQ(0, distance2(mask.bits[8 * 14 + 1], 15, 22));
    EXPECT_EQ(0, distance2(mask.bits[8 * 14 + 2], 16, 21));
    EXPECT_EQ(0, distance2(mask.bits[8 * 14 + 3], 15, 21));
    EXPECT_EQ(0, distance2(mask.bits[8 * 14 + 4], 15, 20));
    EXPECT_EQ(0, distance2(mask.bits[8 * 14 + 5], 15, 19));
    EXPECT_EQ(0, distance2(mask.bits[8 * 14 + 6], 15, 18));
    EXPECT_EQ(0, distance2(mask.bits[8 * 14 + 7], 15, 17));

    // Module D13
    EXPECT_EQ(0, distance2(mask.bits[8 * 13 + 0], 18, 23));
    EXPECT_EQ(0, distance2(mask.bits[8 * 13 + 1], 17, 23));
    EXPECT_EQ(0, distance2(mask.bits[8 * 13 + 2], 18, 24));
    EXPECT_EQ(0, distance2(mask.bits[8 * 13 + 3], 17, 24));
    EXPECT_EQ(0, distance2(mask.bits[8 * 13 + 4], 16, 24));
    EXPECT_EQ(0, distance2(mask.bits[8 * 13 + 5], 15, 24));
    EXPECT_EQ(0, distance2(mask.bits[8 * 13 + 6], 16, 23));
    EXPECT_EQ(0, distance2(mask.bits[8 * 13 + 7], 15, 23));
}

// Code words must fill every non-feature module exactly once.
TEST(QrCodeCodeWordLocations, codeWordsFillAll_QRCode) {
    for (int32_t version = 2; version <= 40; version++) {
        auto mask = QrCodeCodeWordLocations::qrcode(version);
        for (const Point2I& c : mask.bits) {
            mask.set(c.y, c.x, true);
        }
        EXPECT_EQ(0, countFalse(mask)) << "version=" << version;
    }
}

// Data-bit capacity per ISO 18004 Table 7.
TEST(QrCodeCodeWordLocations, dataCapability) {
    auto check = [](int32_t version, int32_t expected) {
        auto mask = QrCodeCodeWordLocations::qrcode(version);
        EXPECT_EQ(expected, mask.getTotalDataBits()) << "version=" << version;
    };
    check(1, 208);
    check(2, 359);
    check(3, 567);
    check(4, 807);
    check(5, 1079);
    check(6, 1383);
    check(7, 1568);
    check(9, 2336);
    check(20, 8683);
    check(40, 29648);
}
