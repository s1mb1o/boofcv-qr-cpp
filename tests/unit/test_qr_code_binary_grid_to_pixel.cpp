// Subset of TestQrCodeBinaryGridToPixel.java — the upstream tests use
// QrCodeEncoder + QrCodeGeneratorImage to render a real QR. Those
// classes aren't part of our decoder-only port, so we substitute a
// synthetic 4-corner setup with hand-known image coordinates.

#include "boofcv_qr/qr_code.hpp"
#include "boofcv_qr/qr_code_binary_grid_to_pixel.hpp"

#include <gtest/gtest.h>

#include <array>

using boofcv_qr::QrCodeBinaryGridToPixel;

// Map a known finder-pattern square at image coordinates (border=20,
// 4px per module) to the canonical grid (0,0)-(7,0)-(7,7)-(0,7).
TEST(QrCodeBinaryGridToPixel, setTransformFromSquare) {
    constexpr double border = 20.0;
    constexpr double modPx = 4.0;
    std::array<cv::Point2d, 4> square = {{
        cv::Point2d(border + 0 * modPx, border + 0 * modPx),
        cv::Point2d(border + 7 * modPx, border + 0 * modPx),
        cv::Point2d(border + 7 * modPx, border + 7 * modPx),
        cv::Point2d(border + 0 * modPx, border + 7 * modPx),
    }};

    QrCodeBinaryGridToPixel alg;
    alg.setTransformFromSquare(square);

    // gridToImage(row, col, pixel) maps grid (col, row) to image pixel.
    cv::Point2d pixel;
    alg.gridToImage(0, 0, pixel);
    EXPECT_NEAR(border, pixel.x, 1e-6);
    EXPECT_NEAR(border, pixel.y, 1e-6);

    alg.gridToImage(/*row=*/0, /*col=*/7, pixel);
    EXPECT_NEAR(border + 7 * modPx, pixel.x, 1e-6);
    EXPECT_NEAR(border, pixel.y, 1e-6);

    alg.gridToImage(/*row=*/7, /*col=*/7, pixel);
    EXPECT_NEAR(border + 7 * modPx, pixel.x, 1e-6);
    EXPECT_NEAR(border + 7 * modPx, pixel.y, 1e-6);

    alg.gridToImage(/*row=*/7, /*col=*/0, pixel);
    EXPECT_NEAR(border, pixel.x, 1e-6);
    EXPECT_NEAR(border + 7 * modPx, pixel.y, 1e-6);

    // imageToGrid: out is cv::Point2d{col, row}.
    cv::Point2d grid;
    alg.imageToGrid(border + 3.5 * modPx, border + 5.5 * modPx, grid);
    EXPECT_NEAR(3.5, grid.x, 1e-6);  // col
    EXPECT_NEAR(5.5, grid.y, 1e-6);  // row
}

// `setTransformFromLinesSquare` round-trip: mirrors Java's
// `TestQrCodeBinaryGridToPixel.setTransformFromLinesSquare()` (line 56
// of upstream). Java's test renders a v=2 QR via `QrCodeGeneratorImage`
// at module-pixel-size 4 with the default 2-module border, then asserts
// the homography correctly maps grid coords (0,0), (7,0), (7,7) to the
// expected image pixels.
//
// We don't have the Java encoder ported, so we synthesize the same
// affine layout directly: ppCorner / ppRight / ppDown placed at the
// canonical v=2 (25-module) grid positions, transformed to image
// space by an affine `image = scale*grid + offset`. The DLT must
// recover this exactly because the line directions and point
// constraints all agree (no projective shear).
TEST(QrCodeBinaryGridToPixel, setTransformFromLinesSquare_roundTrip) {
    using boofcv_qr::QrCode;

    constexpr double border = 8.0;   // v=2 default in Java: 2 modules * 4 px
    constexpr double mod = 4.0;      // 4 px per module

    // image = (border + col*mod, border + row*mod). Inverse:
    // col = (img.x - border) / mod, row = (img.y - border) / mod.
    auto gridToImage = [&](double col, double row) {
        return cv::Point2d(border + col * mod, border + row * mod);
    };

    QrCode qr;
    // v=2 has numModules = 25; right/down finders sit at module 18..25.
    // ppCorner: grid corners (0,0), (7,0), (7,7), (0,7).
    qr.ppCorner[0] = gridToImage(0, 0);
    qr.ppCorner[1] = gridToImage(7, 0);
    qr.ppCorner[2] = gridToImage(7, 7);
    qr.ppCorner[3] = gridToImage(0, 7);
    // ppRight: grid corners (18,0), (25,0), (25,7), (18,7).
    qr.ppRight[0] = gridToImage(18, 0);
    qr.ppRight[1] = gridToImage(25, 0);
    qr.ppRight[2] = gridToImage(25, 7);
    qr.ppRight[3] = gridToImage(18, 7);
    // ppDown: grid corners (0,18), (7,18), (7,25), (0,25).
    qr.ppDown[0] = gridToImage(0, 18);
    qr.ppDown[1] = gridToImage(7, 18);
    qr.ppDown[2] = gridToImage(7, 25);
    qr.ppDown[3] = gridToImage(0, 25);

    QrCodeBinaryGridToPixel alg;
    alg.setTransformFromLinesSquare(qr);

    // Mirrors Java's test: gridToImage(row, col) maps grid (col, row) to
    // image pixel. Tolerance 1e-4 matches Java's `assertEquals(..., 1e-4f)`.
    auto check = [&](double col, double row, double expX, double expY) {
        cv::Point2d pixel;
        alg.gridToImage(row, col, pixel);
        EXPECT_NEAR(expX, pixel.x, 1e-4)
            << "col=" << col << " row=" << row;
        EXPECT_NEAR(expY, pixel.y, 1e-4)
            << "col=" << col << " row=" << row;
    };
    check(0, 0, border, border);
    check(7, 0, border + 7 * mod, border);
    check(7, 7, border + 7 * mod, border + 7 * mod);
}

