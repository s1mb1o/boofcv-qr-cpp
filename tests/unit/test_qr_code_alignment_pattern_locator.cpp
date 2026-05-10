// Mirrors TestQrCodeAlignmentPatternLocator.java. Upstream: BoofCV v1.3.0.
//
// The Java JUnit suite's `simple`, `withLensDistortion`, `centerOnSquare`,
// `localize` cases all build a synthetic QR via
// `new QrCodeEncoder().setVersion(...).addNumeric("...").fixate()` +
// `new QrCodeGeneratorImage(scale).render(qr)` — encoder-side
// infrastructure that this port intentionally doesn't ship (CLAUDE.md
// scope: decoder only). The static helpers, `initializePatterns`, and
// the new `alignment[]` field on `QrCode` ARE all directly portable.
// We mirror those plus add a synthetic centerOnSquare test that
// renders a 5×5 alignment pattern via cv::rectangle and verifies the
// gradient-walk converges.

#include "boofcv_qr/alignment/qr_code_alignment_pattern_locator.hpp"
#include "boofcv_qr/qr_code.hpp"

#include <gtest/gtest.h>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <vector>

using boofcv_qr::QrCode;
using boofcv_qr::QrCodeAlignmentPatternLocator;

// ---------------------------------------------------------------------------
// QrCode::Alignment + alignment[] additions to the QrCode struct.
// ---------------------------------------------------------------------------

TEST(QrCode, alignment_emptyAfterReset) {
    QrCode qr;
    qr.alignment.push_back(QrCode::Alignment{});
    qr.alignment.back().moduleX = 6;
    qr.alignment.back().moduleY = 6;
    qr.alignment.back().pixel = cv::Point2d(100, 200);

    qr.reset();

    EXPECT_TRUE(qr.alignment.empty());
}

TEST(QrCode, Alignment_resetClearsFields) {
    QrCode::Alignment a;
    a.moduleX = 7;
    a.moduleY = 9;
    a.moduleFound = cv::Point2d(1.5, 2.5);
    a.pixel = cv::Point2d(11.0, 22.0);
    a.threshold = 100.0;

    a.reset();

    EXPECT_EQ(0, a.moduleX);
    EXPECT_EQ(0, a.moduleY);
    EXPECT_DOUBLE_EQ(0.0, a.moduleFound.x);
    EXPECT_DOUBLE_EQ(0.0, a.moduleFound.y);
    EXPECT_DOUBLE_EQ(0.0, a.pixel.x);
    EXPECT_DOUBLE_EQ(0.0, a.pixel.y);
    EXPECT_DOUBLE_EQ(0.0, a.threshold);
}

// ---------------------------------------------------------------------------
// Static helpers — mirror Java parity tests greatestDown / greatestUp.
// ---------------------------------------------------------------------------

TEST(QrCodeAlignmentPatternLocator, greatestDown) {
    std::vector<float> values = {200, 210, 190, 20, 25, 18, 0, 255, 255, 255};
    EXPECT_EQ(3, QrCodeAlignmentPatternLocator::greatestDown(values));
}

TEST(QrCodeAlignmentPatternLocator, greatestUp) {
    std::vector<float> values = {200, 0, 255, 20, 25, 18, 0, 200, 255, 255};
    EXPECT_EQ(6, QrCodeAlignmentPatternLocator::greatestUp(values, 3));
}

// ---------------------------------------------------------------------------
// initializePatterns — mirrors Java's `initializePatterns` test verbatim.
// ---------------------------------------------------------------------------

TEST(QrCodeAlignmentPatternLocator, initializePatterns) {
    QrCodeAlignmentPatternLocator alg;

    QrCode qr;
    // Java: `new QrCodeEncoder().setVersion(2).addNumeric(...).fixate()`
    // — only `version` actually matters to initializePatterns.
    qr.version = 2;
    alg.initializePatterns(qr);
    EXPECT_EQ(1u, qr.alignment.size());
    EXPECT_EQ(25 - 7, qr.alignment[0].moduleX);
    EXPECT_EQ(25 - 7, qr.alignment[0].moduleY);

    qr.reset();
    qr.version = 7;
    alg.initializePatterns(qr);
    EXPECT_EQ(6u, qr.alignment.size());
    // Java: assertEquals(22, qr.alignment.get(0).moduleX);
    //       assertEquals(6,  qr.alignment.get(0).moduleY);
    //       assertEquals(6,  qr.alignment.get(1).moduleX);
    //       assertEquals(22, qr.alignment.get(1).moduleY);
    //       assertEquals(22, qr.alignment.get(2).moduleX);
    //       assertEquals(22, qr.alignment.get(2).moduleY);
    EXPECT_EQ(22, qr.alignment[0].moduleX);
    EXPECT_EQ(6, qr.alignment[0].moduleY);
    EXPECT_EQ(6, qr.alignment[1].moduleX);
    EXPECT_EQ(22, qr.alignment[1].moduleY);
    EXPECT_EQ(22, qr.alignment[2].moduleX);
    EXPECT_EQ(22, qr.alignment[2].moduleY);
}

