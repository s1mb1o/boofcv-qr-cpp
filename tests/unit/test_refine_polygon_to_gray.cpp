// Mirrors TestImageLineIntegral.java + TestSnapToLineEdge.java +
// TestRefinePolygonToGrayLine.java. Upstream: BoofCV v1.3.0.
//
// The full Java JUnit suites for SnapToLineEdge and RefinePolygonToGrayLine
// rely on FDistort / GeneralizedImageOps / CommonFitPolygonChecks / BoofTesting
// which we don't pull in. We mirror cases that are pure-algorithmic
// (TestImageLineIntegral) or that only need a synthetic 2D image
// (the "easy aligned", "alignedSquare", and "computePointsAndWeights"
// branches of the SnapToLineEdge / RefinePolygonToGrayLine tests).

#include "boofcv_qr/polygon/detect_polygon_binary_gray_refine.hpp"
#include "boofcv_qr/polygon/image_line_integral.hpp"
#include "boofcv_qr/polygon/refine_polygon_to_gray.hpp"
#include "boofcv_qr/polygon/snap_to_line_edge.hpp"

#include <gtest/gtest.h>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <memory>
#include <vector>

using boofcv_qr::AdjustPolygonForThresholdBias;
using boofcv_qr::ConfigRefinePolygonLineToImage;
using boofcv_qr::DetectPolygonBinaryGrayRefine;
using boofcv_qr::DetectPolygonFromContour;
using boofcv_qr::EdgeIntensityPolygon;
using boofcv_qr::ImageLineIntegral;
using boofcv_qr::LineGeneral2D;
using boofcv_qr::PolylineSplitMergeAdapter;
using boofcv_qr::RefinePolygonToGrayLine;
using boofcv_qr::SnapToLineEdge;

namespace {

constexpr double TOL_F64 = 1e-6;

// Mirror of JUnit's `checkSolution(x0,y0,x1,y1, expected)` — tries forward
// and reverse direction.
void checkLineIntegralSolution(ImageLineIntegral& alg, double x0, double y0,
                                double x1, double y1, double expected) {
    double found = alg.compute(x0, y0, x1, y1);
    EXPECT_NEAR(expected, found, TOL_F64);
    found = alg.compute(x1, y1, x0, y0);
    EXPECT_NEAR(expected, found, TOL_F64);
}

}  // namespace

// ---------------------------------------------------------------------------
// ImageLineIntegral — mirrors TestImageLineIntegral.java
// ---------------------------------------------------------------------------

TEST(ImageLineIntegral, zeroLengthLine) {
    cv::Mat img(15, 10, CV_8UC1, cv::Scalar(0));
    img.at<uint8_t>(6, 6) = 100;

    ImageLineIntegral alg;
    alg.setImage(img);

    checkLineIntegralSolution(alg, 6, 6, 6, 6, 0);
    checkLineIntegralSolution(alg, 6.1, 6.1, 6.1, 6.1, 0);
}

TEST(ImageLineIntegral, inside_SlopeZero) {
    cv::Mat img(15, 10, CV_8UC1, cv::Scalar(0));
    img.at<uint8_t>(6, 6) = 100;

    ImageLineIntegral alg;
    alg.setImage(img);

    checkLineIntegralSolution(alg, 6.5, 6, 6.5, 7, 100);
    checkLineIntegralSolution(alg, 6.5, 6, 6.5, 6.9, 0.9 * 100);
    checkLineIntegralSolution(alg, 6.5, 6.1, 6.5, 7.0, 0.9 * 100);

    checkLineIntegralSolution(alg, 6, 6.5, 7, 6.5, 100);
    checkLineIntegralSolution(alg, 6, 6.5, 6.9, 6.5, 0.9 * 100);
    checkLineIntegralSolution(alg, 6.1, 6.5, 7, 6.5, 0.9 * 100);
}

