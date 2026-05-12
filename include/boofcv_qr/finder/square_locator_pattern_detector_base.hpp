// Port of boofcv.alg.fiducial.qrcode.SquareLocatorPatternDetectorBase
// (BoofCV v1.3.0). Step 7c.
//
// Per CLAUDE.md type mappings:
//   ImageGray<T> -> cv::Mat CV_8UC1 (template <T> dropped)
//   PixelTransform<Point2D_F32> -> deferred (lens distortion stub)
//   MovingAverage -> inline exponential-decay double

#ifndef BOOFCV_QR_FINDER_SQUARE_LOCATOR_PATTERN_DETECTOR_BASE_HPP
#define BOOFCV_QR_FINDER_SQUARE_LOCATOR_PATTERN_DETECTOR_BASE_HPP

#include "boofcv_qr/polygon/detect_polygon_binary_gray_refine.hpp"

#include <opencv2/core.hpp>

#include <cstdint>
#include <memory>

namespace boofcv_qr {

// Several fiducials use square objects as locator patterns (QR finder
// patterns, AprilTag corners, …). This base wraps a
// `DetectPolygonBinaryGrayRefine` and gives concrete subclasses an
// abstract `findLocatorPatternsFromSquares` hook plus a bilinear
// gray-image sampler with EXTENDED-border clamping.
class SquareLocatorPatternDetectorBase {
public:
    explicit SquareLocatorPatternDetectorBase(
        std::shared_ptr<DetectPolygonBinaryGrayRefine> squareDetector);
    virtual ~SquareLocatorPatternDetectorBase() = default;

    // Detects squares in `gray`/`binary` and runs the subclass hook.
    // Both inputs must be CV_8UC1.
    void process(const cv::Mat& gray, const cv::Mat& binary);

    // Lens distortion — deferred. Stub no-op (mirrors step 5 / 7b
    // pattern). Lens-aware sampling is documented in the algorithm
    // doc but not wired in this port.
    void setLensDistortion(int32_t /*width*/, int32_t /*height*/) {}
    void clearLensDistortion() {}

    double getMaxContourFraction() const { return maxContourFraction_; }
    void setMaxContourFraction(double v) { maxContourFraction_ = v; }

    DetectPolygonBinaryGrayRefine& getSquareDetector() { return *squareDetector_; }
    const DetectPolygonBinaryGrayRefine& getSquareDetector() const {
        return *squareDetector_;
    }

    double getProfilingMS() const { return profilingMS_; }
    double getLastContourPolygonMS() const { return lastContourPolygonMS_; }
    double getLastFinderValidationMS() const { return lastFinderValidationMS_; }

    void resetRuntimeProfiling() {
        squareDetector_->resetRuntimeProfiling();
        profilingMS_ = 0.0;
        lastContourPolygonMS_ = 0.0;
        lastFinderValidationMS_ = 0.0;
    }

protected:
    // Concrete subclasses override this to walk the wrapped detector's
    // `getPolygonInfo()` list and turn it into their marker-specific
    // node list.
    virtual void findLocatorPatternsFromSquares() = 0;

    // Configures the contour finder based on the image size — caps
    // external contour length and disables internal-contour tracking
    // (perf optimization; QR finder doesn't need them post-detection).
    void configureContourDetector(const cv::Mat& gray);

    // EXTENDED-border bilinear sample. Used by the QR subclass during
    // the 1:1:3:1:1 appearance check.
    float sampleBilinear(float x, float y) const;

    // Used to subsample the input image. Bound during `process()`.
    cv::Mat gray_;

    // Used to prune very large contours. Default tuned for QR (two
    // adjacent position patterns side by side).
    double maxContourFraction_ = 4.0 / 3.0;

    // Wrapped polygon detector. Shared so subclasses can also see it.
    std::shared_ptr<DetectPolygonBinaryGrayRefine> squareDetector_;

    // Runtime profiling (exponential moving average of the
    // findLocatorPatternsFromSquares() call duration in ms).
    double profilingMS_ = 0.0;
    double lastContourPolygonMS_ = 0.0;
    double lastFinderValidationMS_ = 0.0;
};

}  // namespace boofcv_qr

#endif  // BOOFCV_QR_FINDER_SQUARE_LOCATOR_PATTERN_DETECTOR_BASE_HPP