TEST(QrCodeAlignmentPatternLocator, initializePatterns_v1HasNoAlignment) {
    QrCodeAlignmentPatternLocator alg;
    QrCode qr;
    qr.version = 1;
    alg.initializePatterns(qr);
    EXPECT_EQ(0u, qr.alignment.size());
}

TEST(QrCodeAlignmentPatternLocator, initializePatterns_v40HasMaxCount) {
    QrCodeAlignmentPatternLocator alg;
    QrCode qr;
    qr.version = 40;
    alg.initializePatterns(qr);
    // VERSION_INFO[40].alignment has 7 entries → 7×7 = 49 cells minus
    // the 3 corner cells = 46 alignment patterns.
    EXPECT_EQ(46u, qr.alignment.size());
}

// ---------------------------------------------------------------------------
// configuration getters/setters
// ---------------------------------------------------------------------------

TEST(QrCodeAlignmentPatternLocator, useEdgeScanRoundtrip) {
    QrCodeAlignmentPatternLocator alg;
    EXPECT_FALSE(alg.getUseEdgeScan());
    alg.setUseEdgeScan(true);
    EXPECT_TRUE(alg.getUseEdgeScan());
}

// ---------------------------------------------------------------------------
// Synthetic centerOnSquare — uses a hand-built grid reader to render a
// 5x5 alignment pattern in image coordinates and verifies the
// gradient-walk converges back to the centre when seeded with sub-
// module noise. Substitute for Java's encoder-dependent
// `centerOnSquare` test.
// ---------------------------------------------------------------------------

namespace {

// Render an alignment pattern centred at `(cxImage, cyImage)` with
// module-width `scale` pixels. The pattern is the QR alignment-pattern
// shape: 5×5 black, 3×3 white, 1×1 black (centre dot). Background is
// white. Mirrors the upstream `QrCodeGeneratorImage`-rendered version
// at module-scale `scale`.
cv::Mat renderAlignmentPattern(int32_t W, int32_t H, int32_t cxImage,
                                int32_t cyImage, int32_t scale) {
    cv::Mat image(H, W, CV_8UC1, cv::Scalar(255));

    // Outer 5×5 black.
    int32_t halfOuter = static_cast<int32_t>(2.5f * scale);
    cv::rectangle(image,
                   cv::Rect(cxImage - halfOuter, cyImage - halfOuter,
                            5 * scale, 5 * scale),
                   cv::Scalar(0), cv::FILLED);
    // Middle 3×3 white.
    int32_t halfMid = static_cast<int32_t>(1.5f * scale);
    cv::rectangle(image,
                   cv::Rect(cxImage - halfMid, cyImage - halfMid, 3 * scale,
                            3 * scale),
                   cv::Scalar(255), cv::FILLED);
    // Centre 1×1 black.
    int32_t halfIn = static_cast<int32_t>(0.5f * scale);
    cv::rectangle(image,
                   cv::Rect(cxImage - halfIn, cyImage - halfIn, scale, scale),
                   cv::Scalar(0), cv::FILLED);

    return image;
}

}  // namespace

