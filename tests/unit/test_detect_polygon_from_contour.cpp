// Mirrors TestDetectPolygonFromContour.java + TestContourEdgeIntensity.java.
// Upstream: BoofCV v1.3.0.
//
// The full Java JUnit suite for DetectPolygonFromContour relies on
// FactoryShapeDetector / FactoryThresholdBinary / CommonFitPolygonChecks —
// upstream factory infrastructure we don't pull in. We mirror the unit-
// level cases that exercise package-private methods directly
// (`touchesBorder`, `determineCornersOnBorder`, `flip`) verbatim, plus
// synthetic end-to-end rectangle and triangle detection cases that are
// equivalent to the Java tests but built from `cv::rectangle` /
// `cv::fillPoly` rather than BoofCV's image factory.

#include "boofcv_qr/polygon/detect_polygon_from_contour.hpp"

#include <gtest/gtest.h>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <memory>
#include <vector>

using boofcv_qr::ContourEdgeIntensity;
using boofcv_qr::DetectPolygonFromContour;
using boofcv_qr::PolylineSplitMergeAdapter;

namespace {

std::unique_ptr<DetectPolygonFromContour> makeDetector(int32_t minSides,
                                                       int32_t maxSides,
                                                       bool canTouchBorder = false) {
    auto adapter = std::make_unique<PolylineSplitMergeAdapter>();
    adapter->setMinimumSides(minSides);
    adapter->setMaximumSides(maxSides);

    return std::make_unique<DetectPolygonFromContour>(
        std::move(adapter),
        /*outputClockwiseUpY*/ true,
        /*canTouchBorder*/ canTouchBorder,
        /*contourEdgeThreshold*/ 0.0,  // disabled in basic tests
        /*tangentEdgeIntensity*/ 1.0);
}

// Render a black rectangle on a white CV_8UC1 mat. Returns the (gray, binary) pair.
//
// `binary` follows CLAUDE.md "Binary image convention" — 0/1 with foreground
// (dark module) = 1.
std::pair<cv::Mat, cv::Mat> renderBlackRect(int32_t W, int32_t H, cv::Rect r) {
    cv::Mat gray(H, W, CV_8UC1, cv::Scalar(200));
    cv::rectangle(gray, r, cv::Scalar(0), cv::FILLED);

    cv::Mat binary;
    cv::threshold(gray, binary, 100, 1, cv::THRESH_BINARY_INV);
    return {gray, binary};
}

}  // namespace

// ---------------------------------------------------------------------------
// Java parity — DetectPolygonFromContour package-private methods
// ---------------------------------------------------------------------------

TEST(DetectPolygonFromContour, touchesBorder_false) {
    std::vector<cv::Point2i> contour;

    auto alg = makeDetector(4, 4);
    alg->setImageDims(20, 30);
    EXPECT_FALSE(alg->touchesBorder(contour));

    contour.push_back({10, 1});
    EXPECT_FALSE(alg->touchesBorder(contour));
    contour.push_back({10, 28});
    EXPECT_FALSE(alg->touchesBorder(contour));
    contour.push_back({1, 15});
    EXPECT_FALSE(alg->touchesBorder(contour));
    contour.push_back({18, 15});
    EXPECT_FALSE(alg->touchesBorder(contour));
}

TEST(DetectPolygonFromContour, touchesBorder_true) {
    std::vector<cv::Point2i> contour;
    auto alg = makeDetector(4, 4);
    alg->setImageDims(20, 30);

    // x == 0 boundary
    contour.push_back({0, 5});
    EXPECT_TRUE(alg->touchesBorder(contour));
    contour.clear();

    // y == 0 boundary
    contour.push_back({5, 0});
    EXPECT_TRUE(alg->touchesBorder(contour));
    contour.clear();

    // x == W-1 boundary
    contour.push_back({19, 5});
    EXPECT_TRUE(alg->touchesBorder(contour));
    contour.clear();

    // y == H-1 boundary
    contour.push_back({5, 29});
    EXPECT_TRUE(alg->touchesBorder(contour));
}

