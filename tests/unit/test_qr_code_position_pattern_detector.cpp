// Mirrors TestQrCodePositionPatternDetector.java. Upstream: BoofCV v1.3.0.
//
// The Java JUnit suite renders position patterns via Java AWT
// (Graphics2D.fillRect for the three nested squares). We use OpenCV's
// `cv::rectangle` instead — same end-to-end test cases (`easy`,
// `checkPositionPatternAppearance`, `positionSquareIntensityCheck`).

#include "boofcv_qr/finder/qr_code_position_pattern_detector.hpp"
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
using boofcv_qr::PositionPatternNode;
using boofcv_qr::QrCodePositionPatternDetector;
using boofcv_qr::RefinePolygonToGrayLine;

namespace {

// Mirror of the Java test's `renderPP(g2, x0, y0, width)` — three
// nested concentric rectangles forming a finder pattern.
void renderPP(cv::Mat& image, int32_t x0, int32_t y0, int32_t width) {
    // 7-block black outer square.
    cv::rectangle(image, cv::Rect(x0, y0, width, width), cv::Scalar(0),
                  cv::FILLED);
    // 5-block white middle square (1-block inset).
    int32_t inset1 = width / 7;
    cv::rectangle(image, cv::Rect(x0 + inset1, y0 + inset1, width * 5 / 7,
                                    width * 5 / 7),
                  cv::Scalar(255), cv::FILLED);
    // 3-block black centre square.
    int32_t inset2 = width * 2 / 7;
    cv::rectangle(image, cv::Rect(x0 + inset2, y0 + inset2, width * 3 / 7,
                                    width * 3 / 7),
                  cv::Scalar(0), cv::FILLED);
}

cv::Mat renderImage(const std::vector<std::tuple<int32_t, int32_t, int32_t>>& pps) {
    cv::Mat image(400, 300, CV_8UC1, cv::Scalar(255));
    for (const auto& pp : pps) {
        renderPP(image, std::get<0>(pp), std::get<1>(pp), std::get<2>(pp));
    }
    return image;
}

std::unique_ptr<QrCodePositionPatternDetector> createAlg() {
    auto adapter = std::make_unique<PolylineSplitMergeAdapter>();
    adapter->setMinimumSides(4);
    adapter->setMaximumSides(4);

    auto detector = std::make_unique<DetectPolygonFromContour>(
        std::move(adapter), /*outputClockwiseUpY=*/true,
        /*canTouchBorder=*/false,
        /*contourEdgeThreshold=*/3.0,
        /*tangentEdgeIntensity=*/1.5);
    detector->setNumberOfSides(4, 4);

    ConfigRefinePolygonLineToImage cfg;
    auto refine = std::make_shared<RefinePolygonToGrayLine>(cfg);

    auto wrapper = std::make_shared<DetectPolygonBinaryGrayRefine>(
        std::move(detector), std::move(refine),
        /*minimumRefineEdgeIntensity=*/3.0,
        /*adjustForThresholdBias=*/true);

    return std::make_unique<QrCodePositionPatternDetector>(std::move(wrapper));
}

std::vector<cv::Point2d> makeSquare(int32_t x0, int32_t y0, int32_t width) {
    return {
        cv::Point2d(x0, y0),
        cv::Point2d(x0 + width, y0),
        cv::Point2d(x0 + width, y0 + width),
        cv::Point2d(x0, y0 + width),
    };
}

}  // namespace

TEST(QrCodePositionPatternDetector, easy) {
    // 3 finder patterns in an L-shape — same coords as the Java test.
    cv::Mat image = renderImage({{40, 60, 70}, {140, 60, 70}, {40, 150, 70}});
    cv::Mat binary;
    cv::threshold(image, binary, 100, 1, cv::THRESH_BINARY_INV);

    auto alg = createAlg();

    // Run twice to verify reset is correct.
    for (int32_t trial = 0; trial < 2; trial++) {
        alg->process(image, binary);

        const auto& list = alg->getPositionPatterns();
        EXPECT_EQ(3u, list.size())
            << "trial " << trial << ": expected 3 finder patterns, got "
            << list.size();

        // Each finder pattern's centre is at (x0 + width/2, y0 + width/2)
        // — within ~2 px tolerance to allow for sub-pixel refinement.
        std::vector<cv::Point2d> expectedCenters = {
            {40 + 35, 60 + 35},
            {140 + 35, 60 + 35},
            {40 + 35, 150 + 35},
        };
        for (const auto& expected : expectedCenters) {
            bool foundMatch = false;
            for (const auto& pp : list) {
                double dx = pp.center.x - expected.x;
                double dy = pp.center.y - expected.y;
                if (std::sqrt(dx * dx + dy * dy) < 3.0) {
                    foundMatch = true;
                    break;
                }
            }
            EXPECT_TRUE(foundMatch)
                << "no finder centre near (" << expected.x << ", "
                << expected.y << ")";
        }
    }
}

TEST(QrCodePositionPatternDetector, checkPositionPatternAppearance_positive) {
    cv::Mat image = renderImage({{40, 60, 70}});

    auto alg = createAlg();

    // Need to run process() once to bind the gray image to the
    // base's interpolate sampler.
    cv::Mat binary;
    cv::threshold(image, binary, 100, 1, cv::THRESH_BINARY_INV);
    alg->process(image, binary);

    auto square = makeSquare(40, 60, 70);
    EXPECT_TRUE(alg->checkPositionPatternAppearance(square, 100));
}

TEST(QrCodePositionPatternDetector, checkPositionPatternAppearance_negative_filledStone) {
    // Render a finder pattern, then over-fill the inner stone with
    // black — destroys the 1:1:3:1:1 ratio.
    cv::Mat image = renderImage({{40, 60, 70}});
    cv::rectangle(image, cv::Rect(40, 60, 70, 70), cv::Scalar(0), cv::FILLED);

    auto alg = createAlg();
    cv::Mat binary;
    cv::threshold(image, binary, 100, 1, cv::THRESH_BINARY_INV);
    alg->process(image, binary);

    auto square = makeSquare(40, 60, 70);
    EXPECT_FALSE(alg->checkPositionPatternAppearance(square, 100));
}

TEST(QrCodePositionPatternDetector, positionSquareIntensityCheck) {
    float positive[7] = {10, 200, 10, 10, 10, 200, 10};
    EXPECT_TRUE(QrCodePositionPatternDetector::positionSquareIntensityCheck(
        positive, 100));

    // Each single-bit flip must reject.
    for (int32_t i = 0; i < 7; i++) {
        float negative[7];
        for (int32_t k = 0; k < 7; k++) negative[k] = positive[k];
        negative[i] = (negative[i] < 100) ? 200 : 10;
        EXPECT_FALSE(QrCodePositionPatternDetector::positionSquareIntensityCheck(
            negative, 100))
            << "flip at index " << i << " should reject";
    }
}
