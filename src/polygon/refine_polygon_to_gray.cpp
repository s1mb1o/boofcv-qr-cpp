// Port of boofcv.alg.shapes.polygon.{RefinePolygonToGrayLine,UtilShapePolygon}
// (BoofCV v1.3.0). Verbatim per CLAUDE.md "Verbatim vs idiomize".

#include "boofcv_qr/polygon/refine_polygon_to_gray.hpp"

#include <cmath>
#include <stdexcept>

namespace boofcv_qr {

namespace {

// Mirror of `georegression.geometry.UtilLine2D_F64.convert(Point2D_F64 a, Point2D_F64 b, LineGeneral2D_F64 ret)`.
void lineGeneralFromTwoPoints(const cv::Point2d& a, const cv::Point2d& b,
                                LineGeneral2D& ret) {
    ret.A = a.y - b.y;
    ret.B = b.x - a.x;
    ret.C = -(ret.A * a.x + ret.B * a.y);
}

// Mirror of `georegression.metric.Intersection2D_F64.intersection(LineGeneral2D_F64, LineGeneral2D_F64, Point2D_F64)`.
// Returns true on success (lines not parallel) and writes the
// intersection into `out`.
bool lineGeneralIntersection(const LineGeneral2D& a, const LineGeneral2D& b,
                              cv::Point2d& out) {
    double x = (a.B * b.C) - (a.C * b.B);
    double y = (a.C * b.A) - (a.A * b.C);
    double z = (a.A * b.B) - (a.B * b.A);
    if (z == 0.0) return false;
    out.x = x / z;
    out.y = y / z;
    return true;
}

double distance2(const cv::Point2d& a, const cv::Point2d& b) {
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    return dx * dx + dy * dy;
}

double distance(const cv::Point2d& a, const cv::Point2d& b) {
    return std::sqrt(distance2(a, b));
}

}  // namespace

namespace UtilShapePolygon {

bool convert(const std::vector<LineGeneral2D>& lines,
              std::vector<cv::Point2d>& poly) {
    int32_t N = static_cast<int32_t>(poly.size());
    for (int32_t i = 0; i < N; i++) {
        int32_t j = (i + 1) % N;
        if (!lineGeneralIntersection(lines[static_cast<std::size_t>(i)],
                                      lines[static_cast<std::size_t>(j)],
                                      poly[static_cast<std::size_t>(j)])) {
            return false;
        }
    }
    return true;
}

}  // namespace UtilShapePolygon

// ============================================================================
// RefinePolygonToGrayLine
// ============================================================================

RefinePolygonToGrayLine::RefinePolygonToGrayLine(double cornerOffset,
                                                  int32_t lineSamples,
                                                  int32_t sampleRadius,
                                                  int32_t maxIterations,
                                                  double convergeTolPixels,
                                                  double maxCornerChangePixel)
    : cornerOffset_(cornerOffset),
      maxIterations_(maxIterations),
      convergeTolPixels_(convergeTolPixels),
      maxCornerChangePixel_(maxCornerChangePixel),
      snapToEdge_(lineSamples, sampleRadius) {
    previous_.assign(1, cv::Point2d(0, 0));
}

RefinePolygonToGrayLine::RefinePolygonToGrayLine(int32_t numSides)
    : snapToEdge_(20, 1) {
    previous_.assign(static_cast<std::size_t>(numSides), cv::Point2d(0, 0));
}

RefinePolygonToGrayLine::RefinePolygonToGrayLine(
    const ConfigRefinePolygonLineToImage& cfg)
    : cornerOffset_(cfg.cornerOffset),
      maxIterations_(cfg.maxIterations),
      convergeTolPixels_(cfg.convergeTolPixels),
      maxCornerChangePixel_(cfg.maxCornerChangePixel),
      snapToEdge_(cfg.lineSamples, cfg.sampleRadius) {
    previous_.assign(1, cv::Point2d(0, 0));
}

void RefinePolygonToGrayLine::setImage(const cv::Mat& image) {
    snapToEdge_.setImage(image);
}

bool RefinePolygonToGrayLine::refine(const std::vector<cv::Point2d>& input,
                                       std::vector<cv::Point2d>& output) {
    if (input.size() != output.size())
        throw std::invalid_argument(
            "Input and output sides do not match.");

    // sanity check input. If it's too small this algorithm won't work
    if (checkShapeTooSmall(input))
        return false;

    // see if this work space needs to be resized
    if (general_.size() < input.size()) {
        general_.assign(input.size(), LineGeneral2D{});
    }

    // estimate line equations
    return optimizePolygon(input, output);
}

bool RefinePolygonToGrayLine::checkShapeTooSmall(
    const std::vector<cv::Point2d>& input) const {
    // must be longer than the border plus some small fudge factor
    double minLength = cornerOffset_ * 2 + 2;
    int32_t N = static_cast<int32_t>(input.size());
    for (int32_t i = 0; i < N; i++) {
        int32_t j = (i + 1) % N;
        const cv::Point2d& a = input[static_cast<std::size_t>(i)];
        const cv::Point2d& b = input[static_cast<std::size_t>(j)];
        if (distance2(a, b) < minLength * minLength)
            return true;
    }
    return false;
}

bool RefinePolygonToGrayLine::optimizePolygon(
    const std::vector<cv::Point2d>& seed, std::vector<cv::Point2d>& current) {
    previous_ = seed;

    // pixels squares is faster to compute
    double convergeTol = convergeTolPixels_ * convergeTolPixels_;

    int32_t N = static_cast<int32_t>(seed.size());

    // initialize the lines since they are used to check for corner divergence
    for (int32_t i = 0; i < N; i++) {
        int32_t j = (i + 1) % N;
        const cv::Point2d& a = seed[static_cast<std::size_t>(i)];
        const cv::Point2d& b = seed[static_cast<std::size_t>(j)];
        lineGeneralFromTwoPoints(a, b, general_[static_cast<std::size_t>(i)]);
    }

    bool changed = false;
    for (int32_t iteration = 0; iteration < maxIterations_; iteration++) {
        // snap each line to the edge independently. Lines will be in local coordinates
        for (int32_t i = 0; i < N; i++) {
            int32_t j = (i + 1) % N;
            const cv::Point2d& a = previous_[static_cast<std::size_t>(i)];
            const cv::Point2d& b = previous_[static_cast<std::size_t>(j)];

            before_.setTo(general_[static_cast<std::size_t>(i)]);

            bool failed = false;
            if (!optimize(a, b, general_[static_cast<std::size_t>(i)])) {
                failed = true;
            } else {
                int32_t k = (i + N - 1) % N;

                // see if the corner has diverged
                bool okA = lineGeneralIntersection(
                    general_[static_cast<std::size_t>(k)],
                    general_[static_cast<std::size_t>(i)], tempA_);
                bool okB = lineGeneralIntersection(
                    general_[static_cast<std::size_t>(i)],
                    general_[static_cast<std::size_t>(j)], tempB_);
                if (okA && okB) {
                    if (distance(tempA_, a) > maxCornerChangePixel_ ||
                        distance(tempB_, b) > maxCornerChangePixel_) {
                        failed = true;
                    }
                } else {
                    failed = true;
                }
            }

            // The line fit failed. Probably because its along the image border. Revert it
            if (failed) {
                general_[static_cast<std::size_t>(i)].setTo(before_);
            } else {
                changed = true;
            }
        }

        // Find the corners of the quadrilateral from the lines
        if (!UtilShapePolygon::convert(general_, current))
            return false;

        // see if it has converged
        bool converged = true;
        for (int32_t i = 0; i < N; i++) {
            if (distance2(current[static_cast<std::size_t>(i)],
                          previous_[static_cast<std::size_t>(i)]) > convergeTol) {
                converged = false;
                break;
            }
        }
        if (converged) {
            break;
        } else {
            previous_ = current;
        }
    }

    return changed;
}

bool RefinePolygonToGrayLine::optimize(const cv::Point2d& a,
                                        const cv::Point2d& b,
                                        LineGeneral2D& found) {
    computeAdjustedEndPoints(a, b);
    return snapToEdge_.refine(adjA_, adjB_, found);
}

void RefinePolygonToGrayLine::computeAdjustedEndPoints(const cv::Point2d& a,
                                                        const cv::Point2d& b) {
    double slopeX = (b.x - a.x);
    double slopeY = (b.y - a.y);
    double r = std::sqrt(slopeX * slopeX + slopeY * slopeY);
    // vector of unit length pointing in direction of the slope
    double unitX = slopeX / r;
    double unitY = slopeY / r;

    // offset from corner because the gradient because unstable around there
    adjA_.x = a.x + unitX * cornerOffset_;
    adjA_.y = a.y + unitY * cornerOffset_;
    adjB_.x = b.x - unitX * cornerOffset_;
    adjB_.y = b.y - unitY * cornerOffset_;
}

}  // namespace boofcv_qr