TEST(DetectPolygonFromContour, determineCornersOnBorder) {
    int32_t W = 200, H = 150;
    auto alg = makeDetector(4, 4);
    alg->setImageDims(W, H);

    std::vector<cv::Point2d> poly = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};

    std::vector<uint8_t> corners;
    alg->determineCornersOnBorder(poly, corners);

    EXPECT_EQ(4u, corners.size());

    EXPECT_TRUE(corners[0]);
    EXPECT_TRUE(corners[1]);
    EXPECT_FALSE(corners[2]);
    EXPECT_TRUE(corners[3]);
}

TEST(DetectPolygonFromContour, flip_static) {
    // Mirrors Java's flip behaviour: vertex 0 is preserved, 1..N-1 reversed.
    std::vector<int32_t> a = {0, 1, 2, 3, 4};
    DetectPolygonFromContour::flip(a);
    std::vector<int32_t> expected = {0, 4, 3, 2, 1};
    EXPECT_EQ(expected, a);

    // even N
    a = {0, 1, 2, 3};
    DetectPolygonFromContour::flip(a);
    expected = {0, 3, 2, 1};
    EXPECT_EQ(expected, a);

    // tiny
    a = {7};
    DetectPolygonFromContour::flip(a);
    expected = {7};
    EXPECT_EQ(expected, a);
}

// ---------------------------------------------------------------------------
// Synthetic end-to-end (substitute for the JUnit tests that depend on
// BoofCV's factory infrastructure)
// ---------------------------------------------------------------------------

TEST(DetectPolygonFromContour, easyTestNoDistortion_rect) {
    // 4 black rectangles on a white background. Each is 30x30, well-separated.
    std::vector<cv::Rect> rects = {
        cv::Rect(30, 30, 30, 30),
        cv::Rect(90, 30, 30, 30),
        cv::Rect(30, 90, 30, 30),
        cv::Rect(90, 90, 30, 30),
    };

    int32_t W = 200, H = 200;
    cv::Mat gray(H, W, CV_8UC1, cv::Scalar(200));
    for (const auto& r : rects) cv::rectangle(gray, r, cv::Scalar(0), cv::FILLED);
    cv::Mat binary;
    cv::threshold(gray, binary, 100, 1, cv::THRESH_BINARY_INV);

    auto alg = makeDetector(4, 4);
    alg->process(gray, binary);

    EXPECT_EQ(rects.size(), alg->getFoundInfo().size());
    for (const auto& info : alg->getFoundInfo()) {
        EXPECT_EQ(4u, info.polygon.size());
    }
}

TEST(DetectPolygonFromContour, rejectShape_circle) {
    int32_t W = 200, H = 220;
    cv::Mat gray(H, W, CV_8UC1, cv::Scalar(200));
    cv::ellipse(gray, cv::Point(75, 80), cv::Size(45, 50), 0, 0, 360,
                cv::Scalar(0), cv::FILLED);
    cv::Mat binary;
    cv::threshold(gray, binary, 100, 1, cv::THRESH_BINARY_INV);

    for (int32_t i = 3; i <= 6; i++) {
        auto alg = makeDetector(i, i);
        alg->process(gray, binary);
        EXPECT_EQ(0u, alg->getFoundInfo().size())
            << "expected circle to be rejected at side count " << i;
    }
}

TEST(DetectPolygonFromContour, detect_triangle) {
    int32_t W = 200, H = 220;
    cv::Mat gray(H, W, CV_8UC1, cv::Scalar(200));
    std::vector<cv::Point> tri = {{50, 30}, {120, 30}, {85, 90}};
    cv::fillPoly(gray, std::vector<std::vector<cv::Point>>{tri}, cv::Scalar(0));
    cv::Mat binary;
    cv::threshold(gray, binary, 100, 1, cv::THRESH_BINARY_INV);

    {
        auto alg = makeDetector(3, 3);
        alg->process(gray, binary);
        EXPECT_EQ(1u, alg->getFoundInfo().size());
    }
    for (int32_t i = 4; i <= 6; i++) {
        auto alg = makeDetector(i, i);
        alg->process(gray, binary);
        EXPECT_EQ(0u, alg->getFoundInfo().size())
            << "triangle should not match minSides=maxSides=" << i;
    }
}