// Discriminating test for the cross-product DLT row construction:
// place the QR finders under a 30° rotation about the corner's center.
// 3 points alone (rank 6) admit infinitely many H solutions that fit
// those 3 corners but rotate the rest of the plane wrong. The 4
// direction lines pin the rotation. A buggy 1-row-per-line
// construction would only constrain 1 DOF per line (so 4 lines give 4
// constraints) which still works for affine but loses the requirement
// that lines map to lines with the correct *orientation*.
//
// We verify by checking that the line endpoints (ppRight[0],
// ppRight[3], ppDown[0], ppDown[1]) — which lie OFF the 3-point
// constraint set — are mapped to the correct grid coordinates, with
// the correct rotation.
TEST(QrCodeBinaryGridToPixel, setTransformFromLinesSquare_rotated) {
    using boofcv_qr::QrCode;

    constexpr double theta = 30.0 * M_PI / 180.0;
    constexpr double cx = 200.0, cy = 150.0;   // image center of rotation
    constexpr double mod = 4.0;

    // image = R(theta) * (grid * mod) + (cx, cy). Affine-only, no
    // perspective — so the DLT must recover this exactly.
    auto gridToImage = [&](double col, double row) {
        double x = col * mod;
        double y = row * mod;
        return cv::Point2d(cx + std::cos(theta) * x - std::sin(theta) * y,
                            cy + std::sin(theta) * x + std::cos(theta) * y);
    };

    QrCode qr;
    qr.ppCorner[0] = gridToImage(0, 0);
    qr.ppCorner[1] = gridToImage(7, 0);
    qr.ppCorner[2] = gridToImage(7, 7);
    qr.ppCorner[3] = gridToImage(0, 7);
    qr.ppRight[0] = gridToImage(14, 0);
    qr.ppRight[1] = gridToImage(21, 0);
    qr.ppRight[2] = gridToImage(21, 7);
    qr.ppRight[3] = gridToImage(14, 7);
    qr.ppDown[0] = gridToImage(0, 14);
    qr.ppDown[1] = gridToImage(7, 14);
    qr.ppDown[2] = gridToImage(7, 21);
    qr.ppDown[3] = gridToImage(0, 21);

    QrCodeBinaryGridToPixel alg;
    alg.setTransformFromLinesSquare(qr);

    // 1e-9 tolerance: affine layout + noise-free input + correct DLT
    // = machine-precision recovery.
    auto assertMapsTo = [&](const cv::Point2d& image, double col, double row) {
        cv::Point2d g;
        alg.imageToGrid(image.x, image.y, g);
        EXPECT_NEAR(col, g.x, 1e-9)
            << "image=(" << image.x << "," << image.y << ") expected col=" << col;
        EXPECT_NEAR(row, g.y, 1e-9)
            << "image=(" << image.x << "," << image.y << ") expected row=" << row;
    };

    // 3 point constraints — these MUST land exactly.
    assertMapsTo(qr.ppCorner[1], 7.0, 0.0);
    assertMapsTo(qr.ppCorner[2], 7.0, 7.0);
    assertMapsTo(qr.ppCorner[3], 0.0, 7.0);

    // Line endpoints — these distinguish 1-row-per-line (buggy: under-
    // constrains rotation) from 2-row-per-line (correct).
    assertMapsTo(qr.ppRight[0], 14.0, 0.0);
    assertMapsTo(qr.ppRight[3], 14.0, 7.0);
    assertMapsTo(qr.ppDown[1], 7.0, 14.0);
    assertMapsTo(qr.ppDown[0], 0.0, 14.0);
}

// Round-trip: any image point through imageToGrid then gridToImage
// returns the original (within numerical noise).
TEST(QrCodeBinaryGridToPixel, roundTrip) {
    std::array<cv::Point2d, 4> square = {{
        cv::Point2d(10, 12), cv::Point2d(50, 8),
        cv::Point2d(54, 60), cv::Point2d(8, 56),
    }};

    QrCodeBinaryGridToPixel alg;
    alg.setTransformFromSquare(square);

    for (double x = 5; x < 60; x += 7) {
        for (double y = 5; y < 60; y += 7) {
            cv::Point2d grid;
            alg.imageToGrid(x, y, grid);
            cv::Point2d back;
            alg.gridToImage(grid.y, grid.x, back);
            EXPECT_NEAR(x, back.x, 1e-6);
            EXPECT_NEAR(y, back.y, 1e-6);
        }
    }
}
