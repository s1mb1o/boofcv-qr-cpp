// Port of:
//   boofcv.alg.filter.binary.ThresholdBlock
//   boofcv.alg.filter.binary.ThresholdBlockOtsu
//   boofcv.alg.filter.binary.ComputeOtsu
// Upstream: BoofCV v1.3.0.
//
// QR's default binarizer per ConfigQrCode.java is BLOCK_OTSU with
// useOtsu2=true, scale=1.0, thresholdFromLocalBlocks=true, tuning=4,
// region size=40. We collapse the BoofCV `ThresholdBlock` + concrete
// `ThresholdBlockOtsu` + `ComputeOtsu` triad into one class because
// BoofCV's BlockProcessor abstraction (so a single ThresholdBlock can
// drive multiple algorithm variants) doesn't pay for itself in our
// QR-only port.
//
// Per CLAUDE.md "Verbatim where algorithmic": the inner Otsu loop
// preserves loop structure, variable names, and comments. The block-
// iteration outer loop also mirrors Java line-for-line.
//
// Binary image convention (per CLAUDE.md): output is CV_8UC1 with
// 0 = background (light), 1 = foreground (dark = QR module). When
// `down = true` (the QR default), pixels <= threshold get value 1.
//
// Algorithm description: src/binary/threshold_block_otsu.md.

#ifndef BOOFCV_QR_THRESHOLD_BLOCK_OTSU_HPP
#define BOOFCV_QR_THRESHOLD_BLOCK_OTSU_HPP

#include <opencv2/core.hpp>

#include <cstdint>
#include <vector>

namespace boofcv_qr {

class ThresholdBlockOtsu {
public:
    // QR-default parameters from ConfigQrCode.java.
    struct Config {
        // Otsu variant. true = otsu2 (mean-of-means form, more robust on
        // bimodal histograms). QR uses true.
        bool useOtsu2 = true;

        // Threshold-direction. true = pixels <= threshold are foreground;
        // false = pixels > threshold are foreground. QR uses true (dark
        // modules = foreground).
        bool down = true;

        // Multiplicative scale on the computed threshold. 1.0 = unmodified.
        double scale = 1.0;

        // Tuning parameter for the texture-penalty extension. 0 = standard
        // Otsu. >0 penalises low-variance regions (suppresses spurious
        // detections in textureless areas). QR uses 4.
        double tuning = 4.0;

        // Approximate desired side length of each block, in pixels.
        // BoofCV's `ThresholdBlock` then rounds this so blocks tile the
        // image exactly. QR uses 40.
        int32_t requestedBlockWidth = 40;

        // If true, each block's threshold is computed from the histogram
        // of its 3x3 local neighborhood (smoother result, less blockiness
        // at edges). QR uses true.
        bool thresholdFromLocalBlocks = true;
    };

    ThresholdBlockOtsu() : cfg_() {}
    explicit ThresholdBlockOtsu(const Config& cfg) : cfg_(cfg) {}

    // Binarise `input` (CV_8UC1) into `output` (CV_8UC1, 0/1). Output
    // is reshaped to match input. Caller-provided buffers are reused
    // across calls when sized correctly.
    void process(const cv::Mat& input, cv::Mat& output);

    const Config& config() const { return cfg_; }

private:
    Config cfg_;

    // Per-block 256-int histograms, stored row-major as
    // `block_y * blocksWide + block_x` with each block contributing
    // 256 consecutive ints.
    std::vector<int32_t> stats_;
    int32_t blocksWide_ = 0;
    int32_t blocksHigh_ = 0;
    int32_t blockWidth_ = 0;
    int32_t blockHeight_ = 0;

    void selectBlockSize(int32_t width, int32_t height,
                         int32_t requestedBlockWidth);
    void computeStatistics(const cv::Mat& input, int32_t innerWidth,
                           int32_t innerHeight);
    void applyThreshold(const cv::Mat& input, cv::Mat& output);
    void thresholdBlock(int32_t blockX0, int32_t blockY0, const cv::Mat& input,
                        cv::Mat& output, const int32_t* histogram);
    void computeBlockStatistics(int32_t x0, int32_t y0, int32_t width,
                                int32_t height, int32_t indexStats,
                                const cv::Mat& input);
};

}  // namespace boofcv_qr

#endif  // BOOFCV_QR_THRESHOLD_BLOCK_OTSU_HPP
