// Port of boofcv.alg.shapes.polygon.{RefinePolygonToGray,RefinePolygonToGrayLine,UtilShapePolygon}
// (BoofCV v1.3.0). Step 7b (part 3).
//
// Per CLAUDE.md type mappings:
//   Polygon2D_F64 -> std::vector<cv::Point2d>
//   ImageGray<T> -> cv::Mat CV_8UC1 (we commit to a single image type;
//                  Java's template <T> is dropped)

#ifndef BOOFCV_QR_POLYGON_REFINE_POLYGON_TO_GRAY_HPP
#define BOOFCV_QR_POLYGON_REFINE_POLYGON_TO_GRAY_HPP

#include "boofcv_qr/polygon/snap_to_line_edge.hpp"

#include <opencv2/core.hpp>

#include <cstdint>
#include <vector>

namespace boofcv_qr {

// Refines a polygon using the gray scale image. Mirror of the
// `RefinePolygonToGray` interface; concrete impls swap the
// edge-snapping kernel.
struct RefinePolygonToGray {
    virtual ~RefinePolygonToGray() = default;

    // Sets the input image. Must be CV_8UC1.
    virtual void setImage(const cv::Mat& image) = 0;

    // Lens distortion — deferred (CLAUDE.md "Public API design"). Stub
    // no-ops in the QR-only port.
    virtual void setLensDistortion(int32_t /*width*/, int32_t /*height*/) {}
    virtual void clearLensDistortion() {}

    // Refines the initial polygon. Returns true if successful or false
    // if it failed (e.g. shape too small or all sides reverted).
    virtual bool refine(const std::vector<cv::Point2d>& input,
                         std::vector<cv::Point2d>& output) = 0;
};

// Mirror of `boofcv.factory.shape.ConfigRefinePolygonLineToImage`.
struct ConfigRefinePolygonLineToImage {
    double cornerOffset = 1.0;
    int32_t lineSamples = 30;
    int32_t sampleRadius = 1;
    int32_t maxIterations = 10;
    double convergeTolPixels = 0.2;
    double maxCornerChangePixel = 2.0;
};

class RefinePolygonToGrayLine : public RefinePolygonToGray {
public:
    // Constructor which provides full access to all parameters.
    RefinePolygonToGrayLine(double cornerOffset, int32_t lineSamples,
                             int32_t sampleRadius, int32_t maxIterations,
                             double convergeTolPixels,
                             double maxCornerChangePixel);

    // Simplified constructor with reasonable defaults (mirror of the
    // 2-arg Java ctor). `numSides` is used to pre-size the working
    // polygon storage.
    explicit RefinePolygonToGrayLine(int32_t numSides);

    // Plumbed-config ctor — applies every field from the upstream
    // config struct (mirrors `FactoryShapeDetector.refinePolygon`).
    explicit RefinePolygonToGrayLine(const ConfigRefinePolygonLineToImage& cfg);

    void setImage(const cv::Mat& image) override;

    bool refine(const std::vector<cv::Point2d>& input,
                 std::vector<cv::Point2d>& output) override;

    SnapToLineEdge& getSnapToEdge() { return snapToEdge_; }
    const SnapToLineEdge& getSnapToEdge() const { return snapToEdge_; }

    // Tunable getters — useful for asserting the plumbed-config ctor
    // landed every field on `impl_` (matches the same pattern as
    // `PolylineSplitMergeAdapter::impl()` exposed for parity tests).
    double getCornerOffset() const { return cornerOffset_; }
    int32_t getMaxIterations() const { return maxIterations_; }
    double getConvergeTolPixels() const { return convergeTolPixels_; }
    double getMaxCornerChangePixel() const { return maxCornerChangePixel_; }

    // ---- TEST-VISIBLE
    bool optimize(const cv::Point2d& a, const cv::Point2d& b,
                   LineGeneral2D& found);

private:
    bool checkShapeTooSmall(const std::vector<cv::Point2d>& input) const;
    bool optimizePolygon(const std::vector<cv::Point2d>& seed,
                          std::vector<cv::Point2d>& current);
    void computeAdjustedEndPoints(const cv::Point2d& a, const cv::Point2d& b);

    double cornerOffset_ = 2.0;
    int32_t maxIterations_ = 10;
    double convergeTolPixels_ = 0.01;
    double maxCornerChangePixel_ = 2.0;

    SnapToLineEdge snapToEdge_;

    // Storage for working state across iterations. Pre-sized in the
    // numSides ctor; resized in `refine` when input is larger.
    std::vector<LineGeneral2D> general_;
    std::vector<cv::Point2d> previous_;

    // Adjusted-endpoint storage (Java's adjA / adjB).
    cv::Point2d adjA_{0.0, 0.0};
    cv::Point2d adjB_{0.0, 0.0};

    // Workspace for divergence check (Java's tempA / tempB / before).
    cv::Point2d tempA_{0.0, 0.0};
    cv::Point2d tempB_{0.0, 0.0};
    LineGeneral2D before_;
};

// Mirror of `boofcv.alg.shapes.polygon.UtilShapePolygon`.
namespace UtilShapePolygon {

// Finds the intersections between consecutive lines (assumed ordered)
// and writes them into `poly`. Returns false if any pair was parallel
// (no intersection).
bool convert(const std::vector<LineGeneral2D>& lines,
              std::vector<cv::Point2d>& poly);

}  // namespace UtilShapePolygon

}  // namespace boofcv_qr

#endif  // BOOFCV_QR_POLYGON_REFINE_POLYGON_TO_GRAY_HPP
