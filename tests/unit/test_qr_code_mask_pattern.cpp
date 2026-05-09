// Mirrors TestQrCodeMaskPattern.java. Upstream: BoofCV v1.3.0.

#include "boofcv_qr/qr_code_mask_pattern.hpp"

#include <gtest/gtest.h>

using boofcv_qr::QrCodeMaskPattern;

namespace {

void ensureOutputIs0or1(const QrCodeMaskPattern& pattern) {
    int32_t found;
    for (int32_t i = 0; i < 30; i++) {
        for (int32_t j = 0; j < 30; j++) {
            found = pattern.apply(i, j, 1);
            EXPECT_TRUE(found == 0 || found == 1);
            found = pattern.apply(i, j, 0);
            EXPECT_TRUE(found == 0 || found == 1);
        }
    }
}

}  // namespace

TEST(QrCodeMaskPattern, ensureOutputIs0or1) {
    ensureOutputIs0or1(QrCodeMaskPattern::M000());
    ensureOutputIs0or1(QrCodeMaskPattern::M001());
    ensureOutputIs0or1(QrCodeMaskPattern::M010());
    ensureOutputIs0or1(QrCodeMaskPattern::M011());
    ensureOutputIs0or1(QrCodeMaskPattern::M100());
    ensureOutputIs0or1(QrCodeMaskPattern::M101());
    ensureOutputIs0or1(QrCodeMaskPattern::M110());
    ensureOutputIs0or1(QrCodeMaskPattern::M111());
}

TEST(QrCodeMaskPattern, checkM000) {
    const auto& M = QrCodeMaskPattern::M000();
    EXPECT_EQ(1, M.apply(0, 0, 0));
    EXPECT_EQ(0, M.apply(0, 1, 0));
    EXPECT_EQ(0, M.apply(1, 0, 0));
    EXPECT_EQ(1, M.apply(1, 1, 0));
    EXPECT_EQ(0, M.apply(0, 0, 1));
}

TEST(QrCodeMaskPattern, checkM001) {
    const auto& M = QrCodeMaskPattern::M001();
    EXPECT_EQ(1, M.apply(0, 0, 0));
    EXPECT_EQ(1, M.apply(0, 1, 0));
    EXPECT_EQ(0, M.apply(1, 0, 0));
    EXPECT_EQ(0, M.apply(1, 1, 0));
    EXPECT_EQ(0, M.apply(0, 0, 1));
}

TEST(QrCodeMaskPattern, checkM010) {
    const auto& M = QrCodeMaskPattern::M010();
    EXPECT_EQ(1, M.apply(0, 0, 0));
    EXPECT_EQ(0, M.apply(0, 1, 0));
    EXPECT_EQ(0, M.apply(0, 2, 0));
    EXPECT_EQ(1, M.apply(0, 3, 0));
    EXPECT_EQ(1, M.apply(1, 0, 0));
    EXPECT_EQ(0, M.apply(0, 0, 1));
}

TEST(QrCodeMaskPattern, checkM011) {
    const auto& M = QrCodeMaskPattern::M011();
    EXPECT_EQ(1, M.apply(0, 0, 0));
    EXPECT_EQ(0, M.apply(0, 1, 0));
    EXPECT_EQ(0, M.apply(0, 2, 0));
    EXPECT_EQ(1, M.apply(0, 3, 0));
    EXPECT_EQ(0, M.apply(1, 1, 0));
    EXPECT_EQ(1, M.apply(1, 2, 0));
    EXPECT_EQ(0, M.apply(0, 0, 1));
}

TEST(QrCodeMaskPattern, checkM100) {
    const auto& M = QrCodeMaskPattern::M100();
    EXPECT_EQ(1, M.apply(0, 0, 0));
    EXPECT_EQ(1, M.apply(0, 1, 0));
    EXPECT_EQ(1, M.apply(0, 2, 0));
    EXPECT_EQ(0, M.apply(0, 3, 0));
    EXPECT_EQ(1, M.apply(1, 0, 0));
    EXPECT_EQ(0, M.apply(2, 0, 0));
    EXPECT_EQ(0, M.apply(0, 0, 1));
}

TEST(QrCodeMaskPattern, checkM101) {
    const auto& M = QrCodeMaskPattern::M101();
    EXPECT_EQ(1, M.apply(0, 0, 0));
    EXPECT_EQ(1, M.apply(0, 1, 0));
    EXPECT_EQ(1, M.apply(0, 2, 0));
    EXPECT_EQ(1, M.apply(0, 3, 0));
    EXPECT_EQ(1, M.apply(1, 0, 0));
    EXPECT_EQ(1, M.apply(2, 0, 0));
    EXPECT_EQ(0, M.apply(1, 1, 0));
    EXPECT_EQ(0, M.apply(2, 2, 0));
    EXPECT_EQ(1, M.apply(2, 3, 0));
    EXPECT_EQ(0, M.apply(0, 0, 1));
}

TEST(QrCodeMaskPattern, checkM110) {
    const auto& M = QrCodeMaskPattern::M110();
    EXPECT_EQ(1, M.apply(0, 0, 0));
    EXPECT_EQ(1, M.apply(0, 1, 0));
    EXPECT_EQ(1, M.apply(0, 2, 0));
    EXPECT_EQ(1, M.apply(0, 3, 0));
    EXPECT_EQ(1, M.apply(1, 0, 0));
    EXPECT_EQ(1, M.apply(2, 0, 0));
    EXPECT_EQ(1, M.apply(1, 1, 0));
    EXPECT_EQ(0, M.apply(2, 2, 0));
    EXPECT_EQ(1, M.apply(1, 2, 0));
    EXPECT_EQ(0, M.apply(0, 0, 1));
}

TEST(QrCodeMaskPattern, checkM111) {
    const auto& M = QrCodeMaskPattern::M111();
    EXPECT_EQ(1, M.apply(0, 0, 0));
    EXPECT_EQ(0, M.apply(0, 1, 0));
    EXPECT_EQ(1, M.apply(0, 2, 0));
    EXPECT_EQ(0, M.apply(0, 3, 0));
    EXPECT_EQ(0, M.apply(1, 0, 0));
    EXPECT_EQ(1, M.apply(2, 0, 0));
    EXPECT_EQ(0, M.apply(1, 1, 0));
    EXPECT_EQ(0, M.apply(2, 2, 0));
    EXPECT_EQ(0, M.apply(1, 2, 0));
    EXPECT_EQ(1, M.apply(1, 3, 0));
    EXPECT_EQ(0, M.apply(0, 0, 1));
}

TEST(QrCodeMaskPattern, lookupMask_int) {
    EXPECT_EQ(&QrCodeMaskPattern::M000(), &QrCodeMaskPattern::lookupMask(0b000));
    EXPECT_EQ(&QrCodeMaskPattern::M101(), &QrCodeMaskPattern::lookupMask(0b101));
    EXPECT_THROW(QrCodeMaskPattern::lookupMask(8), std::runtime_error);
}
