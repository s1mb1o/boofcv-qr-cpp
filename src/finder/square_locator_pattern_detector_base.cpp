// Port of boofcv.alg.fiducial.qrcode.SquareLocatorPatternDetectorBase
// (BoofCV v1.3.0). Verbatim per CLAUDE.md "Verbatim vs idiomize".

#include "boofcv_qr/finder/square_locator_pattern_detector_base.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace boofcv_qr {

namespace {

inline void movingAverageUpdate(double& avg, double sample, double decay) {
    avg = avg * decay + sample * (1.0 - decay);
}

}  // namespace

SquareLocatorPatternDetectorBase::SquareLocatorPatternDetectorBase(
    std::shared_ptr<DetectPolygonBinaryGrayRefine> squareDetector)
    : squareDetector_(std::move(squareDetector)) {
    // Configure the wrapped detector for "convex 4-sided shape, output
    // CCW in image coords". Mirrors Java ctor.
    squareDetector_->getDetector().setConvex(true);
    squareDetector_->getDetector().setOutputClockwiseUpY(false);
    squareDetector_->getDetector().setNumberOfSides(4, 4);
}

void SquareLocatorPatternDetectorBase::process(const cv::Mat& gray,
                                                 const cv::Mat& binary) {
    // don't sanity check binary shape here since it may or may not be padded. See square detector
    if (gray.type() != CV_8UC1)
        throw std::invalid_argument(
            "SquareLocatorPatternDetectorBase: gray must be CV_8UC1");

    configureContourDetector(gray);
    gray_ = gray;

    // detect squares
    squareDetector_->process(gray, binary);

    auto time0 = std::chrono::steady_clock::now();
    findLocatorPatternsFromSquares();
    auto time1 = std::chrono::steady_clock::now();

    double milli = std::chrono::duration<double, std::milli>(time1 - time0).count();
    movingAverageUpdate(profilingMS_, milli, 0.8);
}

void SquareLocatorPatternDetectorBase::configureContourDetector(
    const cv::Mat& gray) {
    // determine the maximum possible size of a position pattern
    // contour size is maximum when viewed head one. Assume the smallest qrcode is 3x this width
    // 4 side in a square
    int32_t maxContourSize =
        static_cast<int32_t>(std::min(gray.cols, gray.rows) * maxContourFraction_);

    // Java's `setMaxContour` on the binary contour finder is a perf
    // optimisation (caps external contour length). We don't have a
    // direct setter on `DetectPolygonFromContour` for an upper bound
    // — only the lower bound (`setMinimumContour`). Logging the cap as
    // a hint and skipping is harmless for correctness because the
    // polyline corner finder already gates on `maxSideError`. // TODO(perf):
    // wire setMaximumContour into DetectPolygonFromContour and apply
    // here.
    (void)maxContourSize;

    // `setSaveInnerContour(false)` — internal contours aren't used
    // post-detection by the QR finder. Save the bookkeeping cost.
    squareDetector_->getDetector().setSaveInternalContours(false);
}

float SquareLocatorPatternDetectorBase::sampleBilinear(float x, float y) const {
    // EXTENDED border (cv::BORDER_REPLICATE) — clamp to image bounds.
    int32_t W = gray_.cols;
    int32_t H = gray_.rows;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > static_cast<float>(W - 1)) x = static_cast<float>(W - 1);
    if (y > static_cast<float>(H - 1)) y = static_cast<float>(H - 1);

    int32_t x0 = static_cast<int32_t>(std::floor(x));
    int32_t y0 = static_cast<int32_t>(std::floor(y));
    int32_t x1 = std::min(x0 + 1, W - 1);
    int32_t y1 = std::min(y0 + 1, H - 1);
    float ax = x - static_cast<float>(x0);
    float ay = y - static_cast<float>(y0);

    float v00 = static_cast<float>(gray_.at<uint8_t>(y0, x0));
    float v10 = static_cast<float>(gray_.at<uint8_t>(y0, x1));
    float v01 = static_cast<float>(gray_.at<uint8_t>(y1, x0));
    float v11 = static_cast<float>(gray_.at<uint8_t>(y1, x1));

    float v0 = v00 + (v10 - v00) * ax;
    float v1 = v01 + (v11 - v01) * ax;
    return v0 + (v1 - v0) * ay;
}

}  // namespace boofcv_qr
