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
#include <cfloat>
#include <cmath>
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

    // Single-point projective transforms. Defined inline here so the 3×3
    // mat-vec + perspective-divide is visible to the compiler at every
    // call site — the bit sampler in `QrCodeBinaryGridReader::readBit` /
    // `readBitIntensity` hits `gridToImage` 5× per module bit (~156k
    // calls per Version-40 QR scan), and the original out-of-line
    // function call was profile-confirmed at ~10% of decode time on
    // `high_version`. The math is the same `(Mx + b) / (m20 x + m21 y +
    // m22)` that OpenCV's `perspectiveTransform_64f` runs; we mirror
    // OpenCV's `FLT_EPSILON` gate (not `DBL_EPSILON`) and `(0, 0)`
    // zero-fill on the degenerate branch — bit-identical output.
    //
    // The `adjustWithFeatures` slow path of `gridToImage` (per-call
    // nearest-pair lookup) is kept out-of-line: the branch is dead on
    // every decode call after cycle 3 (no consumer enables it on the
    // sampling path), and inlining the loop would bloat every call site
    // with code that never runs. The cheap `adjustWithFeatures &&
    // !adjustments.empty()` predicate is the only thing inlined; the
    // body below the predicate is forwarded to `applyAdjustment`.
    inline void imageToGrid(double x, double y, cv::Point2d& grid) const {
        const double w = H(2, 0) * x + H(2, 1) * y + H(2, 2);
        if (std::fabs(w) > FLT_EPSILON) {
            const double iw = 1.0 / w;
            grid.x = (H(0, 0) * x + H(0, 1) * y + H(0, 2)) * iw;
            grid.y = (H(1, 0) * x + H(1, 1) * y + H(1, 2)) * iw;
        } else {
            grid.x = 0.0;
            grid.y = 0.0;
        }
    }

    inline void gridToImage(double row, double col, cv::Point2d& pixel) const {
        const double w = Hinv(2, 0) * col + Hinv(2, 1) * row + Hinv(2, 2);
        if (std::fabs(w) > FLT_EPSILON) {
            const double iw = 1.0 / w;
            pixel.x = (Hinv(0, 0) * col + Hinv(0, 1) * row + Hinv(0, 2)) * iw;
            pixel.y = (Hinv(1, 0) * col + Hinv(1, 1) * row + Hinv(1, 2)) * iw;
        } else {
            pixel.x = 0.0;
            pixel.y = 0.0;
        }
        if (adjustWithFeatures && !adjustments.empty()) {
            applyAdjustment(row, col, pixel);
        }
    }

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

    // Slow-path helper for `gridToImage` — per-call nearest-pair lookup
    // when `adjustWithFeatures` is on. Out-of-line so the inlined fast
    // path stays small.
    void applyAdjustment(double row, double col, cv::Point2d& pixel) const;
};

}  // namespace boofcv_qr

#endif  // BOOFCV_QR_QR_CODE_BINARY_GRID_TO_PIXEL_HPP
