// Port of boofcv.alg.shapes.polygon.{DetectPolygonBinaryGrayRefine,AdjustPolygonForThresholdBias}
// + boofcv.alg.shapes.edge.{ScoreLineSegmentEdge,EdgeIntensityPolygon}
// (BoofCV v1.3.0). Step 7b (part 3) — top-level entry point that
// composes the contour stage with the refinement chain.
//
// Per CLAUDE.md type mappings:
//   ImageGray<T> -> cv::Mat CV_8UC1 (we commit to a single image type;
//                  Java's template <T> is dropped)

#ifndef BOOFCV_QR_POLYGON_DETECT_POLYGON_BINARY_GRAY_REFINE_HPP
#define BOOFCV_QR_POLYGON_DETECT_POLYGON_BINARY_GRAY_REFINE_HPP

#include "boofcv_qr/polygon/detect_polygon_from_contour.hpp"
#include "boofcv_qr/polygon/image_line_integral.hpp"
#include "boofcv_qr/polygon/refine_polygon_to_gray.hpp"

#include <opencv2/core.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace boofcv_qr {

// Mirror of `boofcv.alg.shapes.edge.ScoreLineSegmentEdge`.
class ScoreLineSegmentEdge {
public:
    explicit ScoreLineSegmentEdge(int32_t numSamples) : numSamples_(numSamples) {}

    void setImage(const cv::Mat& image) { integral_.setImage(image); }

    // Returns average tangential derivative along the line segment.
    // Derivative is computed in direction of tangent. A positive step in
    // the tangent direction will have a positive value. If all samples
    // go outside the image, returns 0.
    double computeAverageDerivative(const cv::Point2d& a, const cv::Point2d& b,
                                     double tanX, double tanY);

    int32_t getSamplesInside() const { return samplesInside_; }
    int32_t getNumSamples() const { return numSamples_; }
    void setNumSamples(int32_t v) { numSamples_ = v; }
    double getAverageUp() const { return averageUp_; }
    double getAverageDown() const { return averageDown_; }

private:
    int32_t numSamples_;
    int32_t samplesInside_ = 0;
    double averageUp_ = 0.0;
    double averageDown_ = 0.0;
    ImageLineIntegral integral_;
};

// Mirror of `boofcv.alg.shapes.edge.EdgeIntensityPolygon`. Used by the
// refine wrapper as a quality gate.
class EdgeIntensityPolygon {
public:
    EdgeIntensityPolygon(double cornerOffset, double tangentDistance,
                          int32_t numSamples);

    void setImage(const cv::Mat& image) { scorer_.setImage(image); }

    // Compute average inside/outside intensity along each side. Returns
    // true if any side had an in-image sample.
    bool computeEdge(const std::vector<cv::Point2d>& polygon, bool ccw);

    double getAverageInside() const { return averageInside_; }
    double getAverageOutside() const { return averageOutside_; }

    double getCornerOffset() const { return cornerOffset_; }
    void setCornerOffset(double v) { cornerOffset_ = v; }
    double getTangentDistance() const { return tangentDistance_; }
    void setTangentDistance(double v) { tangentDistance_ = v; }

private:
    double cornerOffset_;
    double tangentDistance_;

    cv::Point2d offsetA_{0.0, 0.0};
    cv::Point2d offsetB_{0.0, 0.0};

    double averageInside_ = 0.0;
    double averageOutside_ = 0.0;

    ScoreLineSegmentEdge scorer_;
};

// Mirror of `boofcv.alg.shapes.polygon.AdjustPolygonForThresholdBias`.
class AdjustPolygonForThresholdBias {
public:
    // Adjust the polygon vertices to undo the floor() bias the binary
    // threshold introduces. Polygon may LOSE vertices if a corner
    // becomes a near-duplicate of its neighbour after the shift —
    // caller must check `polygon.size()` afterwards.
    void process(std::vector<cv::Point2d>& polygon, bool clockwise);

private:
    // Per-side line-segment scratch — `(a.x, a.y, b.x, b.y)` per side.
    struct LineSeg2D {
        cv::Point2d a;
        cv::Point2d b;
    };
    std::vector<LineSeg2D> segments_;
};