TEST(DetectPolygonFromContour, internalContourPreserved) {
    // Asserts the C++ port's `saveInternalContours_=true` default. The
    // Java QR factory wires `polygonContour()` → `linearExternal()`
    // which sets `isSaveInternalContours=false`; this test confirms our
    // deviation is intentional. Downstream pipelines (the QR finder-
    // pattern detector at step 7c) rely on hole topology being available.
    int32_t W = 150, H = 150;
    cv::Mat gray(H, W, CV_8UC1, cv::Scalar(200));
    cv::circle(gray, cv::Point(75, 75), 40, cv::Scalar(0), cv::FILLED);
    cv::circle(gray, cv::Point(75, 75), 25, cv::Scalar(200), cv::FILLED);

    cv::Mat binary;
    cv::threshold(gray, binary, 100, 1, cv::THRESH_BINARY_INV);

    // No side-count restriction; we just want the contour topology.
    auto alg = makeDetector(3, 100, /*canTouchBorder=*/true);
    alg->process(gray, binary);

    // Whatever polygon survives, the source contour should have at
    // least one internal hole (the ring's inner boundary).
    bool sawInternal = false;
    for (const auto& info : alg->getFoundInfo()) {
        if (info.hasInternal()) sawInternal = true;
    }
    if (!alg->getFoundInfo().empty()) {
        EXPECT_TRUE(sawInternal);
    }
}

TEST(DetectPolygonFromContour, rejectTouchingBorder) {
    // Black rectangle that touches the top border.
    int32_t W = 200, H = 200;
    cv::Mat gray(H, W, CV_8UC1, cv::Scalar(200));
    cv::rectangle(gray, cv::Rect(10, 0, 50, 60), cv::Scalar(0), cv::FILLED);
    cv::Mat binary;
    cv::threshold(gray, binary, 100, 1, cv::THRESH_BINARY_INV);

    auto alg = makeDetector(4, 4, /*canTouchBorder=*/false);
    alg->process(gray, binary);
    EXPECT_EQ(0u, alg->getFoundInfo().size());

    auto alg2 = makeDetector(4, 4, /*canTouchBorder=*/true);
    alg2->process(gray, binary);
    EXPECT_EQ(1u, alg2->getFoundInfo().size());
}

// ---------------------------------------------------------------------------
// ContourEdgeIntensity — mirrors TestContourEdgeIntensity.java
// ---------------------------------------------------------------------------

namespace {

// Mirror of the Java helper `rectToContour(RectangleLength2D_I32)` —
// counter-clockwise traversal of a rectangle's pixel boundary.
std::vector<cv::Point2i> rectToContour(int32_t x0, int32_t y0, int32_t w, int32_t h) {
    std::vector<cv::Point2i> contour;
    int32_t x1 = x0 + w - 1;
    int32_t y1 = y0 + h - 1;

    for (int32_t i = 1; i < w; i++) contour.push_back({x0 + i, y0});
    for (int32_t i = 1; i < h; i++) contour.push_back({x1, y0 + i});
    for (int32_t i = 1; i < w; i++) contour.push_back({x1 - i, y1});
    for (int32_t i = 1; i < h; i++) contour.push_back({x0, y1 - i});
    return contour;
}

}  // namespace

