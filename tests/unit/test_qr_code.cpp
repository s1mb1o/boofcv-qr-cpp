// Subset of TestQrCode.java that doesn't require QrCodeCodeWordLocations
// (deferred to step 5) or the geometry fields (deferred to step 7+).

#include "boofcv_qr/qr_code.hpp"

#include <gtest/gtest.h>

using boofcv_qr::ErrorLevel;
using boofcv_qr::QrCode;

// VersionInfo sanity: matches the cross-checks from TestQrCode.sanityVersionInfo
// that DON'T need QrCodeCodeWordLocations — the block-arithmetic identity
// `info.codewords == codewords*blocks + (codewords+1)*blocksB`.
TEST(QrCode, versionInfo_blockArithmetic) {
    for (int32_t version = 1; version <= QrCode::MAX_VERSION; version++) {
        const auto& info = QrCode::VERSION_INFO()[static_cast<std::size_t>(version)];
        for (ErrorLevel level :
             {ErrorLevel::L, ErrorLevel::M, ErrorLevel::Q, ErrorLevel::H}) {
            const auto& block = info.levels.at(level);
            EXPECT_LT(block.dataCodewords, block.codewords) << "version=" << version;

            int32_t byteBlockB = block.codewords + 1;
            int32_t byteDataB = block.dataCodewords + 1;
            int32_t countB = info.codewords - block.codewords * block.blocks;

            EXPECT_GE(countB, 0) << "version=" << version;
            if (countB > 0) {
                EXPECT_EQ(0, countB % byteBlockB) << "version=" << version;
                countB /= byteBlockB;
                EXPECT_EQ(info.codewords,
                          block.codewords * block.blocks + byteBlockB * countB);
                EXPECT_LT(byteDataB, byteBlockB);
            }
        }
    }
}

// Last alignment-pattern coordinate should monotonically increase by version.
TEST(QrCode, versionInfo_alignment_lastNumberIncreasing) {
    for (int32_t version = 3; version <= QrCode::MAX_VERSION; version++) {
        const auto& a = QrCode::VERSION_INFO()[static_cast<std::size_t>(version - 1)];
        const auto& b = QrCode::VERSION_INFO()[static_cast<std::size_t>(version)];
        EXPECT_LT(a.alignment.back(), b.alignment.back()) << "version=" << version;
    }
}

// Compare against ISO 18004 specification for select versions.
TEST(QrCode, VersionInfo_totalDataBytes) {
    EXPECT_EQ(19, QrCode::VERSION_INFO()[1].totalDataBytes(ErrorLevel::L));
    EXPECT_EQ(16, QrCode::VERSION_INFO()[1].totalDataBytes(ErrorLevel::M));
    EXPECT_EQ(13, QrCode::VERSION_INFO()[1].totalDataBytes(ErrorLevel::Q));
    EXPECT_EQ(9,  QrCode::VERSION_INFO()[1].totalDataBytes(ErrorLevel::H));

    EXPECT_EQ(242 - 48,  QrCode::VERSION_INFO()[8].totalDataBytes(ErrorLevel::L));
    EXPECT_EQ(242 - 88,  QrCode::VERSION_INFO()[8].totalDataBytes(ErrorLevel::M));
    EXPECT_EQ(242 - 132, QrCode::VERSION_INFO()[8].totalDataBytes(ErrorLevel::Q));
    EXPECT_EQ(242 - 156, QrCode::VERSION_INFO()[8].totalDataBytes(ErrorLevel::H));
}

TEST(QrCode, totalModules) {
    EXPECT_EQ(21,  QrCode::totalModules(1));
    EXPECT_EQ(45,  QrCode::totalModules(7));
    EXPECT_EQ(177, QrCode::totalModules(40));
}

TEST(QrCode, reset) {
    QrCode qr;
    qr.version = 5;
    qr.error = ErrorLevel::H;
    qr.message = "hello";
    qr.totalBitErrors = 3;
    qr.bitsTransposed = true;

    qr.reset();

    EXPECT_EQ(-1, qr.version);
    EXPECT_EQ(ErrorLevel::L, qr.error);
    EXPECT_TRUE(qr.message.empty());
    EXPECT_EQ(0, qr.totalBitErrors);
    EXPECT_FALSE(qr.bitsTransposed);
}