TEST(ImageLineIntegral, across_SlopeZero) {
    cv::Mat img(15, 10, CV_8UC1, cv::Scalar(0));
    img.at<uint8_t>(6, 6) = 100;
    img.at<uint8_t>(7, 6) = 50;
    img.at<uint8_t>(8, 6) = 10;
    img.at<uint8_t>(6, 7) = 50;
    img.at<uint8_t>(6, 8) = 10;

    ImageLineIntegral alg;
    alg.setImage(img);

    checkLineIntegralSolution(alg, 6.5, 6, 6.5, 8, 150);
    checkLineIntegralSolution(alg, 6.5, 6, 6.5, 7.5, 125);
    checkLineIntegralSolution(alg, 6.5, 6.5, 6.5, 8, 100);
    checkLineIntegralSolution(alg, 6.5, 6.5, 6.5, 7.5, 75);
    checkLineIntegralSolution(alg, 6.5, 6, 6.5, 8.5, 155);

    checkLineIntegralSolution(alg, 6, 6.5, 8, 6.5, 150);
    checkLineIntegralSolution(alg, 6, 6.5, 7.5, 6.5, 125);
    checkLineIntegralSolution(alg, 6.5, 6.5, 8, 6.5, 100);
    checkLineIntegralSolution(alg, 6.5, 6.5, 7.5, 6.5, 75);
    checkLineIntegralSolution(alg, 6, 6.5, 8.5, 6.5, 155);
}

TEST(ImageLineIntegral, inside_nonZero) {
    cv::Mat img(15, 10, CV_8UC1, cv::Scalar(255));
    img.at<uint8_t>(6, 6) = 100;

    ImageLineIntegral alg;
    alg.setImage(img);

    // entirely inside
    double r = std::sqrt(0.1 * 0.1 + 0.2 * 0.2);
    checkLineIntegralSolution(alg, 6.1, 6.2, 6.2, 6.4, r * 100);
    checkLineIntegralSolution(alg, 6.2, 6.1, 6.4, 6.2, r * 100);

    // one entire diagonal
    checkLineIntegralSolution(alg, 6, 6, 7, 7, std::sqrt(2.0) * 100);
    checkLineIntegralSolution(alg, 6, 7, 7, 6, std::sqrt(2.0) * 100);
}

TEST(ImageLineIntegral, across_nonZero) {
    cv::Mat img(15, 10, CV_8UC1, cv::Scalar(255));
    img.at<uint8_t>(6, 6) = 100;
    img.at<uint8_t>(7, 6) = 200;
    img.at<uint8_t>(8, 6) = 140;
    img.at<uint8_t>(6, 7) = 150;
    img.at<uint8_t>(6, 8) = 175;
    img.at<uint8_t>(7, 7) = 50;

    ImageLineIntegral alg;
    alg.setImage(img);

    // two entire diagonals at 45 degrees
    checkLineIntegralSolution(alg, 6, 6, 8, 8, std::sqrt(2.0) * (100 + 50));
    checkLineIntegralSolution(alg, 6, 8, 8, 6, std::sqrt(2.0) * (200 + 150));

    // two squares horizontal and vertical
    double r = std::sqrt(1.0 + 4.0) / 2.0;
    checkLineIntegralSolution(alg, 6, 6, 8, 7, r * (100 + 150));
    checkLineIntegralSolution(alg, 6, 6, 7, 8, r * (100 + 200));
}

TEST(ImageLineIntegral, isInside) {
    cv::Mat img(14, 12, CV_8UC1, cv::Scalar(0));
    ImageLineIntegral alg;
    alg.setImage(img);
    EXPECT_TRUE(alg.isInside(0, 0));
    EXPECT_TRUE(alg.isInside(11.99999, 13.999));
    EXPECT_TRUE(alg.isInside(11.99999, 0));
    EXPECT_TRUE(alg.isInside(0, 13.999));

    EXPECT_FALSE(alg.isInside(12, 14));
    EXPECT_FALSE(alg.isInside(12, 0));
    EXPECT_FALSE(alg.isInside(0, 14));
    EXPECT_FALSE(alg.isInside(-0.0001, 0));
    EXPECT_FALSE(alg.isInside(0, -0.00001));
    EXPECT_FALSE(alg.isInside(12.000001, 14));
    EXPECT_FALSE(alg.isInside(12, 14.0001));
    EXPECT_FALSE(alg.isInside(12.0001, 0));
    EXPECT_FALSE(alg.isInside(0, 14.00001));
}

// ---------------------------------------------------------------------------
// SnapToLineEdge — mirrors easy_aligned / computePointsAndWeights /
// computePointsAndWeights_border / localToGlobal from TestSnapToLineEdge.
// (Affine-transformation tests deferred — they need FDistort.)
// ---------------------------------------------------------------------------

