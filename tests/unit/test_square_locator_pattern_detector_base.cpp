// Smoke tests for `SquareLocatorPatternDetectorBase`. The Java suite
// only tests this class indirectly via QrCodePositionPatternDetector;
// here we cover the small surface that the base provides standalone:
// shape validation, profilingMS plumbing, and the configureContourDetector
// hook (`saveInternalContours=false` after process).

#include "boofcv_qr/finder/square_locator_pattern_detector_base.hpp"
#include "boofcv_qr/polygon/detect_polygon_binary_gray_refine.hpp"
#include "boofcv_qr/polygon/detect_polygon_from_contour.hpp"
#include "boofcv_qr/polygon/refine_polygon_to_gray.hpp"

#include <gtest/gtest.h>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <memory>

using boofcv_qr::ConfigRefinePolygonLineToImage;
using boofcv_qr::DetectPolygonBinaryGrayRefine;
using boofcv_qr::DetectPolygonFromContour;
using boofcv_qr::PolylineSplitMergeAdapter;
using boofcv_qr::RefinePolygonToGrayLine;
using boofcv_qr::SquareLocatorPatternDetectorBase;

namespace {

// Concrete subclass exposing the abstract hook so we can drive the
// base in isolation. Records the number of times the hook fired.
class StubLocatorDetector : public SquareLocatorPatternDetectorBase {
public:
    int32_t hookCalls = 0;

    using SquareLocatorPatternDetectorBase::SquareLocatorPatternDetectorBase;

protected:
    void findLocatorPatternsFromSquares() override { hookCalls++; }
};

std::shared_ptr<DetectPolygonBinaryGrayRefine> makeWrapper() {
    auto adapter = std::make_unique<PolylineSplitMergeAdapter>();
    adapter->setMinimumSides(4);
    adapter->setMaximumSides(4);

    auto detector = std::make_unique<DetectPolygonFromContour>(
        std::move(adapter), /*outputClockwiseUpY=*/true,
        /*canTouchBorder=*/false,
        /*contourEdgeThreshold=*/0.0,
        /*tangentEdgeIntensity=*/1.0);

    ConfigRefinePolygonLineToImage cfg;
    auto refine = std::make_shared<RefinePolygonToGrayLine>(cfg);

    return std::make_shared<DetectPolygonBinaryGrayRefine>(
        std::move(detector), std::move(refine),
        /*minimumRefineEdgeIntensity=*/0.0,
        /*adjustForThresholdBias=*/true);
}

}  // namespace

TEST(SquareLocatorPatternDetectorBase, ctorConfiguresWrapper) {
    auto wrapper = makeWrapper();
    StubLocatorDetector alg(wrapper);

    // Base ctor sets the wrapped detector to "convex 4-sided shape, output CCW (clockwise=false)".
    EXPECT_TRUE(wrapper->getDetector().isConvex());
    EXPECT_FALSE(wrapper->getDetector().isOutputClockwiseUpY());
    EXPECT_EQ(4, wrapper->getDetector().getMinimumSides());
    EXPECT_EQ(4, wrapper->getDetector().getMaximumSides());
}

TEST(SquareLocatorPatternDetectorBase, processInvokesHookAndDisablesInternalContours) {
    auto wrapper = makeWrapper();
    // Default after step 7b/2 is `saveInternalContours_=true`.
    EXPECT_TRUE(wrapper->getDetector().isSaveInternalContours());

    StubLocatorDetector alg(wrapper);

    // Single black 30x30 square on a 200x200 white image.
    int32_t W = 200, H = 200;
    cv::Mat gray(H, W, CV_8UC1, cv::Scalar(200));
    cv::rectangle(gray, cv::Rect(50, 50, 30, 30), cv::Scalar(0), cv::FILLED);
    cv::Mat binary;
    cv::threshold(gray, binary, 100, 1, cv::THRESH_BINARY_INV);

    EXPECT_EQ(0, alg.hookCalls);
    alg.process(gray, binary);
    EXPECT_EQ(1, alg.hookCalls);
    alg.process(gray, binary);
    EXPECT_EQ(2, alg.hookCalls);

    // configureContourDetector flips the saveInternalContours flag off.
    EXPECT_FALSE(wrapper->getDetector().isSaveInternalContours());

    // profilingMS gets a non-trivial sample after at least one call.
    EXPECT_GE(alg.getProfilingMS(), 0.0);
}

TEST(SquareLocatorPatternDetectorBase, maxContourFractionRoundtrip) {
    auto wrapper = makeWrapper();
    StubLocatorDetector alg(wrapper);

    EXPECT_DOUBLE_EQ(4.0 / 3.0, alg.getMaxContourFraction());
    alg.setMaxContourFraction(2.0);
    EXPECT_DOUBLE_EQ(2.0, alg.getMaxContourFraction());
}

TEST(SquareLocatorPatternDetectorBase, processRejectsNonU8) {
    auto wrapper = makeWrapper();
    StubLocatorDetector alg(wrapper);

    cv::Mat grayF(50, 50, CV_32FC1, cv::Scalar(0));
    cv::Mat binary(50, 50, CV_8UC1, cv::Scalar(0));
    EXPECT_THROW(alg.process(grayF, binary), std::invalid_argument);
}
