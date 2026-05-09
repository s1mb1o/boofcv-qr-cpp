// Port of boofcv.alg.shapes.polygon.{DetectPolygonBinaryGrayRefine,AdjustPolygonForThresholdBias}
// + boofcv.alg.shapes.edge.{ScoreLineSegmentEdge,EdgeIntensityPolygon}
// (BoofCV v1.3.0). Verbatim per CLAUDE.md "Verbatim vs idiomize".

#include "boofcv_qr/polygon/detect_polygon_binary_gray_refine.hpp"

#include <chrono>
#include <cmath>
#include <stdexcept>

namespace boofcv_qr {

namespace {

// Mirror of `georegression.geometry.UtilLine2D_F64.convert(LineSegment2D, LineGeneral)` (= same as two-points overload).
void lineGeneralFromSegment(const cv::Point2d& a, const cv::Point2d& b,
                              LineGeneral2D& ret) {
    ret.A = a.y - b.y;
    ret.B = b.x - a.x;
    ret.C = -(ret.A * a.x + ret.B * a.y);
}

// Mirror of `Intersection2D_F64.intersection(LineGeneral, LineGeneral, Point2D_F64)`.
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

// Mirror of `UtilPolygons2D_F64.removeAdjacentDuplicates(polygon, tol)`.
//
// Walks the polygon backwards; if `polygon[i]` is within `tol` of
// `polygon[j]` (where `j` is the *previous* iteration's `i`, i.e. one
// step ahead in the sweep) then `i` is removed.
void removeAdjacentDuplicatesPolygon(std::vector<cv::Point2d>& polygon,
                                      double tol) {
    int32_t j = 0;
    for (int32_t i = static_cast<int32_t>(polygon.size()) - 1;
         i >= 0 && polygon.size() > 1; i--) {
        cv::Point2d pi = polygon[static_cast<std::size_t>(i)];
        cv::Point2d pj = polygon[static_cast<std::size_t>(j)];
        if (std::fabs(pi.x - pj.x) <= tol && std::fabs(pi.y - pj.y) <= tol) {
            polygon.erase(polygon.begin() + i);
        }
        j = i;
    }
}

inline void movingAverageUpdate(double& avg, double sample, double decay) {
    avg = avg * decay + sample * (1.0 - decay);
}

}  // namespace

// ============================================================================
// ScoreLineSegmentEdge
// ============================================================================

double ScoreLineSegmentEdge::computeAverageDerivative(const cv::Point2d& a,
                                                       const cv::Point2d& b,
                                                       double tanX, double tanY) {
    samplesInside_ = 0;
    averageUp_ = averageDown_ = 0;

    // Java does `BoofMiscOps.isInside(integralImage.getWidth(), …)` here;
    // our `ImageLineIntegral::isInside` already captures the bound
    // image's dimensions, so route the check through it.

    for (int32_t i = 0; i < numSamples_; i++) {
        double x = (b.x - a.x) * i / (numSamples_ - 1) + a.x;
        double y = (b.y - a.y) * i / (numSamples_ - 1) + a.y;

        double x0 = x + tanX;
        double y0 = y + tanY;
        if (!integral_.isInside(x0, y0))
            continue;

        double x1 = x - tanX;
        double y1 = y - tanY;
        if (!integral_.isInside(x1, y1))
            continue;

        samplesInside_++;

        double up = integral_.compute(x, y, x0, y0);
        double down = integral_.compute(x, y, x1, y1);

        // don't take the abs here and require that a high score involves it being entirely black or white around
        // the edge. Otherwise a random image would score high
        averageUp_ += up;
        averageDown_ += down;
    }

    if (samplesInside_ == 0)
        return 0;
    averageUp_ /= samplesInside_;
    averageDown_ /= samplesInside_;

    return averageUp_ - averageDown_;
}

// ============================================================================
// EdgeIntensityPolygon
// ============================================================================

EdgeIntensityPolygon::EdgeIntensityPolygon(double cornerOffset,
                                             double tangentDistance,
                                             int32_t numSamples)
    : cornerOffset_(cornerOffset),
      tangentDistance_(tangentDistance),
      scorer_(numSamples) {}

bool EdgeIntensityPolygon::computeEdge(const std::vector<cv::Point2d>& polygon,
                                        bool ccw) {
    averageInside_ = 0;
    averageOutside_ = 0;

    double tangentSign = ccw ? 1 : -1;

    int32_t totalSides = 0;
    int32_t N = static_cast<int32_t>(polygon.size());
    for (int32_t i = N - 1, j = 0; j < N; i = j, j++) {

        const cv::Point2d& a = polygon[static_cast<std::size_t>(i)];
        const cv::Point2d& b = polygon[static_cast<std::size_t>(j)];

        double dx = b.x - a.x;
        double dy = b.y - a.y;
        double t = std::sqrt(dx * dx + dy * dy);
        dx /= t;
        dy /= t;

        // see if the side is too small
        if (t < 3 * cornerOffset_) {
            offsetA_ = a;
            offsetB_ = b;
        } else {
            offsetA_.x = a.x + cornerOffset_ * dx;
            offsetA_.y = a.y + cornerOffset_ * dy;

            offsetB_.x = b.x - cornerOffset_ * dx;
            offsetB_.y = b.y - cornerOffset_ * dy;
        }

        double tanX = -dy * tangentDistance_ * tangentSign;
        double tanY = dx * tangentDistance_ * tangentSign;

        scorer_.computeAverageDerivative(offsetA_, offsetB_, tanX, tanY);

        if (scorer_.getSamplesInside() > 0) {
            totalSides++;
            averageInside_ += scorer_.getAverageUp() / tangentDistance_;
            averageOutside_ += scorer_.getAverageDown() / tangentDistance_;
        }
    }

    if (totalSides > 0) {
        averageInside_ /= totalSides;
        averageOutside_ /= totalSides;
    } else {
        averageInside_ = averageOutside_ = 0;
        return false;
    }

    return true;
}

// ============================================================================
// AdjustPolygonForThresholdBias
// ============================================================================

void AdjustPolygonForThresholdBias::process(std::vector<cv::Point2d>& polygon,
                                              bool clockwise) {
    int32_t N = static_cast<int32_t>(polygon.size());
    segments_.assign(static_cast<std::size_t>(N), LineSeg2D{});

    // Apply the adjustment independently to each side
    for (int32_t i = N - 1, j = 0; j < N; i = j, j++) {
        int32_t ii, jj;
        if (clockwise) {
            ii = i;
            jj = j;
        } else {
            ii = j;
            jj = i;
        }

        const cv::Point2d& a = polygon[static_cast<std::size_t>(ii)];
        const cv::Point2d& b = polygon[static_cast<std::size_t>(jj)];

        double dx = b.x - a.x;
        double dy = b.y - a.y;
        double l = std::sqrt(dx * dx + dy * dy);
        if (l == 0) {
            throw std::runtime_error("Two identical corners!");
        }

        // only needs to be shifted in two directions
        if (dx < 0)
            dx = 0;
        if (dy > 0)
            dy = 0;

        LineSeg2D& s = segments_[static_cast<std::size_t>(ii)];
        s.a.x = a.x - dy / l;
        s.a.y = a.y + dx / l;
        s.b.x = b.x - dy / l;
        s.b.y = b.y + dx / l;
    }

    // Find the intersection between the adjusted lines to convert it back into polygon format
    LineGeneral2D ga, gb;
    cv::Point2d intersection;
    for (int32_t i = N - 1, j = 0; j < N; i = j, j++) {
        int32_t ii, jj;
        if (clockwise) {
            ii = i;
            jj = j;
        } else {
            ii = j;
            jj = i;
        }

        lineGeneralFromSegment(segments_[static_cast<std::size_t>(ii)].a,
                                segments_[static_cast<std::size_t>(ii)].b, ga);
        lineGeneralFromSegment(segments_[static_cast<std::size_t>(jj)].a,
                                segments_[static_cast<std::size_t>(jj)].b, gb);

        if (lineGeneralIntersection(ga, gb, intersection)) {
            // very acute angles can cause a large delta. This is conservative and prevents that
            cv::Point2d& pj = polygon[static_cast<std::size_t>(jj)];
            double ddx = intersection.x - pj.x;
            double ddy = intersection.y - pj.y;
            if (ddx * ddx + ddy * ddy < 20.0) {
                pj = intersection;
            }
        }
    }

    // if two corners have a distance of 1 there are some conditions which exist where the corners can be shifted
    // such that two points will now be equal. Avoiding the shift isn't a good idea shift the shift should happen
    // there might be a more elegant solution to this problem but this is probably the simplest
    removeAdjacentDuplicatesPolygon(polygon, 1e-8);
}

// ============================================================================
// DetectPolygonBinaryGrayRefine
// ============================================================================

DetectPolygonBinaryGrayRefine::DetectPolygonBinaryGrayRefine(
    std::unique_ptr<DetectPolygonFromContour> detector,
    std::shared_ptr<RefinePolygonToGray> refineGray,
    double minimumRefineEdgeIntensity, bool adjustForThresholdBias)
    : detector_(std::move(detector)),
      refineGray_(std::move(refineGray)),
      minimumRefineEdgeIntensity_(minimumRefineEdgeIntensity),
      // Mirror Java's `EdgeIntensityPolygon<>(1, 1.5, 15, …)` ctor in the wrapper.
      edgeIntensity_(1, 1.5, 15) {
    if (adjustForThresholdBias) {
        adjustForBias_ = std::make_unique<AdjustPolygonForThresholdBias>();
    }
}

void DetectPolygonBinaryGrayRefine::process(const cv::Mat& gray,
                                              const cv::Mat& binary) {
    detector_->process(gray, binary);
    if (refineGray_)
        refineGray_->setImage(gray);
    edgeIntensity_.setImage(gray);

    auto time0 = std::chrono::steady_clock::now();

    // detector's foundInfo is a value-typed vector; we mutate it in
    // place (Java mutates the same DogArray<Info>).
    auto& detections = detector_->getMutableFoundInfo();

    if (adjustForBias_) {
        int32_t minSides = getMinimumSides();
        for (int32_t i = static_cast<int32_t>(detections.size()) - 1; i >= 0; i--) {
            std::vector<cv::Point2d>& p =
                detections[static_cast<std::size_t>(i)].polygon;
            adjustForBias_->process(p, detector_->isOutputClockwiseUpY());

            // When the polygon is adjusted for bias a point might need to be removed because it's
            // almost parallel. This could cause the shape to have too few corners and needs to be removed.
            if (static_cast<int32_t>(p.size()) < minSides)
                detections.erase(detections.begin() + i);
        }
    }
    auto time1 = std::chrono::steady_clock::now();

    double milli = std::chrono::duration<double, std::milli>(time1 - time0).count();
    movingAverageUpdate(milliAdjustBias_, milli, 0.8);
}

bool DetectPolygonBinaryGrayRefine::refine(
    DetectPolygonFromContour::DetectedInfo& info) {
    double before, after;
    if (edgeIntensity_.computeEdge(info.polygon,
                                    !detector_->isOutputClockwiseUpY())) {
        before = edgeIntensity_.getAverageOutside() -
                 edgeIntensity_.getAverageInside();
    } else {
        return false;
    }

    bool success = false;

    // RefinePolygonToContour branch: not ported (refineContour=null in
    // QR factory).

    if (refineGray_) {
        work_.assign(info.polygon.size(), cv::Point2d());
        if (refineGray_->refine(info.polygon, work_)) {
            if (edgeIntensity_.computeEdge(work_,
                                            !detector_->isOutputClockwiseUpY())) {
                after = edgeIntensity_.getAverageOutside() -
                        edgeIntensity_.getAverageInside();

                // basically, unless it diverged stick with this optimization
                // a near tie
                if (after * 1.5 > before) {
                    info.edgeInside = edgeIntensity_.getAverageInside();
                    info.edgeOutside = edgeIntensity_.getAverageOutside();
                    info.polygon = work_;
                    success = true;
                }
            }
        }
    }

    return success;
}

void DetectPolygonBinaryGrayRefine::refineAll() {
    auto& detections = detector_->getMutableFoundInfo();

    for (std::size_t i = 0; i < detections.size(); i++) {
        refine(detections[i]);
    }
}

std::vector<std::vector<cv::Point2d>> DetectPolygonBinaryGrayRefine::getPolygons(
    std::vector<DetectPolygonFromContour::DetectedInfo*>* storageInfo) {
    std::vector<std::vector<cv::Point2d>> storage;
    if (storageInfo) storageInfo->clear();

    auto& detections = detector_->getMutableFoundInfo();
    for (std::size_t i = 0; i < detections.size(); i++) {
        DetectPolygonFromContour::DetectedInfo& d = detections[i];

        if (d.computeEdgeIntensity() >= minimumRefineEdgeIntensity_) {
            storage.push_back(d.polygon);
            if (storageInfo) storageInfo->push_back(&d);
        }
    }
    return storage;
}

}  // namespace boofcv_qr