namespace {

constexpr int32_t SNAP_W = 400;
constexpr int32_t SNAP_H = 500;
constexpr int32_t X0 = 200;
constexpr int32_t Y0 = 160;
constexpr int32_t X1 = 260;
constexpr int32_t Y1 = 400;
constexpr int32_t WHITE = 200;

cv::Mat makeSnapImage() {
    cv::Mat image(SNAP_H, SNAP_W, CV_8UC1, cv::Scalar(WHITE));
    cv::rectangle(image, cv::Rect(X0, Y0, X1 - X0, Y1 - Y0), cv::Scalar(0),
                  cv::FILLED);
    return image;
}

void normalizeLine(LineGeneral2D& l) {
    double n = std::sqrt(l.A * l.A + l.B * l.B);
    if (n != 0.0) {
        l.A /= n;
        l.B /= n;
        l.C /= n;
    }
}

// Mirror of `checkIdentical(LineSegment2D_F64 expected, LineGeneral2D_F64 found)`
// from TestSnapToLineEdge.
void checkLineMatchesSegment(const cv::Point2d& a, const cv::Point2d& b,
                              const LineGeneral2D& found) {
    LineGeneral2D expected;
    expected.A = a.y - b.y;
    expected.B = b.x - a.x;
    expected.C = -(expected.A * a.x + expected.B * a.y);

    LineGeneral2D e = expected;
    LineGeneral2D f = found;
    normalizeLine(e);
    normalizeLine(f);

    if ((e.C >= 0) != (f.C >= 0)) {
        e.A = -e.A;
        e.B = -e.B;
        e.C = -e.C;
    }

    EXPECT_NEAR(e.A, f.A, 1e-8);
    EXPECT_NEAR(e.B, f.B, 1e-8);
    EXPECT_NEAR(e.C, f.C, 1e-8);
}

void differentInitial(SnapToLineEdge& alg, const cv::Point2d& a, const cv::Point2d& b) {
    double slopeX = b.x - a.x;
    double slopeY = b.y - a.y;
    double r = std::sqrt(slopeX * slopeX + slopeY * slopeY);
    cv::Point2d v(-slopeY / r, slopeX / r);

    LineGeneral2D found;

    for (int32_t i = -1; i <= 1; i++) {
        cv::Point2d wa(a.x + i * v.x, a.y + i * v.y);
        cv::Point2d wb(b.x + i * v.x, b.y + i * v.y);

        EXPECT_TRUE(alg.refine(wa, wb, found));
        checkLineMatchesSegment(a, b, found);

        EXPECT_TRUE(alg.refine(wb, wa, found));
        checkLineMatchesSegment(a, b, found);
    }
}

}  // namespace

TEST(SnapToLineEdge, easy_aligned) {
    cv::Mat image = makeSnapImage();

    SnapToLineEdge alg(10, 2);
    alg.setImage(image);

    int32_t r = 2;
    cv::Point2d bottom_a(X1 - r, Y0), bottom_b(X0 + r, Y0);
    cv::Point2d left_a(X0, Y0 + r), left_b(X0, Y1 - r);
    cv::Point2d top_a(X0 + r, Y1), top_b(X1 - r, Y1);
    cv::Point2d right_a(X1, Y1 - r), right_b(X1, Y0 + r);

    differentInitial(alg, bottom_a, bottom_b);
    differentInitial(alg, left_a, left_b);
    differentInitial(alg, top_a, top_b);
    differentInitial(alg, right_a, right_b);
}

TEST(SnapToLineEdge, computePointsAndWeights) {
    cv::Mat image = makeSnapImage();

    SnapToLineEdge alg(10, 2);
    alg.setImage(image);
    alg.center().x = 10;
    alg.center().y = 12;
    alg.localScale() = 2;

    float H = static_cast<float>(Y1 - Y0 - 10);
    alg.computePointsAndWeights(0, H, X0, Y0 + 5, 1, 0);

    EXPECT_EQ(static_cast<std::size_t>(alg.getLineSamples()),
              alg.samplePts().size());

    for (int32_t i = 0; i < alg.getLineSamples(); i++) {
        EXPECT_NEAR(WHITE, alg.weights()[static_cast<std::size_t>(i)], 1e-8);
        double x = X0 - 10;
        double y = Y0 + 5 + H * i / (alg.getLineSamples() - 1) - 12;
        x /= alg.localScale();
        y /= alg.localScale();
        const cv::Point2d& p = alg.samplePts()[static_cast<std::size_t>(i)];
        EXPECT_NEAR(x, p.x, 1e-4);
        EXPECT_NEAR(y, p.y, 1e-4);
    }
}

