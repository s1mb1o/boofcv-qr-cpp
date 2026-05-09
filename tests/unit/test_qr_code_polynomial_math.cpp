// Mirrors TestQrCodePolynomialMath.java. Upstream: BoofCV v1.3.0.

#include "boofcv_qr/qr_code.hpp"
#include "boofcv_qr/qr_code_mask_pattern.hpp"
#include "boofcv_qr/qr_code_polynomial_math.hpp"

#include <gtest/gtest.h>

using boofcv_qr::ErrorLevel;
using boofcv_qr::QrCode;
using boofcv_qr::QrCodeMaskPattern;
using boofcv_qr::QrCodePolynomialMath;

TEST(QrCodePolynomialMath, encodeVersionBits) {
    int32_t found = QrCodePolynomialMath::encodeVersionBits(7);
    int32_t expected = 0b000111110010010100;
    EXPECT_EQ(expected, found);
}

TEST(QrCodePolynomialMath, checkVersionBits) {
    for (int32_t version = 7; version <= 40; version++) {
        int32_t found = QrCodePolynomialMath::encodeVersionBits(version);
        EXPECT_TRUE(QrCodePolynomialMath::checkVersionBits(found));

        // single-bit flips should fail the check
        for (int32_t i = 0; i < 18; i++) {
            int32_t mod = found ^ (1 << i);
            EXPECT_FALSE(QrCodePolynomialMath::checkVersionBits(mod));
        }
    }
}

TEST(QrCodePolynomialMath, encodeFormatBits) {
    int32_t found = QrCodePolynomialMath::encodeFormatBits(ErrorLevel::M, 0b101);
    found ^= QrCode::FORMAT_MASK;
    int32_t expected = 0b100000011001110;
    EXPECT_EQ(expected, found);
}

TEST(QrCodePolynomialMath, checkFormatBits) {
    for (ErrorLevel error : {ErrorLevel::L, ErrorLevel::M, ErrorLevel::Q, ErrorLevel::H}) {
        int32_t found = QrCodePolynomialMath::encodeFormatBits(error, 0b101);
        EXPECT_TRUE(QrCodePolynomialMath::checkFormatBits(found));
        for (int32_t i = 0; i < 15; i++) {
            int32_t mod = found ^ (1 << i);
            EXPECT_FALSE(QrCodePolynomialMath::checkFormatBits(mod));
        }
    }
}

TEST(QrCodePolynomialMath, decodeFormatMessage) {
    QrCode qr;
    for (ErrorLevel error : {ErrorLevel::L, ErrorLevel::M, ErrorLevel::Q, ErrorLevel::H}) {
        int32_t message = QrCodePolynomialMath::encodeFormatBits(error, 0b101);
        message >>= 10;

        QrCodePolynomialMath::decodeFormatMessage(message, qr);

        EXPECT_EQ(error, qr.error);
        EXPECT_EQ(&QrCodeMaskPattern::M101(), qr.mask);
    }
}

TEST(QrCodePolynomialMath, correctDCH) {
    int32_t data = 0b10101;
    int32_t errorBits = 10;
    int32_t dataBits = 5;
    int32_t generator = QrCodePolynomialMath::FORMAT_GENERATOR;
    int32_t message = (data << errorBits) ^
                      QrCodePolynomialMath::bitPolyModulus(
                          data << errorBits, generator, errorBits + dataBits, dataBits);

    for (int32_t i = 0; i < data; i++) {
        int32_t corrupted = message ^ (1 << i);
        int32_t corrected = QrCodePolynomialMath::correctDCH(
            32, corrupted, generator, errorBits + dataBits, dataBits);
        EXPECT_EQ(data, corrected);

        for (int32_t j = 0; j < 32; j++) {
            int32_t corrupted2 = corrupted ^ (1 << j);
            corrected = QrCodePolynomialMath::correctDCH(
                32, corrupted2, generator, errorBits + dataBits, dataBits);
            EXPECT_EQ(data, corrected);
        }
    }
}

TEST(QrCodePolynomialMath, bitPolyDivide) {
    int32_t message = 0b00101 << 10;
    int32_t divisor = 0b10100110111;
    int32_t found = QrCodePolynomialMath::bitPolyModulus(message, divisor, 15, 5);
    int32_t expected = 0b0011011100;
    EXPECT_EQ(expected, found);
}