TEST(ContourEdgeIntensity, simpleCase) {
    int32_t W = 200, H = 150;
    cv::Mat image(H, W, CV_8UC1, cv::Scalar(0));
    int32_t rx = 20, ry = 25, rw = 30, rh = 35;
    cv::rectangle(image, cv::Rect(rx, ry, rw, rh), cv::Scalar(200), cv::FILLED);

    auto contour = rectToContour(rx, ry, rw, rh);

    ContourEdgeIntensity alg(20, 2, 1.0);
    alg.setImage(image);

    alg.process(contour, true);
    EXPECT_LT(alg.getEdgeOutsideAverage(), 8.0f);
    EXPECT_GT(alg.getEdgeInsideAverage(), 195.0f);

    // CCW flag flips the inside/outside roles.
    alg.process(contour, false);
    EXPECT_GT(alg.getEdgeOutsideAverage(), 195.0f);
    EXPECT_LT(alg.getEdgeInsideAverage(), 8.0f);

    // Smaller contourSamples — should still classify correctly.
    ContourEdgeIntensity alg2(10, 2, 1.0);
    alg2.setImage(image);
    alg2.process(contour, true);
    EXPECT_LT(alg2.getEdgeOutsideAverage(), 8.0f);
    EXPECT_GT(alg2.getEdgeInsideAverage(), 195.0f);

    // Single tangent sample.
    ContourEdgeIntensity alg3(20, 1, 1.0);
    alg3.setImage(image);
    alg3.process(contour, true);
    EXPECT_LT(alg3.getEdgeOutsideAverage(), 8.0f);
    EXPECT_GT(alg3.getEdgeInsideAverage(), 195.0f);
}

TEST(ContourEdgeIntensity, smallContours) {
    int32_t W = 200, H = 150;
    cv::Mat image(H, W, CV_8UC1, cv::Scalar(0));
    int32_t rx = 20, ry = 25, rw = 5, rh = 4;
    cv::rectangle(image, cv::Rect(rx, ry, rw, rh), cv::Scalar(200), cv::FILLED);

    auto contour = rectToContour(rx, ry, rw, rh);

    ContourEdgeIntensity alg(30, 2, 1.0);
    alg.setImage(image);

    alg.process(contour, true);
    EXPECT_LT(alg.getEdgeOutsideAverage(), 8.0f);
    EXPECT_GT(alg.getEdgeInsideAverage(), 195.0f);
}

// ---------------------------------------------------------------------------
// Codex review fix #1 — full ConfigPolylineSplitMerge plumbed through
// adapter to the underlying PolylineSplitMerge.
// ---------------------------------------------------------------------------

TEST(PolylineSplitMergeAdapter, qrConfigDefaultsReachImpl) {
    // Mirror the QR-specific overrides from `ConfigQrCode.java`
    // (lines 93-105 in upstream). The previous adapter dropped these
    // silently — every override below MUST land on impl_.
    boofcv_qr::ConfigPolylineSplitMerge cfg;
    cfg.minimumSides = 4;
    cfg.maximumSides = 4;
    cfg.convex = true;
    cfg.loops = true;
    cfg.minimumSideLength = 2;        // QR override (default 2 in cfg, 10 in impl)
    cfg.cornerScorePenalty = 0.4;     // QR override (default 0.025)
    cfg.maxSideError =
        boofcv_qr::ConfigLength::relative(0.12, 3);  // QR override
    cfg.thresholdSideSplitScore = 0.2;
    cfg.maxNumberOfSideSamples = 50;
    cfg.convexTest = 2.5;
    cfg.extraConsider = boofcv_qr::ConfigLength::relative(1.0, 0);

    PolylineSplitMergeAdapter adapter(cfg);
    const auto& impl = adapter.impl();

    EXPECT_EQ(4, impl.getMinSides());
    EXPECT_EQ(4, impl.getMaxSides());
    EXPECT_TRUE(impl.isConvex());
    EXPECT_TRUE(impl.isLoops());
    EXPECT_EQ(2, impl.getMinimumSideLength());
    EXPECT_DOUBLE_EQ(0.4, impl.getCornerScorePenalty());
    EXPECT_DOUBLE_EQ(0.2, impl.getThresholdSideSplitScore());
    EXPECT_EQ(50, impl.getMaxNumberOfSideSamples());
    EXPECT_DOUBLE_EQ(2.5, impl.getConvexTest());
    EXPECT_DOUBLE_EQ(0.12, impl.getMaxSideError().fraction);
    EXPECT_DOUBLE_EQ(3.0, impl.getMaxSideError().length);
    EXPECT_DOUBLE_EQ(1.0, impl.getExtraConsider().fraction);
    EXPECT_DOUBLE_EQ(0.0, impl.getExtraConsider().length);
}