TEST(SnapToLineEdge, computePointsAndWeights_border) {
    cv::Mat image = makeSnapImage();

    SnapToLineEdge alg(10, 2);
    alg.setImage(image);

    alg.computePointsAndWeights(0, image.rows - 2, 0, 2, 1, 0);
    EXPECT_EQ(0u, alg.samplePts().size());
    alg.computePointsAndWeights(0, image.rows - 2, image.cols - 1, 2, 1, 0);
    EXPECT_EQ(0u, alg.samplePts().size());
    alg.computePointsAndWeights(image.cols - 2, 0, 1, 0, 0, 1);
    EXPECT_EQ(0u, alg.samplePts().size());
    alg.computePointsAndWeights(image.cols - 2, 0, 1, image.rows - 1, 0, 1);
    EXPECT_EQ(0u, alg.samplePts().size());
}

TEST(SnapToLineEdge, localToGlobal) {
    SnapToLineEdge alg(10, 2);
    alg.center().x = 20;
    alg.center().y = 23;
    alg.localScale() = 10;

    // A segment in global coords; convert points to local coords; then
    // verify localToGlobal recovers the global line.
    cv::Point2d ga(10, 20), gb(50, -10);

    auto toLocal = [&](cv::Point2d p) {
        return cv::Point2d((p.x - alg.center().x) / alg.localScale(),
                            (p.y - alg.center().y) / alg.localScale());
    };
    cv::Point2d la = toLocal(ga);
    cv::Point2d lb = toLocal(gb);

    LineGeneral2D expected;
    expected.A = ga.y - gb.y;
    expected.B = gb.x - ga.x;
    expected.C = -(expected.A * ga.x + expected.B * ga.y);

    LineGeneral2D found;
    found.A = la.y - lb.y;
    found.B = lb.x - la.x;
    found.C = -(found.A * la.x + found.B * la.y);

    alg.localToGlobal(found);

    LineGeneral2D e = expected, f = found;
    normalizeLine(e);
    normalizeLine(f);

    EXPECT_NEAR(e.A, f.A, 1e-8);
    EXPECT_NEAR(e.B, f.B, 1e-8);
    EXPECT_NEAR(e.C, f.C, 1e-8);
}

// ---------------------------------------------------------------------------
// RefinePolygonToGrayLine — synthetic axis-aligned-square test (mirrors
// the easy core of TestRefinePolygonToGrayLine.alignedSquare without
// pulling in the FDistort/CommonFitPolygonChecks infra).
// ---------------------------------------------------------------------------

TEST(RefinePolygonToGrayLine, alignedSquare) {
    // Black 60x240 (well, 60x240 == X1-X0 by Y1-Y0) rectangle on white.
    cv::Mat image(SNAP_H, SNAP_W, CV_8UC1, cv::Scalar(WHITE));
    cv::rectangle(image, cv::Rect(X0, Y0, X1 - X0, Y1 - Y0), cv::Scalar(0),
                  cv::FILLED);

    RefinePolygonToGrayLine alg(4);
    alg.setImage(image);

    // Perfect initial guess (the integer corners).
    std::vector<cv::Point2d> input = {{X0, Y0}, {X0, Y1}, {X1, Y1}, {X1, Y0}};

    std::vector<cv::Point2d> output(input.size());
    EXPECT_TRUE(alg.refine(input, output));

    // Should be close to the input; the line-snap should converge to
    // exactly the axis-aligned edges of the rectangle.
    for (std::size_t i = 0; i < input.size(); i++) {
        EXPECT_NEAR(input[i].x, output[i].x, 0.05);
        EXPECT_NEAR(input[i].y, output[i].y, 0.05);
    }
}