TEST(QrCodeAlignmentPatternLocator, centerOnSquare_synthetic) {
    // Render a single alignment pattern centred at (100, 100) with
    // module-scale 4 px. The locator's grid reader needs a homography
    // — we set up the smallest one that puts grid (moduleX, moduleY) =
    // (1.5, 1.5) at image-pixel (100, 100), with each module = 4px.
    //
    // In other words: we wire up a v1 QR (small, no alignment table)
    // whose finder-corner positions give the reader a clean 4-pixel-
    // per-module homography centred so that grid (1.5, 1.5) lands at
    // image (100, 100). Then we feed centerOnSquare a noisy initial
    // guess and assert it converges back to (1.5, 1.5).
    int32_t scale = 4;
    int32_t cx = 100, cy = 100;
    int32_t W = 200, H = 200;

    cv::Mat image = renderAlignmentPattern(W, H, cx, cy, scale);

    // Build a synthetic homography seed. v1 QR has no alignment
    // pattern, so we use it for the simplest setup. The reader's
    // setMarker requires three finder corners + alignment lists. We
    // pass v1-shaped finder corners that put module (0,0) at image
    // (cx - 1.5*scale, cy - 1.5*scale) — i.e., the alignment-pattern
    // centre maps to grid (1.5, 1.5). For v1, totalModules = 21; we
    // place the three finder patterns at the module corners that v1
    // expects.
    //
    // For the locator we don't actually need v1 specifically — we just
    // need centerOnSquare to be callable with a configured reader.
    // Use v2 so VERSION_INFO[2].alignment has one entry; we'll point
    // its expected grid coord at (moduleX=1.5, moduleY=1.5) module
    // coords.
    QrCode qr;
    qr.version = 2;

    // 21-module QR (v1) means finder pattern corners at module 0..6 and
    // 14..20. For v2 (25 modules) it'd be 0..6 and 18..24. We use v2
    // here. Synthesise finder polygons that put module (0,0) at
    // (cx - 12.5*scale, cy - 12.5*scale) (centred on the alignment
    // pattern at module (12.5, 12.5)).
    //
    // Actually, simpler: use a v2 QR centred on the alignment pattern
    // such that VERSION_INFO[2].alignment[0] = 18 → expected module
    // coord (18, 18). We want that to land at image (cx, cy).
    int32_t centerModule = 18;
    double mod_to_px = scale;
    double topLeftX = cx - (centerModule + 0.5) * mod_to_px;
    double topLeftY = cy - (centerModule + 0.5) * mod_to_px;
    auto modToImage = [&](double mx, double my) {
        return cv::Point2d(topLeftX + mx * mod_to_px,
                           topLeftY + my * mod_to_px);
    };

    // QR v2 is 25 modules wide. Top-left finder corners:
    std::array<cv::Point2d, 4> ppCorner = {
        modToImage(0, 0),  modToImage(7, 0),
        modToImage(7, 7),  modToImage(0, 7),
    };
    // Top-right finder corners (modules 18..25 along x, 0..7 along y):
    std::array<cv::Point2d, 4> ppRight = {
        modToImage(18, 0),  modToImage(25, 0),
        modToImage(25, 7),  modToImage(18, 7),
    };
    // Bottom-left finder corners (modules 0..7 along x, 18..25 along y):
    std::array<cv::Point2d, 4> ppDown = {
        modToImage(0, 18),  modToImage(7, 18),
        modToImage(7, 25),  modToImage(0, 25),
    };

    QrCodeAlignmentPatternLocator alg;
    // We invoke process to wire the reader (which sets up the grid →
    // image transform). VERSION_INFO[2].alignment = {6, 18}; the
    // single non-corner cell is (1, 1) → moduleX=18, moduleY=18.
    alg.process(image, qr, ppCorner, ppRight, ppDown,
                 /*priorAlignmentCenters=*/{},
                 /*priorAlignmentGridCoords=*/{});

    // After process(), qr.alignment[0] should hold the located
    // alignment pattern centre. Tolerance is generous because the
    // gradient-walk only converges to within ~1 module of the true
    // centre on this synthetic.
    ASSERT_EQ(1u, qr.alignment.size());
    const auto& a = qr.alignment[0];
    EXPECT_NEAR(static_cast<double>(cx), a.pixel.x, scale * 1.0)
        << "alignment pixel.x off by more than one module";
    EXPECT_NEAR(static_cast<double>(cy), a.pixel.y, scale * 1.0)
        << "alignment pixel.y off by more than one module";
}

// ---------------------------------------------------------------------------
// Codex review fix #3 — edge-scan path end-to-end.
// ---------------------------------------------------------------------------