TEST(PolylineSplitMergeAdapter, defaultCtorMatchesUpstreamDefaults) {
    // Mirror the upstream `ConfigPolylineSplitMerge` default values
    // (which differ from `PolylineSplitMerge`'s own ctor defaults —
    // that's the original parity bug).
    PolylineSplitMergeAdapter adapter;
    const auto& impl = adapter.impl();

    EXPECT_EQ(2, impl.getMinimumSideLength());
    EXPECT_DOUBLE_EQ(0.025, impl.getCornerScorePenalty());
    EXPECT_DOUBLE_EQ(0.2, impl.getThresholdSideSplitScore());
    EXPECT_DOUBLE_EQ(2.5, impl.getConvexTest());
    EXPECT_DOUBLE_EQ(0.05, impl.getMaxSideError().fraction);
    EXPECT_DOUBLE_EQ(3.0, impl.getMaxSideError().length);
}

// ---------------------------------------------------------------------------
// Codex review fix #2 — contour rotated to canonical (topmost,
// leftmost) start pixel after winding reversal.
// ---------------------------------------------------------------------------

TEST(DetectPolygonFromContour, contourCanonicalStart) {
    // Black 30x30 square; the topmost row's leftmost pixel must be the
    // first contour element after reversal+rotation. BoofCV's
    // LinearContourLabelChang2004 is row-major, so the first scanned
    // foreground pixel is (x_min, y_min).
    int32_t W = 200, H = 200;
    int32_t x0 = 30, y0 = 30, x1 = 60, y1 = 60;  // inclusive corners
    cv::Mat gray(H, W, CV_8UC1, cv::Scalar(200));
    cv::rectangle(gray, cv::Rect(x0, y0, x1 - x0 + 1, y1 - y0 + 1),
                  cv::Scalar(0), cv::FILLED);
    cv::Mat binary;
    cv::threshold(gray, binary, 100, 1, cv::THRESH_BINARY_INV);

    auto alg = makeDetector(4, 4);
    alg->process(gray, binary);

    ASSERT_EQ(1u, alg->getFoundInfo().size());
    const auto& info = alg->getFoundInfo()[0];
    ASSERT_FALSE(info.contour.external.empty());
    const cv::Point2i& first = info.contour.external[0];

    // Expected: the topmost row's leftmost foreground pixel.
    EXPECT_EQ(x0, first.x);
    EXPECT_EQ(y0, first.y);
}

// ---------------------------------------------------------------------------
// Codex review fix #4 — saveInternalContours toggle.
// ---------------------------------------------------------------------------

TEST(DetectPolygonFromContour, saveInternalContoursToggle) {
    // Same donut as `internalContourPreserved`, but with
    // `setSaveInternalContours(false)` we expect no internal contours
    // in the output Contour.
    int32_t W = 150, H = 150;
    cv::Mat gray(H, W, CV_8UC1, cv::Scalar(200));
    cv::circle(gray, cv::Point(75, 75), 40, cv::Scalar(0), cv::FILLED);
    cv::circle(gray, cv::Point(75, 75), 25, cv::Scalar(200), cv::FILLED);

    cv::Mat binary;
    cv::threshold(gray, binary, 100, 1, cv::THRESH_BINARY_INV);

    auto alg = makeDetector(3, 100, /*canTouchBorder=*/true);
    alg->setSaveInternalContours(false);
    alg->process(gray, binary);

    for (const auto& info : alg->getFoundInfo()) {
        EXPECT_FALSE(info.hasInternal())
            << "internal contour should have been suppressed";
    }
}