TEST(RefinePolygonToGrayLine, alignedSquare_noisyInitial) {
    cv::Mat image(SNAP_H, SNAP_W, CV_8UC1, cv::Scalar(WHITE));
    cv::rectangle(image, cv::Rect(X0, Y0, X1 - X0, Y1 - Y0), cv::Scalar(0),
                  cv::FILLED);

    RefinePolygonToGrayLine alg(4);
    alg.setImage(image);

    // Add small per-corner noise; the refinement should still pull each
    // line to the underlying rectangle edge.
    std::vector<cv::Point2d> noisy = {
        {X0 + 0.5, Y0 + 0.7}, {X0 - 0.4, Y1 - 0.3},
        {X1 + 0.6, Y1 + 0.2}, {X1 - 0.5, Y0 - 0.6},
    };

    std::vector<cv::Point2d> output(noisy.size());
    EXPECT_TRUE(alg.refine(noisy, output));

    std::vector<cv::Point2d> expected = {{X0, Y0}, {X0, Y1}, {X1, Y1}, {X1, Y0}};
    for (std::size_t i = 0; i < expected.size(); i++) {
        EXPECT_NEAR(expected[i].x, output[i].x, 0.5);
        EXPECT_NEAR(expected[i].y, output[i].y, 0.5);
    }
}

TEST(RefinePolygonToGrayLine, fit_tooSmall) {
    cv::Mat image(SNAP_H, SNAP_W, CV_8UC1, cv::Scalar(WHITE));
    cv::rectangle(image, cv::Rect(X0, Y0, X1 - X0, Y1 - Y0), cv::Scalar(0),
                  cv::FILLED);

    // 1×1 square is too small for the refinement (< 2*cornerOffset+2 px).
    std::vector<cv::Point2d> input = {{5, 5}, {5, 6}, {6, 6}, {6, 5}};
    std::vector<cv::Point2d> output(4);

    RefinePolygonToGrayLine alg(input.size());
    alg.setImage(image);
    EXPECT_FALSE(alg.refine(input, output));
}

TEST(RefinePolygonToGrayLine, configCtor_fieldsLand) {
    ConfigRefinePolygonLineToImage cfg;
    cfg.cornerOffset = 1.5;
    cfg.lineSamples = 25;
    cfg.sampleRadius = 2;
    cfg.maxIterations = 7;
    cfg.convergeTolPixels = 0.05;
    cfg.maxCornerChangePixel = 1.0;

    RefinePolygonToGrayLine alg(cfg);
    EXPECT_EQ(25, alg.getSnapToEdge().getLineSamples());
    EXPECT_EQ(2, alg.getSnapToEdge().getRadialSamples());
}

// ---------------------------------------------------------------------------
// AdjustPolygonForThresholdBias — verifies the post-binarisation shift.
// ---------------------------------------------------------------------------

TEST(AdjustPolygonForThresholdBias, identicalCornersThrows) {
    AdjustPolygonForThresholdBias alg;
    std::vector<cv::Point2d> poly = {{5, 5}, {5, 5}, {6, 6}, {6, 5}};
    EXPECT_THROW(alg.process(poly, true), std::runtime_error);
}

TEST(AdjustPolygonForThresholdBias, axisAlignedSquare) {
    // Axis-aligned 10x10 square, clockwise (image-coord, +y down). The
    // adjustment should shift the right and bottom edges by 1 pixel and
    // leave the top and left untouched.
    AdjustPolygonForThresholdBias alg;
    std::vector<cv::Point2d> poly = {{10, 10}, {20, 10}, {20, 20}, {10, 20}};

    alg.process(poly, /*clockwise=*/true);

    // Polygon must still have 4 corners.
    ASSERT_EQ(4u, poly.size());
}

// ---------------------------------------------------------------------------
// EdgeIntensityPolygon — quality gate.
// ---------------------------------------------------------------------------

