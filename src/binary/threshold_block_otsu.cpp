// Port of BoofCV's BLOCK_OTSU binarizer. See header for context and
// the algorithm doc src/binary/threshold_block_otsu.md.
//
// CLAUDE.md "Verbatim where algorithmic" — Otsu's between-class-variance
// loop and the texture-penalty adjustment match BoofCV's `ComputeOtsu`
// line for line. The block-iteration loop matches `ThresholdBlock`.

#include "boofcv_qr/threshold_block_otsu.hpp"

#include <algorithm>
#include <stdexcept>

namespace boofcv_qr {

namespace {

constexpr int32_t kHistogramLen = 256;

// Per-block Otsu computation. `histogram` has length 256, `totalPixels`
// is the sum of histogram[0..255].
//
// Mirrors ComputeOtsu.computeOtsu / computeOtsu2 / compute exactly.
struct OtsuResult {
    double threshold;
    double variance;
};

OtsuResult computeOtsuRaw(const int32_t* histogram, int32_t length,
                          int32_t totalPixels, bool useOtsu2) {
    OtsuResult r{0.0, 0.0};

    double dlength = static_cast<double>(length);
    double sum = 0.0;
    for (int32_t i = 0; i < length; i++) {
        sum += (i / dlength) * histogram[i];
    }

    double sumB = 0.0;
    int32_t wB = 0;

    double selectedMB = 0.0;
    double selectedMF = 0.0;

    for (int32_t i = 0; i < length; i++) {
        wB += histogram[i];               // Weight Background
        if (wB == 0) continue;

        int32_t wF = totalPixels - wB;    // Weight Foreground
        if (wF == 0) break;

        double f = i / dlength;
        sumB += f * histogram[i];

        double mB = sumB / wB;            // Mean Background
        double mF = (sum - sumB) / wF;    // Mean Foreground

        // Calculate Between Class Variance
        double varBetween =
            static_cast<double>(wB) * static_cast<double>(wF) * (mB - mF) * (mB - mF);

        if (varBetween > r.variance) {
            r.variance = varBetween;
            if (useOtsu2) {
                selectedMB = mB;
                selectedMF = mF;
            } else {
                r.threshold = i;
            }
        }
    }

    if (useOtsu2) {
        // select a threshold which maximises the distance between the two
        // distributions. In pathological cases there's a dead zone where
        // all values are equally good and it would select a value with a
        // low index arbitrarily; this form sidesteps that.
        r.threshold = length * (selectedMB + selectedMF) / 2.0;
    }
    return r;
}

double finalizeThreshold(double threshold, double variance, double tuning,
                         double scale, bool down) {
    // apply optional penalty to low texture regions
    variance += 0.001;  // avoid divide by zero
    int32_t adjustment = static_cast<int32_t>(
        tuning * threshold * tuning * threshold / variance + 0.5);
    threshold += down ? -adjustment : adjustment;
    return static_cast<double>(static_cast<int32_t>(
        scale * std::max(threshold, 0.0) + 0.5));
}

}  // namespace

void ThresholdBlockOtsu::selectBlockSize(int32_t width, int32_t height,
                                         int32_t requestedBlockWidth) {
    if (height < requestedBlockWidth) {
        blockHeight_ = height;
    } else {
        int32_t rows = height / requestedBlockWidth;
        blockHeight_ = height / rows;
    }
    if (width < requestedBlockWidth) {
        blockWidth_ = width;
    } else {
        int32_t cols = width / requestedBlockWidth;
        blockWidth_ = width / cols;
    }
}

void ThresholdBlockOtsu::computeBlockStatistics(int32_t x0, int32_t y0,
                                                int32_t width, int32_t height,
                                                int32_t indexStats,
                                                const cv::Mat& input) {
    int32_t* hist = stats_.data() + indexStats;
    for (int32_t i = 0; i < kHistogramLen; i++) hist[i] = 0;

    for (int32_t y = 0; y < height; y++) {
        const std::uint8_t* row = input.ptr<std::uint8_t>(y0 + y) + x0;
        for (int32_t x = 0; x < width; x++) {
            hist[row[x]]++;
        }
    }
}

void ThresholdBlockOtsu::computeStatistics(const cv::Mat& input,
                                           int32_t innerWidth,
                                           int32_t innerHeight) {
    int32_t indexStats = 0;
    int32_t statPixelStride = kHistogramLen;

    for (int32_t y = 0; y < innerHeight; y += blockHeight_) {
        for (int32_t x = 0; x < innerWidth;
             x += blockWidth_, indexStats += statPixelStride) {
            computeBlockStatistics(x, y, blockWidth_, blockHeight_, indexStats,
                                   input);
        }
        if (innerWidth != input.cols) {
            computeBlockStatistics(innerWidth, y, input.cols - innerWidth,
                                   blockHeight_, indexStats, input);
            indexStats += statPixelStride;
        }
    }
    if (innerHeight != input.rows) {
        int32_t y = innerHeight;
        int32_t bh = input.rows - innerHeight;
        for (int32_t x = 0; x < innerWidth;
             x += blockWidth_, indexStats += statPixelStride) {
            computeBlockStatistics(x, y, blockWidth_, bh, indexStats, input);
        }
        if (innerWidth != input.cols) {
            computeBlockStatistics(innerWidth, y, input.cols - innerWidth, bh,
                                   indexStats, input);
        }
    }
}

void ThresholdBlockOtsu::thresholdBlock(int32_t blockX0, int32_t blockY0,
                                        const cv::Mat& input, cv::Mat& output,
                                        std::vector<int32_t>& workHistogram) {
    int32_t x0 = blockX0 * blockWidth_;
    int32_t y0 = blockY0 * blockHeight_;

    int32_t x1 =
        blockX0 == blocksWide_ - 1 ? input.cols : (blockX0 + 1) * blockWidth_;
    int32_t y1 =
        blockY0 == blocksHigh_ - 1 ? input.rows : (blockY0 + 1) * blockHeight_;

    int32_t bX0, bY0, bX1, bY1;
    if (cfg_.thresholdFromLocalBlocks) {
        bX1 = std::min(blocksWide_ - 1, blockX0 + 1);
        bY1 = std::min(blocksHigh_ - 1, blockY0 + 1);
        bX0 = std::max(0, blockX0 - 1);
        bY0 = std::max(0, blockY0 - 1);
    } else {
        bX0 = bX1 = blockX0;
        bY0 = bY1 = blockY0;
    }

    // sum up histogram in local region
    for (int32_t i = 0; i < kHistogramLen; i++)
        workHistogram[static_cast<std::size_t>(i)] = 0;

    for (int32_t y = bY0; y <= bY1; y++) {
        for (int32_t x = bX0; x <= bX1; x++) {
            int32_t indexStats = (y * blocksWide_ + x) * kHistogramLen;
            for (int32_t i = 0; i < kHistogramLen; i++) {
                workHistogram[static_cast<std::size_t>(i)] +=
                    stats_[static_cast<std::size_t>(indexStats + i)];
            }
        }
    }

    int32_t total = 0;
    for (int32_t i = 0; i < kHistogramLen; i++)
        total += workHistogram[static_cast<std::size_t>(i)];

    OtsuResult res = computeOtsuRaw(workHistogram.data(), kHistogramLen, total,
                                    cfg_.useOtsu2);
    double threshold = finalizeThreshold(res.threshold, res.variance,
                                         cfg_.tuning, cfg_.scale, cfg_.down);

    std::uint8_t a, b;
    if (cfg_.down) {
        a = 1;
        b = 0;
    } else {
        a = 0;
        b = 1;
    }

    for (int32_t y = y0; y < y1; y++) {
        const std::uint8_t* in = input.ptr<std::uint8_t>(y);
        std::uint8_t* out = output.ptr<std::uint8_t>(y);
        for (int32_t x = x0; x < x1; x++) {
            out[x] = (static_cast<int32_t>(in[x]) <= threshold) ? a : b;
        }
    }
}

void ThresholdBlockOtsu::applyThreshold(const cv::Mat& input, cv::Mat& output) {
    std::vector<int32_t> workHistogram(kHistogramLen, 0);
    for (int32_t blockY = 0; blockY < blocksHigh_; blockY++) {
        for (int32_t blockX = 0; blockX < blocksWide_; blockX++) {
            thresholdBlock(blockX, blockY, input, output, workHistogram);
        }
    }
}

void ThresholdBlockOtsu::process(const cv::Mat& input, cv::Mat& output) {
    if (input.type() != CV_8UC1)
        throw std::invalid_argument(
            "ThresholdBlockOtsu requires CV_8UC1 input");
    output.create(input.size(), CV_8UC1);

    int32_t requested = cfg_.requestedBlockWidth;
    // BoofCV's ConfigLength.computeI(min(width, height)) — for a
    // fixed value the result is just the value itself.
    selectBlockSize(input.cols, input.rows, requested);

    blocksWide_ = input.cols / blockWidth_;
    blocksHigh_ = input.rows / blockHeight_;
    if (blocksWide_ == 0 || blocksHigh_ == 0)
        throw std::invalid_argument(
            "Image too small for requested block width");

    int32_t innerWidth =
        input.cols % blockWidth_ == 0
            ? input.cols
            : input.cols - blockWidth_ - input.cols % blockWidth_;
    int32_t innerHeight =
        input.rows % blockHeight_ == 0
            ? input.rows
            : input.rows - blockHeight_ - input.rows % blockHeight_;

    stats_.assign(static_cast<std::size_t>(blocksWide_) *
                      static_cast<std::size_t>(blocksHigh_) *
                      static_cast<std::size_t>(kHistogramLen),
                  0);

    computeStatistics(input, innerWidth, innerHeight);
    applyThreshold(input, output);
}

}  // namespace boofcv_qr
