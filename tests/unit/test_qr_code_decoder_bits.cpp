// Subset of TestQrCodeDecoderBits.java. The encoder-dependent tests
// (applyErrorCorrection, invalidEncoding) require QrCodeEncoder which
// hasn't been ported (encoder isn't part of this project's deliverable).
// We cover the pure helpers (alignToBytes, checkPaddingBytes, decodeEci)
// and the length-bits-per-version table.

#include "boofcv_qr/eci_encoding.hpp"
#include "boofcv_qr/packed_bits.hpp"
#include "boofcv_qr/qr_code.hpp"
#include "boofcv_qr/qr_code_decoder_bits.hpp"

#include <gtest/gtest.h>
#include <optional>

using boofcv_qr::EciEncoding;
using boofcv_qr::PackedBits8;
using boofcv_qr::QrCode;
using boofcv_qr::QrCodeDecoderBits;

TEST(QrCodeDecoderBits, alignToBytes) {
    EXPECT_EQ(0, QrCodeDecoderBits::alignToBytes(0));
    EXPECT_EQ(8, QrCodeDecoderBits::alignToBytes(1));
    EXPECT_EQ(8, QrCodeDecoderBits::alignToBytes(7));
    EXPECT_EQ(8, QrCodeDecoderBits::alignToBytes(8));
    EXPECT_EQ(16, QrCodeDecoderBits::alignToBytes(9));
}

TEST(QrCodeDecoderBits, checkPaddingBytes) {
    QrCode qr;
    QrCodeDecoderBits alg(std::optional<std::string>(EciEncoding::UTF8),
                          EciEncoding::BINARY);

    qr.corrected.assign(50, 0);

    EXPECT_FALSE(alg.checkPaddingBytes(qr, 2));

    // Fill in the 0x37 / 0x88 padding pattern starting at byte 2.
    for (std::size_t i = 2; i < qr.corrected.size(); i++) {
        if (i % 2 == 0) {
            qr.corrected[i] = 0b00110111;
        } else {
            qr.corrected[i] = 0b10001000;
        }
    }
    EXPECT_TRUE(alg.checkPaddingBytes(qr, 2));

    EXPECT_FALSE(alg.checkPaddingBytes(qr, 3));
    qr.corrected[8] = static_cast<std::uint8_t>(qr.corrected[8] ^ 0x01);
    EXPECT_FALSE(alg.checkPaddingBytes(qr, 2));
}

// Test against the example from the QR specification (ECI designator 9 ->
// ISO8859_7).
TEST(QrCodeDecoderBits, decodeEci_IsoExample) {
    PackedBits8 bits;
    bits.append(0b00001001, 8, false);

    QrCodeDecoderBits alg(std::optional<std::string>(EciEncoding::UTF8),
                          EciEncoding::BINARY);

    int32_t newBit = alg.decodeEci(bits, 0);
    ASSERT_TRUE(alg.encodingEci.has_value());
    EXPECT_EQ("ISO8859_7", *alg.encodingEci);
    EXPECT_EQ(8, newBit);
}

// applyErrorCorrection on garbage input must fail gracefully, not UB.
TEST(QrCodeDecoderBits, applyErrorCorrection_invalidVersion) {
    QrCode qr;  // version = -1 by default
    QrCodeDecoderBits alg(std::optional<std::string>(EciEncoding::UTF8),
                          EciEncoding::BINARY);
    EXPECT_FALSE(alg.applyErrorCorrection(qr));
    EXPECT_EQ(boofcv_qr::Failure::VERSION, qr.failureCause);
}

TEST(QrCodeDecoderBits, applyErrorCorrection_truncatedRawbits) {
    QrCode qr;
    qr.version = 1;
    qr.error = boofcv_qr::ErrorLevel::L;
    qr.rawbits.assign(5, 0);  // way short of the 26 codewords v1 needs
    QrCodeDecoderBits alg(std::optional<std::string>(EciEncoding::UTF8),
                          EciEncoding::BINARY);
    EXPECT_FALSE(alg.applyErrorCorrection(qr));
    EXPECT_EQ(boofcv_qr::Failure::READING_BITS, qr.failureCause);
}

// ECI prefix of all 1s would shift by negative — guard against UB.
TEST(QrCodeDecoderBits, decodeEci_allOnesPrefix) {
    PackedBits8 bits;
    bits.append(0xFF, 8, false);
    QrCodeDecoderBits alg(std::optional<std::string>(EciEncoding::UTF8),
                          EciEncoding::BINARY);
    EXPECT_THROW(alg.decodeEci(bits, 0), std::runtime_error);
}

TEST(QrCodeDecoderBits, getLengthBits) {
    // numeric: 10 / 12 / 14
    EXPECT_EQ(10, QrCodeDecoderBits::getLengthBitsNumeric(1));
    EXPECT_EQ(10, QrCodeDecoderBits::getLengthBitsNumeric(9));
    EXPECT_EQ(12, QrCodeDecoderBits::getLengthBitsNumeric(10));
    EXPECT_EQ(12, QrCodeDecoderBits::getLengthBitsNumeric(26));
    EXPECT_EQ(14, QrCodeDecoderBits::getLengthBitsNumeric(27));
    EXPECT_EQ(14, QrCodeDecoderBits::getLengthBitsNumeric(40));

    // alphanumeric: 9 / 11 / 13
    EXPECT_EQ(9,  QrCodeDecoderBits::getLengthBitsAlphanumeric(1));
    EXPECT_EQ(11, QrCodeDecoderBits::getLengthBitsAlphanumeric(10));
    EXPECT_EQ(13, QrCodeDecoderBits::getLengthBitsAlphanumeric(27));

    // bytes: 8 / 16 / 16
    EXPECT_EQ(8,  QrCodeDecoderBits::getLengthBitsBytes(1));
    EXPECT_EQ(16, QrCodeDecoderBits::getLengthBitsBytes(10));
    EXPECT_EQ(16, QrCodeDecoderBits::getLengthBitsBytes(40));

    // kanji: 8 / 10 / 12
    EXPECT_EQ(8,  QrCodeDecoderBits::getLengthBitsKanji(1));
    EXPECT_EQ(10, QrCodeDecoderBits::getLengthBitsKanji(10));
    EXPECT_EQ(12, QrCodeDecoderBits::getLengthBitsKanji(27));
}