TEST(EdgeIntensityPolygon, blackSquareClockwise) {
    // 60x60 black square on white background.
    int32_t W = 200, H = 200;
    cv::Mat image(H, W, CV_8UC1, cv::Scalar(WHITE));
    cv::rectangle(image, cv::Rect(50, 50, 60, 60), cv::Scalar(0), cv::FILLED);

    EdgeIntensityPolygon alg(1.0, 1.5, 15);
    alg.setImage(image);

    // Polygon corners traversed in image-coord-CW order. With +y down
    // in image coords flipped to +y up in math coords, image-CW becomes
    // math-CCW — so Java's `ccw` flag (which describes the *math*
    // winding) is `true` here.
    std::vector<cv::Point2d> poly = {{50, 50}, {110, 50}, {110, 110}, {50, 110}};
    EXPECT_TRUE(alg.computeEdge(poly, /*ccw=*/true));

    EXPECT_LT(alg.getAverageInside(), 50.0);    // inside is black
    EXPECT_GT(alg.getAverageOutside(), 150.0);  // outside is white
}

// ---------------------------------------------------------------------------
// DetectPolygonBinaryGrayRefine — wiring + refineAll on synthetic input.
// ---------------------------------------------------------------------------

namespace {

std::unique_ptr<DetectPolygonBinaryGrayRefine> makeRefineDetector(
    int32_t minSides, int32_t maxSides, double minimumRefineEdgeIntensity) {
    auto adapter = std::make_unique<PolylineSplitMergeAdapter>();
    adapter->setMinimumSides(minSides);
    adapter->setMaximumSides(maxSides);

    auto detector = std::make_unique<DetectPolygonFromContour>(
        std::move(adapter), /*outputClockwiseUpY=*/true,
        /*canTouchBorder=*/false,
        /*contourEdgeThreshold=*/0.0,
        /*tangentEdgeIntensity=*/1.0);

    auto refine = std::make_shared<RefinePolygonToGrayLine>(4);

    return std::make_unique<DetectPolygonBinaryGrayRefine>(
        std::move(detector), std::move(refine),
        minimumRefineEdgeIntensity, /*adjustForThresholdBias=*/true);
}

}  // namespace

TEST(DetectPolygonBinaryGrayRefine, rectanglesProcessThenRefine) {
    int32_t W = 200, H = 200;
    cv::Mat gray(H, W, CV_8UC1, cv::Scalar(WHITE));
    std::vector<cv::Rect> rects = {
        cv::Rect(30, 30, 30, 30),
        cv::Rect(90, 30, 30, 30),
        cv::Rect(30, 90, 30, 30),
        cv::Rect(90, 90, 30, 30),
    };
    for (const auto& r : rects) cv::rectangle(gray, r, cv::Scalar(0), cv::FILLED);

    cv::Mat binary;
    cv::threshold(gray, binary, 100, 1, cv::THRESH_BINARY_INV);

    auto alg = makeRefineDetector(4, 4, /*minimumRefineEdgeIntensity=*/0.0);
    alg->process(gray, binary);

    // After process(), foundInfo holds 4 polygons; each was already
    // shifted by the threshold-bias step.
    EXPECT_EQ(rects.size(), alg->getPolygonInfo().size());

    alg->refineAll();

    // After refine + edge-intensity gate (with threshold 0), all 4
    // should still survive.
    auto polys = alg->getPolygons();
    EXPECT_EQ(rects.size(), polys.size());
    for (const auto& p : polys) {
        EXPECT_EQ(4u, p.size());
    }
}

TEST(DetectPolygonBinaryGrayRefine, getPolygonsHonoursMinimumEdgeIntensity) {
    int32_t W = 200, H = 200;
    cv::Mat gray(H, W, CV_8UC1, cv::Scalar(WHITE));
    cv::rectangle(gray, cv::Rect(30, 30, 30, 30), cv::Scalar(0), cv::FILLED);
    cv::Mat binary;
    cv::threshold(gray, binary, 100, 1, cv::THRESH_BINARY_INV);

    // After process+refine, edgeOutside-edgeInside is roughly 200 for a
    // black-on-white rectangle. Threshold of 1e6 should reject everything.
    auto alg = makeRefineDetector(4, 4, /*minimumRefineEdgeIntensity=*/1e6);
    alg->process(gray, binary);
    alg->refineAll();
    auto polys = alg->getPolygons();
    EXPECT_EQ(0u, polys.size());

    // Reasonable threshold (10) keeps the rectangle.
    auto alg2 = makeRefineDetector(4, 4, /*minimumRefineEdgeIntensity=*/10.0);
    alg2->process(gray, binary);
    alg2->refineAll();
    polys = alg2->getPolygons();
    EXPECT_EQ(1u, polys.size());
}