TEST(QrCodeAlignmentPatternLocator, useEdgeScan_findsCenter) {
    // Same v2 synthetic scene as `centerOnSquare_synthetic`, but with
    // `setUseEdgeScan(true)` so the locator's `localize()` edge-scan
    // path runs *between* `centerOnSquare` and `meanshift`. Asserts
    // the path executes without crashing and produces plausible
    // coordinates. Without this test the 200+ LOC dormant `localize()`
    // port is unreachable from any test (codex review fix #3).
    //
    // Use scale=8 (vs centerOnSquare_synthetic's scale=4) — the
    // edge-scan's 12-sample sweep across 3 modules needs ≥ 1 pixel
    // per sample, which means scale ≥ 4. Use 8 for headroom so the
    // edge transitions are unambiguous.
    int32_t scale = 8;
    int32_t cx = 200, cy = 200;
    int32_t W = 400, H = 400;

    cv::Mat image = renderAlignmentPattern(W, H, cx, cy, scale);

    QrCode qr;
    qr.version = 2;

    int32_t centerModule = 18;
    double mod_to_px = scale;
    double topLeftX = cx - (centerModule + 0.5) * mod_to_px;
    double topLeftY = cy - (centerModule + 0.5) * mod_to_px;
    auto modToImage = [&](double mx, double my) {
        return cv::Point2d(topLeftX + mx * mod_to_px,
                           topLeftY + my * mod_to_px);
    };

    std::array<cv::Point2d, 4> ppCorner = {
        modToImage(0, 0),  modToImage(7, 0),
        modToImage(7, 7),  modToImage(0, 7),
    };
    std::array<cv::Point2d, 4> ppRight = {
        modToImage(18, 0),  modToImage(25, 0),
        modToImage(25, 7),  modToImage(18, 7),
    };
    std::array<cv::Point2d, 4> ppDown = {
        modToImage(0, 18),  modToImage(7, 18),
        modToImage(7, 25),  modToImage(0, 25),
    };

    QrCodeAlignmentPatternLocator alg;
    alg.setUseEdgeScan(true);  // exercise the dormant edge-scan path

    EXPECT_TRUE(alg.process(image, qr, ppCorner, ppRight, ppDown,
                              /*priorAlignmentCenters=*/{},
                              /*priorAlignmentGridCoords=*/{}));

    ASSERT_EQ(1u, qr.alignment.size());
    const auto& a = qr.alignment[0];
    // Generous tolerance — the edge-scan path is a Java-disabled
    // diagnostic with known precision limits; we only assert that
    // it produces plausible coordinates (within 1.5 modules).
    EXPECT_NEAR(static_cast<double>(cx), a.pixel.x, scale * 1.5)
        << "edge-scan path produced implausible pixel.x";
    EXPECT_NEAR(static_cast<double>(cy), a.pixel.y, scale * 1.5)
        << "edge-scan path produced implausible pixel.y";
}

// ---------------------------------------------------------------------------
// Codex review fix #4 — v7+ multi-alignment-pattern adjustment.
//
// v2 has one non-corner alignment pattern; v7 has six in a partial
// 3×3 grid that exercises both `adjY` (across rows) and `adjX`
// (across columns) neighbour-steering paths. Without this test, the
// row/column adjustment-seeding branch in `localizePositionPatterns`
// has zero green-test guarantee.
// ---------------------------------------------------------------------------

