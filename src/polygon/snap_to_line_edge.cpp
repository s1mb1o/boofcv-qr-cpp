// Port of boofcv.alg.shapes.edge.SnapToLineEdge (BoofCV v1.3.0).
// Verbatim per CLAUDE.md "Verbatim vs idiomize".

#include "boofcv_qr/polygon/snap_to_line_edge.hpp"

#include <cmath>
#include <stdexcept>

namespace boofcv_qr {

namespace {

// Mirror of `georegression.fitting.line.FitLine_F64.polar(points, weights, ret)`.
// Returns true if total weight > 0 and `ret` was filled, false otherwise.
//
// Decompiled formula:
//   meanX, meanY = weighted means
//   top = Σ w * (mean - p).x * (mean - p).y
//   bottom = Σ w * ((mean - p).y² - (mean - p).x²)
//   angle = atan2(-2 * top / W, bottom / W) / 2
//   distance = meanX*cos(angle) + meanY*sin(angle)
bool fitLinePolarWeighted(const std::vector<cv::Point2d>& points,
                           const std::vector<double>& weights,
                           LinePolar2D& ret) {
    int32_t N = static_cast<int32_t>(points.size());
    double totalWeight = 0.0;
    for (int32_t i = 0; i < N; i++) {
        totalWeight += weights[static_cast<std::size_t>(i)];
    }
    if (totalWeight == 0.0) {
        return false;
    }

    double meanX = 0.0;
    double meanY = 0.0;
    for (int32_t i = 0; i < N; i++) {
        const cv::Point2d& p = points[static_cast<std::size_t>(i)];
        double w = weights[static_cast<std::size_t>(i)];
        meanX += w * p.x;
        meanY += w * p.y;
    }
    meanX /= totalWeight;
    meanY /= totalWeight;

    double top = 0.0;
    double bottom = 0.0;
    for (int32_t i = 0; i < N; i++) {
        const cv::Point2d& p = points[static_cast<std::size_t>(i)];
        double w = weights[static_cast<std::size_t>(i)];
        double dx = meanX - p.x;
        double dy = meanY - p.y;
        top += w * dx * dy;
        bottom += w * (dy * dy - dx * dx);
    }
    ret.angle = std::atan2(-2.0 * (top / totalWeight), bottom / totalWeight) / 2.0;
    ret.distance = meanX * std::cos(ret.angle) + meanY * std::sin(ret.angle);
    return true;
}

// Mirror of `georegression.geometry.UtilLine2D_F64.convert(LinePolar2D_F64, LineGeneral2D_F64)`.
void polarToGeneral(const LinePolar2D& src, LineGeneral2D& ret) {
    double c = std::cos(src.angle);
    double s = std::sin(src.angle);
    ret.A = c;
    ret.B = s;
    ret.C = -src.distance;
}

}  // namespace

SnapToLineEdge::SnapToLineEdge(int32_t lineSamples, int32_t tangentialSamples)
    : lineSamples_(lineSamples), radialSamples_(tangentialSamples) {
    if (tangentialSamples < 1)
        throw std::invalid_argument(
            "Tangential samples must be >= 1 or else it won't work");
}

void SnapToLineEdge::setImage(const cv::Mat& image) {
    integral_.setImage(image);
}

bool SnapToLineEdge::refine(const cv::Point2d& a, const cv::Point2d& b,
                              LineGeneral2D& found) {
    // determine the local coordinate system
    center_.x = (a.x + b.x) / 2.0;
    center_.y = (a.y + b.y) / 2.0;
    double cdx = a.x - center_.x;
    double cdy = a.y - center_.y;
    localScale_ = std::sqrt(cdx * cdx + cdy * cdy);

    // define the line which points are going to be sampled along
    double slopeX = (b.x - a.x);
    double slopeY = (b.y - a.y);
    double r = std::sqrt(slopeX * slopeX + slopeY * slopeY);

    // tangent of unit length that radial sample samples are going to be along
    // Two choices for tangent here. Select the one which points to the "right" of the line,
    // which is inside of the edge
    double tanX = slopeY / r;
    double tanY = -slopeX / r;

    // set up inputs into line fitting
    computePointsAndWeights(slopeX, slopeY, a.x, a.y, tanX, tanY);

    if (samplePts_.size() >= 4) {
        // fit line and convert into generalized format
        if (!fitLinePolarWeighted(samplePts_, weights_, polar_)) {
            throw std::runtime_error(
                "All weights were zero, bug some place");
        }
        polarToGeneral(polar_, found);

        // Convert line from local to global coordinates
        localToGlobal(found);

        return true;
    } else {
        return false;
    }
}

void SnapToLineEdge::computePointsAndWeights(double slopeX, double slopeY,
                                              double x0, double y0,
                                              double tanX, double tanY) {

    samplePts_.clear();
    weights_.clear();
    int32_t numSamples = radialSamples_ * 2 + 2;
    int32_t numPts = numSamples - 1;
    double widthX = numSamples * tanX;
    double widthY = numSamples * tanY;

    for (int32_t i = 0; i < lineSamples_; i++) {
        // find point on line and shift it over to the first sample point
        double frac = i / static_cast<double>(lineSamples_ - 1);
        double x = x0 + slopeX * frac - widthX / 2.0;
        double y = y0 + slopeY * frac - widthY / 2.0;

        // Unless all the sample points are inside the image, ignore this point
        if (!integral_.isInside(x, y) || !integral_.isInside(x + widthX, y + widthY))
            continue;

        double sample0 = integral_.compute(x, y, x + tanX, y + tanY);
        x += tanX;
        y += tanY;
        for (int32_t j = 0; j < numPts; j++) {
            // still need to check the next point due to round off error. very rare condition
            if (j == numPts - 1 && !integral_.isInside(x + tanX, y + tanY))
                break;
            double sample1 = integral_.compute(x, y, x + tanX, y + tanY);

            double w = sample0 - sample1;
            if (w < 0) w = -w;

            if (w > 0) {
                weights_.push_back(w);
                samplePts_.emplace_back((x - center_.x) / localScale_,
                                         (y - center_.y) / localScale_);
            }

            x += tanX;
            y += tanY;
            sample0 = sample1;
        }
    }
}

void SnapToLineEdge::localToGlobal(LineGeneral2D& line) {
    line.C = localScale_ * line.C - center_.x * line.A - center_.y * line.B;
}

}  // namespace boofcv_qr
