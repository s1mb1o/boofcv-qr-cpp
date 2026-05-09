// Port of boofcv.alg.shapes.edge.SnapToLineEdge (BoofCV v1.3.0).
// Step 7b (part 3).
//
// Snaps a line to a polygon edge by sampling line-integrals
// perpendicular to the line, weighting by the absolute step between
// adjacent integrals, and fitting a polar line via weighted least
// squares.
//
// Per CLAUDE.md type mappings:
//   ImageGray<T> -> cv::Mat CV_8UC1 (we commit to a single image type;
//                  Java's template <T> is dropped)
//   LineGeneral2D_F64 -> nested LineGeneral2D struct (A,B,C)
//   Point2D_F64 -> cv::Point2d
//   DogArray_F64 -> std::vector<double>
//   DogArray<Point2D_F64> -> std::vector<cv::Point2d>

#ifndef BOOFCV_QR_POLYGON_SNAP_TO_LINE_EDGE_HPP
#define BOOFCV_QR_POLYGON_SNAP_TO_LINE_EDGE_HPP

#include "boofcv_qr/polygon/image_line_integral.hpp"

#include <opencv2/core.hpp>

#include <cstdint>
#include <vector>

namespace boofcv_qr {

// `LineGeneral2D_F64` mirror — A*x + B*y + C = 0.
struct LineGeneral2D {
    double A = 0.0, B = 0.0, C = 0.0;

    void setTo(const LineGeneral2D& other) { A = other.A; B = other.B; C = other.C; }

    // Mirror of LineGeneral2D_F64.normalize()
    void normalize() {
        double n = std::sqrt(A * A + B * B);
        if (n != 0.0) { A /= n; B /= n; C /= n; }
    }
};

// `LinePolar2D_F64` mirror — `(angle, distance)` polar form.
struct LinePolar2D {
    double angle = 0.0;
    double distance = 0.0;
};

class SnapToLineEdge {
public:
    // `lineSamples` Number of times it will sample along the line's
    //   axis. ConfigRefinePolygonLineToImage default is 30; the
    //   `RefinePolygonToGrayLine(int numSides, …)` simple ctor uses 20.
    // `tangentialSamples` Radius along the tangent. Must be ≥ 1.
    SnapToLineEdge(int32_t lineSamples, int32_t tangentialSamples);

    // Sets the input image. Must be CV_8UC1.
    void setImage(const cv::Mat& image);

    // Fits a line defined by the two points. Multiple calls might be
    // required to get a perfect fit. Returns true if successful or
    // false if the sample window collected too few in-image points
    // (< 4) to fit a line.
    bool refine(const cv::Point2d& a, const cv::Point2d& b, LineGeneral2D& found);

    int32_t getLineSamples() const { return lineSamples_; }
    void setLineSamples(int32_t v) { lineSamples_ = v; }

    int32_t getRadialSamples() const { return radialSamples_; }
    void setRadialSamples(int32_t v) { radialSamples_ = v; }

    // ---- TEST-VISIBLE: package-private members in Java reached by JUnit
    //      directly. Exposed here for the unit tests.

    // The center used to define the local coordinate system.
    cv::Point2d& center() { return center_; }
    double& localScale() { return localScale_; }

    // Mirror of computePointsAndWeights (Java protected). Test-visible.
    void computePointsAndWeights(double slopeX, double slopeY, double x0,
                                  double y0, double tanX, double tanY);

    // Mirror of localToGlobal (Java protected). Test-visible.
    void localToGlobal(LineGeneral2D& line);

    const std::vector<cv::Point2d>& samplePts() const { return samplePts_; }
    const std::vector<double>& weights() const { return weights_; }

private:
    int32_t lineSamples_;
    int32_t radialSamples_;

    LinePolar2D polar_;

    std::vector<double> weights_;
    std::vector<cv::Point2d> samplePts_;

    cv::Point2d center_{0.0, 0.0};
    double localScale_ = 0.0;

    ImageLineIntegral integral_;
};

}  // namespace boofcv_qr

#endif  // BOOFCV_QR_POLYGON_SNAP_TO_LINE_EDGE_HPP