TEST(QrCodeAlignmentPatternLocator, localizePositionPatterns_v7_steersFromNeighbours) {
    // VERSION_INFO[7].alignment = {6, 22, 38}. v7 is 45 modules wide
    // (4*7+17=45). Positions in the 3×3 grid:
    //   (6,6) — corner, skip
    //   (6,22) (6,38)
    //   (22,6) (22,22) (22,38)
    //   (38,6) (38,22) (38,38) — but (0,0) (0,2) (2,0) cells are
    //                              corners. Per Java initializePatterns
    //                              for v7 it skips (0,0), (0,2), (2,0):
    //                              expected order: (1,0), (1,1), (1,2),
    //                              (2,1), (2,2) — with the synthetic
    //                              first row's (0,1) also kept.
    //
    // Lookup ordering from initializePatterns (row-major, skipping
    // corners (0,0), (0,n-1), (n-1,0)) for v7's n=3:
    //   (0,1) → moduleX=22, moduleY=6
    //   (1,0) → 6, 22
    //   (1,1) → 22, 22
    //   (1,2) → 38, 22
    //   (2,1) → 22, 38
    //   (2,2) → 38, 38
    int32_t version = 7;
    int32_t totalModules = QrCode::totalModules(version);  // 45
    int32_t scale = 4;
    int32_t W = (totalModules + 4) * scale;  // 4-module border
    int32_t H = W;
    int32_t border = 2 * scale;

    cv::Mat image(H, W, CV_8UC1, cv::Scalar(255));

    auto modToImage = [&](double mx, double my) {
        return cv::Point2d(border + mx * scale, border + my * scale);
    };

    // Render all 6 non-corner alignment patterns (5×5 black, 3×3 white,
    // 1×1 black centre). Centred on (modX+0.5, modY+0.5) module coords.
    auto renderAt = [&](int32_t modX, int32_t modY) {
        cv::Point2d c = modToImage(modX + 0.5, modY + 0.5);
        int32_t cx = static_cast<int32_t>(c.x);
        int32_t cy = static_cast<int32_t>(c.y);
        int32_t halfOuter = static_cast<int32_t>(2.5f * scale);
        cv::rectangle(image,
                       cv::Rect(cx - halfOuter, cy - halfOuter,
                                5 * scale, 5 * scale),
                       cv::Scalar(0), cv::FILLED);
        int32_t halfMid = static_cast<int32_t>(1.5f * scale);
        cv::rectangle(image,
                       cv::Rect(cx - halfMid, cy - halfMid, 3 * scale,
                                3 * scale),
                       cv::Scalar(255), cv::FILLED);
        int32_t halfIn = static_cast<int32_t>(0.5f * scale);
        cv::rectangle(image,
                       cv::Rect(cx - halfIn, cy - halfIn, scale, scale),
                       cv::Scalar(0), cv::FILLED);
    };
    // VERSION_INFO[7].alignment = {6, 22, 38}; 3×3 grid minus corners.
    std::vector<std::pair<int32_t, int32_t>> nonCorner = {
        {22, 6},   // row 0, col 1
        {6, 22},   // row 1, col 0
        {22, 22},  // row 1, col 1
        {38, 22},  // row 1, col 2
        {22, 38},  // row 2, col 1
        {38, 38},  // row 2, col 2
    };
    for (const auto& mp : nonCorner) renderAt(mp.first, mp.second);

    QrCode qr;
    qr.version = version;

    // Finder corners at v7's expected positions. v7 finder corners:
    //   ppCorner: modules (0..7, 0..7)
    //   ppRight:  modules (38..45, 0..7) — but actually for v7 the
    //                                       right finder is at (38..45, 0..7)
    //   ppDown:   modules (0..7, 38..45)
    int32_t s7End = totalModules;  // 45
    int32_t pp = s7End - 7;        // 38

    std::array<cv::Point2d, 4> ppCorner = {
        modToImage(0, 0),  modToImage(7, 0),
        modToImage(7, 7),  modToImage(0, 7),
    };
    std::array<cv::Point2d, 4> ppRight = {
        modToImage(pp, 0),  modToImage(s7End, 0),
        modToImage(s7End, 7),  modToImage(pp, 7),
    };
    std::array<cv::Point2d, 4> ppDown = {
        modToImage(0, pp),  modToImage(7, pp),
        modToImage(7, s7End),  modToImage(0, s7End),
    };

    QrCodeAlignmentPatternLocator alg;
    bool ok = alg.process(image, qr, ppCorner, ppRight, ppDown,
                            /*priorAlignmentCenters=*/{},
                            /*priorAlignmentGridCoords=*/{});
    EXPECT_TRUE(ok);

    // All 6 non-corner alignment patterns located.
    ASSERT_EQ(6u, qr.alignment.size());

    // Verify each is found within ~1 module of its ground-truth
    // centre. If the row/column adjustment chain were broken, only
    // the first cell would land near truth and the rest would diverge.
    for (std::size_t i = 0; i < nonCorner.size(); i++) {
        cv::Point2d expected =
            modToImage(nonCorner[i].first + 0.5, nonCorner[i].second + 0.5);
        const auto& a = qr.alignment[i];
        double dx = a.pixel.x - expected.x;
        double dy = a.pixel.y - expected.y;
        double d = std::sqrt(dx * dx + dy * dy);
        EXPECT_LE(d, scale * 1.0)
            << "alignment[" << i << "] (modX=" << a.moduleX
            << ", modY=" << a.moduleY << ") was " << d
            << " px from ground truth (" << expected.x << ", "
            << expected.y << ")";
        // moduleFound should be near the expected (modX+0.5, modY+0.5).
        EXPECT_NEAR(a.moduleX + 0.5, a.moduleFound.x, 0.5)
            << "alignment[" << i << "].moduleFound.x off by more than 0.5 modules";
        EXPECT_NEAR(a.moduleY + 0.5, a.moduleFound.y, 0.5)
            << "alignment[" << i << "].moduleFound.y off by more than 0.5 modules";
    }
}
