// Port of boofcv.alg.fiducial.qrcode.QrCodeBinaryGridToPixel
// (BoofCV v1.3.0).
//
// Maintains a homography between QR module grid coordinates (col, row)
// and image pixel coordinates. CLAUDE.md mandates `cv::getPerspective
// Transform` / `cv::findHomography` / `cv::perspectiveTransform` for
// homography math; `cv::warpPerspective` is forbidden in this path.
//
// Algorithm description: src/sampler/qr_code_binary_grid_to_pixel.md.
//
// Note: setTransformFromLinesSquare uses BoofCV's line-correspondence
// DLT (which OpenCV doesn't directly expose). Implemented here via a
// custom 8-equation linear system solved with cv::SVDecomp — see the
// .cpp.

#ifndef BOOFCV_QR_QR_CODE_BINARY_GRID_TO_PIXEL_HPP
#define BOOFCV_QR_QR_CODE_BINARY_GRID_TO_PIXEL_HPP

#include <opencv2/core.hpp>

#include "boofcv_qr/qr_code.hpp"

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace boofcv_qr {

// Pair of (image_pixel, grid_coord) used to fit the homography.
struct AssociatedPair {
    cv::Point2d p1;  // image pixel
    cv::Point2d p2;  // grid coord (col, row)
};

class QrCodeBinaryGridToPixel {
public:
    QrCodeBinaryGridToPixel();

    // Set the homography from a single 4-corner finder-pattern square
    // (used for the simplest step-5 sampling).
    void setTransformFromSquare(const std::array<cv::Point2d, 4>& square);

    // Add the 12 finder-corner correspondences and any alignment
    // patterns from `qr`. Caller must then call computeTransform().
    //
    // The 6-arg overload was the only one available before step 9
    // because `QrCode` lacked the geometry fields. It is preserved
    // for the step-7/8 callers that still need to pass the polygons
    // explicitly.
    void addAllFeatures(const QrCode& qr,
                        const std::array<cv::Point2d, 4>& ppCorner,
                        const std::array<cv::Point2d, 4>& ppRight,
                        const std::array<cv::Point2d, 4>& ppDown,
                        const std::vector<cv::Point2d>& alignmentCenters,
                        const std::vector<cv::Point2d>& alignmentGridCoords);

    // Java's 1-arg signature — reads `qr.ppCorner`/`ppRight`/`ppDown`
    // and `qr.alignment[]` directly. Step-9 orchestrator path.
    void addAllFeatures(const QrCode& qr);

    // Estimate image-to-grid before the version is known. The top-left
    // finder square fixes the coordinate system; 4 lines between the
    // finders make the fit less sensitive to errors at any one corner.
    // Mirrors Java's `setTransformFromLinesSquare(QrCode)`.
    void setTransformFromLinesSquare(const QrCode& qr);

    // Outside corners of finder patterns are commonly damaged — drop
    // pairs at indices 0, 5, 11 (must follow exactly addAllFeatures).
    void removeOutsideCornerFeatures();

    // Returns true if a feature was removed; greedy outlier rejection
    // by reprojection-residual magnitude.
    bool removeFeatureWithLargestError();

    void computeTransform();

    // Coordinate transforms via cv::perspectiveTransform.
    void imageToGrid(double x, double y, cv::Point2d& grid) const;
    void gridToImage(double row, double col, cv::Point2d& pixel) const;

    void setAdjustWithFeatures(bool v) { adjustWithFeatures = v; }
    void setHomographyInv(const cv::Matx33d& Hinv_);

    // Public for parity testing — Java fields have package-private access.
    cv::Matx33d H = cv::Matx33d::eye();
    cv::Matx33d Hinv = cv::Matx33d::eye();
    bool adjustWithFeatures = false;
    std::vector<AssociatedPair> pairs2D;

private:
    // Storage for residual-based local adjustments (used when
    // adjustWithFeatures is on).
    std::vector<cv::Point2d> adjustments;

    void recomputeInverse();
};

}  // namespace boofcv_qr

#endif  // BOOFCV_QR_QR_CODE_BINARY_GRID_TO_PIXEL_HPP
