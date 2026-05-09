// Subset of TestQrCodeBinaryGridToPixel.java — the upstream tests use
// QrCodeEncoder + QrCodeGeneratorImage to render a real QR. Those
// classes aren't part of our decoder-only port, so we substitute a
// synthetic 4-corner setup with hand-known image coordinates.

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