// Top-level wrapper: contour stage + threshold-bias adjust + per-shape
// edge-intensity gating + per-shape gray refinement.
class DetectPolygonBinaryGrayRefine {
public:
    // `detector` is moved-in (we own it). `refineGray` may be null for
    // the no-refinement variant (callers that want raw integer-pixel
    // polygons). `minimumRefineEdgeIntensity` is the post-refine
    // pruning gate (`Info::computeEdgeIntensity()` ≥ this).
    DetectPolygonBinaryGrayRefine(
        std::unique_ptr<DetectPolygonFromContour> detector,
        std::shared_ptr<RefinePolygonToGray> refineGray,
        double minimumRefineEdgeIntensity, bool adjustForThresholdBias);

    void setHelper(std::shared_ptr<PolygonHelper> helper) {
        detector_->setHelper(std::move(helper));
    }

    // Lens distortion — deferred. Stub no-op.
    void setLensDistortion(int32_t /*width*/, int32_t /*height*/) {}
    void clearLensDistortion() {}

    void resetRuntimeProfiling() {
        detector_->resetRuntimeProfiling();
        milliAdjustBias_ = 0.0;
    }

    // Detect polygons in `gray` using the binary mask `binary`. Both
    // must be `CV_8UC1`.
    void process(const cv::Mat& gray, const cv::Mat& binary);

    // Refines the fit of the polygon at `info`. Returns true if any
    // refinement stage improved the fit.
    bool refine(DetectPolygonFromContour::DetectedInfo& info);

    // Refines every detection. Detections that fail are NOT removed
    // (Java's behaviour) — callers use `getPolygons` to filter on
    // edge intensity.
    void refineAll();

    // Returns just the polygons whose `Info::computeEdgeIntensity()` is
    // at or above the configured `minimumRefineEdgeIntensity`. Values
    // are deep-copied from the underlying detection storage — per
    // CLAUDE.md "Public API design" line 32 (owned types only; no raw
    // pointers in public signatures).
    //
    // Java's overload took an optional `DogArray<Info>` storage arg
    // for the corresponding entries; we expose that view via
    // `getPolygonInfoFiltered()` instead, which returns the surviving
    // `Info` entries by value.
    std::vector<std::vector<cv::Point2d>> getPolygons() const;

    // Returns the `Info` entries for polygons that pass the
    // `minimumRefineEdgeIntensity` gate. Same filter as `getPolygons()`
    // but returns the full `DetectedInfo` (corner array + contour +
    // edge-intensity diagnostics + per-corner border flags). Deep-copy
    // by value per CLAUDE.md "Public API design".
    std::vector<DetectPolygonFromContour::DetectedInfo> getPolygonInfoFiltered() const;

    // Reference invalidated by next `process()` call.
    const std::vector<DetectPolygonFromContour::DetectedInfo>& getPolygonInfo() const {
        return detector_->getFoundInfo();
    }

    DetectPolygonFromContour& getDetector() { return *detector_; }
    const DetectPolygonFromContour& getDetector() const { return *detector_; }

    // Underlying refinement strategy. May be null if the wrapper was
    // constructed without one. Useful for parity checks asserting that
    // a plumbed-config ctor delivered every field.
    std::shared_ptr<RefinePolygonToGray> getRefineGray() const { return refineGray_; }

    int32_t getMinimumSides() const { return detector_->getMinimumSides(); }
    int32_t getMaximumSides() const { return detector_->getMaximumSides(); }
    bool isOutputClockwise() const { return detector_->isOutputClockwiseUpY(); }

    double getMilliAdjustBias() const { return milliAdjustBias_; }

    // Pruning gate.
    double getMinimumRefineEdgeIntensity() const { return minimumRefineEdgeIntensity_; }
    void setMinimumRefineEdgeIntensity(double v) { minimumRefineEdgeIntensity_ = v; }

private:
    std::unique_ptr<DetectPolygonFromContour> detector_;
    std::shared_ptr<RefinePolygonToGray> refineGray_;
    std::unique_ptr<AdjustPolygonForThresholdBias> adjustForBias_;

    double minimumRefineEdgeIntensity_;

    EdgeIntensityPolygon edgeIntensity_;
    std::vector<cv::Point2d> work_;

    double milliAdjustBias_ = 0.0;
};

}  // namespace boofcv_qr

#endif  // BOOFCV_QR_POLYGON_DETECT_POLYGON_BINARY_GRAY_REFINE_HPP
