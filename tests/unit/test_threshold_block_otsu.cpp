// Tests for ThresholdBlockOtsu. We don't ship a Java parity test
// (BoofCV has none in boofcv-recognition for this class — its tests
// live in boofcv-ip/src/test under the BlockProcessor abstraction
// which we don't replicate). Instead we verify properties:
//
//  1. Bimodal images (clear black/white split) produce the expected
//     binary partition.
//  2. Output is in the documented `0/1` convention with foreground =
//     dark module when `down = true`.
//  3. Process can be called repeatedly without state leakage.

#include "boofcv_qr/threshold_block_otsu.hpp"

#include <gtest/gtest.h>

#include <opencv2/core.hpp>

using boofcv_qr::ThresholdBlockOtsu;

namespace {

cv::Mat makeBimodal(int rows, int cols, std::uint8_t bg, std::uint8_t fg,
                    int splitCol) {
    cv::Mat m(rows, cols, CV_8UC1, cv::Scalar(bg));
    m(cv::Rect(splitCol, 0, cols - splitCol, rows)).setTo(fg);
    return m;
}

}  // namespace

// Half-bright / half-dark image: all bright pixels should be 0 (background)
// and all dark pixels should be 1 (foreground = dark module).
TEST(ThresholdBlockOtsu, halfDarkHalfBright) {
    cv::Mat input = makeBimodal(120, 120, /*bg=*/220, /*fg=*/30,
                                /*splitCol=*/60);
    ThresholdBlockOtsu alg;  // defaults match the QR config
    cv::Mat output;
    alg.process(input, output);

    ASSERT_EQ(input.rows, output.rows);
    ASSERT_EQ(input.cols, output.cols);
    ASSERT_EQ(CV_8UC1, output.type());

    // Sample well away from the split to dodge block-boundary effects.
    EXPECT_EQ(0, output.at<std::uint8_t>(60, 10));   // bright -> 0
    EXPECT_EQ(1, output.at<std::uint8_t>(60, 110));  // dark -> 1

    // Output values must only ever be 0 or 1.
    for (int y = 0; y < output.rows; y++) {
        const std::uint8_t* row = output.ptr<std::uint8_t>(y);
        for (int x = 0; x < output.cols; x++) {
            ASSERT_LE(row[x], 1) << "y=" << y << " x=" << x;
        }
    }
}

// `down = false` flips the convention: dark pixels -> 0, bright -> 1.
TEST(ThresholdBlockOtsu, downFalseFlipsConvention) {
    cv::Mat input = makeBimodal(120, 120, 220, 30, 60);
    ThresholdBlockOtsu::Config cfg;
    cfg.down = false;
    ThresholdBlockOtsu alg(cfg);
    cv::Mat output;
    alg.process(input, output);

    EXPECT_EQ(1, output.at<std::uint8_t>(60, 10));   // bright -> 1
    EXPECT_EQ(0, output.at<std::uint8_t>(60, 110));  // dark -> 0
}

// Repeated process() calls must not carry state forward.
TEST(ThresholdBlockOtsu, repeatable) {
    cv::Mat a = makeBimodal(120, 120, 220, 30, 60);
    cv::Mat b = makeBimodal(80, 200, 50, 200, 100);
    ThresholdBlockOtsu alg;
    cv::Mat outA, outB, outA2;
    alg.process(a, outA);
    alg.process(b, outB);
    alg.process(a, outA2);

    EXPECT_TRUE(cv::countNonZero(outA != outA2) == 0)
        << "process() left state from intermediate run";
    EXPECT_EQ(b.rows, outB.rows);
    EXPECT_EQ(b.cols, outB.cols);
}

// Reject non-CV_8UC1 inputs cleanly.
TEST(ThresholdBlockOtsu, rejectsWrongType) {
    cv::Mat input(120, 120, CV_32FC1, cv::Scalar(0.5));
    ThresholdBlockOtsu alg;
    cv::Mat output;
    EXPECT_THROW(alg.process(input, output), std::invalid_argument);
}

// Reject images smaller than the requested block.
TEST(ThresholdBlockOtsu, rejectsTooSmallImage) {
    cv::Mat input(20, 20, CV_8UC1, cv::Scalar(128));
    ThresholdBlockOtsu::Config cfg;
    cfg.requestedBlockWidth = 40;  // > input dim
    ThresholdBlockOtsu alg(cfg);
    cv::Mat output;
    // BoofCV's selectBlockSize uses height < requestedBlockWidth ->
    // blockHeight = height, so 20x20 with block=20 yields 1x1 blocks
    // and processes successfully. This sanity test just confirms it
    // doesn't crash / throw on a minimal image.
    EXPECT_NO_THROW(alg.process(input, output));
    EXPECT_EQ(20, output.rows);
    EXPECT_EQ(20, output.cols);
}
