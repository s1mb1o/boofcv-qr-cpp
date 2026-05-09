// Port of boofcv.alg.shapes.polygon.DetectPolygonFromContour
// + boofcv.alg.shapes.polygon.ContourEdgeIntensity (BoofCV v1.3.0).
//
// Step 7b (part 2). Verbatim per CLAUDE.md "Verbatim vs idiomize" —
// the algorithmic core preserves the Java loop structure / variable
// names / comments. Plumbing (factories, polygon-storage shapes) is
// idiomatic C++17.
//
// See src/polygon/detect_polygon_from_contour.md for the algorithm
// explanation.

#include "boofcv_qr/polygon/detect_polygon_from_contour.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace boofcv_qr {

namespace {

// Mirror of `georegression.geometry.UtilPolygons2D_I32.isPositiveZ`.
//
// FIXME(parity): see the same-named helper in polyline_split_merge.cpp
// for the int-vs-int64 wrap-on-overflow rationale. Same widening here.
bool isPositiveZ(const cv::Point2i& a, const cv::Point2i& b, const cv::Point2i& c) {
    int32_t dx0 = a.x - b.x;
    int32_t dy0 = a.y - b.y;
    int32_t dx1 = c.x - b.x;
    int32_t dy1 = c.y - b.y;
    int64_t z = static_cast<int64_t>(dx0) * dy1 - static_cast<int64_t>(dy0) * dx1;
    return z > 0;
}

// Mirror of `georegression.geometry.UtilPolygons2D_I32.isCCW(List)`.
bool isCCW(const std::vector<cv::Point2i>& polygon) {
    int32_t N = static_cast<int32_t>(polygon.size());
    int32_t sign = 0;
    for (int32_t i = 0; i < N; i++) {
        int32_t j = (i + 1) % N;
        int32_t k = (i + 2) % N;
        sign = isPositiveZ(polygon[static_cast<std::size_t>(i)],
                           polygon[static_cast<std::size_t>(j)],
                           polygon[static_cast<std::size_t>(k)])
                   ? sign + 1
                   : sign - 1;
    }
    return sign < 0;
}

// Mirror of `georegression.metric.Area2D_F64.polygonSimple(Polygon2D_F64)`.
// Same line-by-line shape as the Java; the absolute-value at the end
// makes the result winding-independent.
double polygonSimpleArea(const std::vector<cv::Point2d>& poly) {
    double total = 0.0;
    cv::Point2d v0 = poly[0];
    cv::Point2d v1 = poly[1];
    for (std::size_t i = 2; i < poly.size(); i++) {
        cv::Point2d v2 = poly[i];
        total += v1.x * (v2.y - v0.y);
        v0 = v1;
        v1 = v2;
    }
    cv::Point2d v22 = poly[0];
    total += v1.x * (v22.y - v0.y);
    cv::Point2d v02 = v1;
    total += v22.x * (poly[1].y - v02.y);
    return std::fabs(total / 2.0);
}

// Mirror of `boofcv.misc.MovingAverage` exponential decay update.
inline void movingAverageUpdate(double& avg, double sample, double decay) {
    avg = avg * decay + sample * (1.0 - decay);
}

}  // namespace

// ============================================================================
// ContourEdgeIntensity
// ============================================================================

ContourEdgeIntensity::ContourEdgeIntensity(int32_t contourSamples,
                                            int32_t tangentSamples,
                                            double tangentStep)
    : contourSamples_(contourSamples),
      tangentSamples_(tangentSamples),
      tangentStep_(static_cast<float>(tangentStep)) {}

void ContourEdgeIntensity::setImage(const cv::Mat& image) {
    if (image.type() != CV_8UC1) {
        throw std::invalid_argument(
            "ContourEdgeIntensity: image must be CV_8UC1");
    }
    image_ = image;
    imageWidth_ = image.cols;
    imageHeight_ = image.rows;
}

float ContourEdgeIntensity::sample(float x, float y) const {
    // BorderType.EXTENDED == cv::BORDER_REPLICATE: clamp (x, y) to image
    // bounds before bilinear sampling. Mirrors `InterpolatePixelS` with
    // EXTENDED border policy.
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > static_cast<float>(imageWidth_ - 1))
        x = static_cast<float>(imageWidth_ - 1);
    if (y > static_cast<float>(imageHeight_ - 1))
        y = static_cast<float>(imageHeight_ - 1);

    int32_t x0 = static_cast<int32_t>(std::floor(x));
    int32_t y0 = static_cast<int32_t>(std::floor(y));
    int32_t x1 = std::min(x0 + 1, imageWidth_ - 1);
    int32_t y1 = std::min(y0 + 1, imageHeight_ - 1);
    float ax = x - static_cast<float>(x0);
    float ay = y - static_cast<float>(y0);

    float v00 = static_cast<float>(image_.at<uint8_t>(y0, x0));
    float v10 = static_cast<float>(image_.at<uint8_t>(y0, x1));
    float v01 = static_cast<float>(image_.at<uint8_t>(y1, x0));
    float v11 = static_cast<float>(image_.at<uint8_t>(y1, x1));

    float v0 = v00 + (v10 - v00) * ax;
    float v1 = v01 + (v11 - v01) * ax;
    return v0 + (v1 - v0) * ay;
}

void ContourEdgeIntensity::process(const std::vector<cv::Point2i>& contour,
                                    bool isCCW_) {
    if (imageWidth_ == 0)
        throw std::runtime_error("You didn't call setImage()");

    // How many pixels along the contour it will step between samples
    int32_t step;
    if (static_cast<int32_t>(contour.size()) <= contourSamples_)
        step = 1;
    else
        step = static_cast<int32_t>(contour.size()) / contourSamples_;

    // Want the local tangent. How many contour points forward it will sample to get the tangent
    int32_t sample = std::max(1, std::min(step / 2, 5));

    edgeOutsideAverage_ = edgeInsideAverage_ = 0;
    int32_t totalInside = 0;
    int32_t totalOutside = 0;

    // traverse the contour
    for (int32_t i = 0; i < static_cast<int32_t>(contour.size()); i += step) {
        const cv::Point2i& a = contour[static_cast<std::size_t>(i)];
        const cv::Point2i& b = contour[static_cast<std::size_t>(
            (i + sample) % static_cast<int32_t>(contour.size()))];

        // compute the tangent using the two points
        float dx = static_cast<float>(b.x - a.x);
        float dy = static_cast<float>(b.y - a.y);
        float r = std::sqrt(dx * dx + dy * dy);
        dx /= r;
        dy /= r;

        // sample points tangent to the contour but not the contour itself
        for (int32_t j = 0; j < tangentSamples_; j++) {
            float x, y;
            float length = static_cast<float>(j + 1) * tangentStep_;

            x = static_cast<float>(a.x) + length * dy;
            y = static_cast<float>(a.y) - length * dx;
            if (x >= 0 && y >= 0 && x <= static_cast<float>(imageWidth_ - 1) &&
                y <= static_cast<float>(imageHeight_ - 1)) {
                edgeOutsideAverage_ += this->sample(x, y);
                totalOutside++;
            }

            x = static_cast<float>(a.x) - length * dy;
            y = static_cast<float>(a.y) + length * dx;
            if (x >= 0 && y >= 0 && x <= static_cast<float>(imageWidth_ - 1) &&
                y <= static_cast<float>(imageHeight_ - 1)) {
                edgeInsideAverage_ += this->sample(x, y);
                totalInside++;
            }
        }
    }

    if (totalOutside > 0) edgeOutsideAverage_ /= static_cast<float>(totalOutside);
    if (totalInside > 0) edgeInsideAverage_ /= static_cast<float>(totalInside);

    if (!isCCW_) {
        float tmp = edgeOutsideAverage_;
        edgeOutsideAverage_ = edgeInsideAverage_;
        edgeInsideAverage_ = tmp;
    }
}

// ============================================================================
// DetectPolygonFromContour
// ============================================================================

DetectPolygonFromContour::DetectPolygonFromContour(
    std::unique_ptr<PointsToPolyline> contourToPolyline,
    bool outputClockwiseUpY, bool canTouchBorder, double contourEdgeThreshold,
    double tangentEdgeIntensity)
    : contourToPolyline_(std::move(contourToPolyline)),
      outputClockwiseUpY_(outputClockwiseUpY),
      canTouchBorder_(canTouchBorder),
      contourEdgeThreshold_(contourEdgeThreshold) {
    if (!contourToPolyline_->isLoop())
        throw std::invalid_argument("ContourToPolygon must be configured for loops");

    if (contourEdgeThreshold > 0) {
        contourEdgeIntensity_ = std::make_unique<ContourEdgeIntensity>(
            30, 1, tangentEdgeIntensity);
    }
}

DetectPolygonFromContour::DetectPolygonFromContour() = default;

void DetectPolygonFromContour::setNumberOfSides(int32_t minSides, int32_t maxSides) {
    if (minSides < 3)
        throw std::invalid_argument("The min must be >= 3");
    if (maxSides < minSides)
        throw std::invalid_argument("The max must be >= the min");

    contourToPolyline_->setMinimumSides(minSides);
    contourToPolyline_->setMaximumSides(maxSides);
}

int32_t DetectPolygonFromContour::getMinimumSides() const {
    return contourToPolyline_->getMinimumSides();
}

int32_t DetectPolygonFromContour::getMaximumSides() const {
    return contourToPolyline_->getMaximumSides();
}

void DetectPolygonFromContour::process(const cv::Mat& gray, const cv::Mat& binary) {
    if (gray.cols != binary.cols || gray.rows != binary.rows) {
        throw std::invalid_argument(
            "DetectPolygonFromContour: gray and binary must have the same shape");
    }
    if (gray.type() != CV_8UC1 || binary.type() != CV_8UC1) {
        throw std::invalid_argument(
            "DetectPolygonFromContour: gray and binary must both be CV_8UC1");
    }

    if (imageWidth_ != gray.cols || imageHeight_ != gray.rows)
        configure(gray.cols, gray.rows);

    // reset storage for output. Call reset individually here to ensure that all references are nulled from last time
    for (std::size_t i = 0; i < foundInfo_.size(); i++) {
        foundInfo_[i].reset();
    }
    foundInfo_.clear();

    if (contourEdgeIntensity_)
        contourEdgeIntensity_->setImage(gray);

    auto time0 = std::chrono::steady_clock::now();

    // find all the contours.
    //
    // OpenCV substitution per CLAUDE.md "OpenCV substitution policy":
    // RETR_CCOMP gives a 2-level hierarchy of external + internal
    // contours, matching BoofCV's `Contour { external; internal[]; }`.
    // CHAIN_APPROX_NONE keeps every pixel — the polyline corner finder
    // wants per-pixel ordering, not the simplified version.
    //
    // findContours mutates its input; clone defensively.
    cv::Mat work = binary.clone();
    std::vector<std::vector<cv::Point>> cvContours;
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(work, cvContours, hierarchy, cv::RETR_CCOMP,
                      cv::CHAIN_APPROX_NONE);
    buildContoursFromOpenCV(cvContours, hierarchy);

    auto time1 = std::chrono::steady_clock::now();

    // Using the contours find the polygons
    findCandidateShapes(gray);

    auto time2 = std::chrono::steady_clock::now();

    double a = std::chrono::duration<double, std::milli>(time1 - time0).count();
    double b = std::chrono::duration<double, std::milli>(time2 - time1).count();

    movingAverageUpdate(milliContour_, a, 0.8);
    movingAverageUpdate(milliShapes_, b, 0.8);
}

void DetectPolygonFromContour::configure(int32_t width, int32_t height) {
    this->imageWidth_ = width;
    this->imageHeight_ = height;

    // adjust size based parameters based on image size.
    //
    // ConfigLength.computeNegMaxI returns INT32_MAX on negative
    // thresholds — same as Java. Our local ConfigLength only has
    // computeI; replicate the negative-fallback inline.
    double size = minimumContour_.compute(std::sqrt(static_cast<double>(width) * height));
    int32_t minContour;
    if (size >= 0.0)
        minContour = static_cast<int32_t>(std::lround(size));
    else
        minContour = std::numeric_limits<int32_t>::max();

    this->minimumContourPixels_ = minContour;
    this->minimumContourPixels_ = std::max(4, minimumContourPixels_);  // This is needed to avoid processing zero or other impossible
    this->minimumArea_ = std::pow(this->minimumContourPixels_ / 4.0, 2);

    if (helper_)
        helper_->setImageShape(width, height);
}

void DetectPolygonFromContour::buildContoursFromOpenCV(
    const std::vector<std::vector<cv::Point>>& cvContours,
    const std::vector<cv::Vec4i>& hierarchy) {
    contours_.clear();
    if (cvContours.empty()) return;

    // RETR_CCOMP convention: each cv contour's hierarchy entry is
    // [next, prev, child, parent]. Top-level contours have parent == -1
    // (these are external boundaries). Their children, walked via
    // hierarchy[child][0] (next sibling at the same level), are
    // internal-hole boundaries.
    //
    // Winding direction note: OpenCV's findContours emits external
    // contours CCW in image coords (CW in math), and internal contours
    // CW in image (CCW in math). BoofCV's LinearContourLabelChang2004
    // emits the OPPOSITE windings (external CW in image, internal CCW
    // in image). The polyline corner finder's convex check is tuned for
    // BoofCV's convention — concretely, the `isPositiveZ(a, b, c)`
    // call in `setSplitVariables` rejects splits whose new corner b
    // would form a convex turn under the OpenCV winding, which is
    // exactly the wrong half. Reversing per-contour fixes it.
    auto reverse_in_place = [](std::vector<cv::Point2i>& v) {
        std::reverse(v.begin(), v.end());
    };

    for (std::size_t i = 0; i < cvContours.size(); i++) {
        if (hierarchy[i][3] != -1) continue;  // not a top-level external

        Contour c;
        const auto& src = cvContours[i];
        c.external.reserve(src.size());
        for (const auto& p : src) c.external.emplace_back(p.x, p.y);
        reverse_in_place(c.external);

        for (int32_t child = hierarchy[i][2]; child != -1;
             child = hierarchy[static_cast<std::size_t>(child)][0]) {
            const auto& csrc = cvContours[static_cast<std::size_t>(child)];
            std::vector<cv::Point2i> inner;
            inner.reserve(csrc.size());
            for (const auto& p : csrc) inner.emplace_back(p.x, p.y);
            reverse_in_place(inner);
            c.internal.push_back(std::move(inner));
        }

        contours_.push_back(std::move(c));
    }
}

void DetectPolygonFromContour::findCandidateShapes(const cv::Mat& /*gray*/) {
    // find blobs where all 4 edges are lines
    for (std::size_t i = 0; i < contours_.size(); i++) {
        Contour& c = contours_[i];

        if (static_cast<int32_t>(c.external.size()) < minimumContourPixels_)
            continue;
        float edgeInside = -1, edgeOutside = -1;

        // ignore shapes which touch the image border
        bool touchesBorder_ = touchesBorder(c.external);
        if (!canTouchBorder_ && touchesBorder_) {
            continue;
        }

        if (helper_)
            if (!helper_->filterContour(c.external, touchesBorder_, true))
                continue;

        // filter out contours which are noise
        if (contourEdgeIntensity_) {
            contourEdgeIntensity_->process(c.external, true);
            edgeInside = contourEdgeIntensity_->getEdgeInsideAverage();
            edgeOutside = contourEdgeIntensity_->getEdgeOutsideAverage();

            // take the ABS because CCW/CW isn't known yet
            if (std::fabs(edgeOutside - edgeInside) < contourEdgeThreshold_) {
                continue;
            }
        }

        // remove lens distortion — deferred. Until lens distortion lands
        // the undistorted contour is the same as the distorted one.
        const std::vector<cv::Point2i>& undistorted = c.external;

        if (helper_) {
            helper_->configureBeforePolyline(*contourToPolyline_, touchesBorder_);
        }

        // Find the initial approximate fit of a polygon to the contour
        if (!contourToPolyline_->process(undistorted, splits_)) {
            continue;
        }

        // determine the polygon's orientation
        polygonPixel_.clear();
        for (std::size_t j = 0; j < splits_.size(); j++) {
            polygonPixel_.push_back(undistorted[static_cast<std::size_t>(
                splits_[j])]);
        }

        // Note the CCW here uses a standard geometric coordinate system with +y up, not +y down
        bool isCCW_ = isCCW(polygonPixel_);

        // Now that the orientation is known it can check to see if it's actually trying to fit to a
        // white blob instead of a black blob
        if (contourEdgeIntensity_) {
            // before it assumed it was CCW
            if (!isCCW_) {
                float tmp = edgeInside;
                edgeInside = edgeOutside;
                edgeOutside = tmp;
            }

            if (edgeInside > edgeOutside) {
                continue;
            }
        }

        // see if it should be flipped so that the polygon has the correct orientation
        if (outputClockwiseUpY_ == isCCW_) {
            flip(splits_);
        }

        // convert the format of the initial crude polygon
        polygonWork_.assign(splits_.size(), cv::Point2d());
        polygonDistorted_.assign(splits_.size(), cv::Point2d());
        for (std::size_t j = 0; j < splits_.size(); j++) {
            const cv::Point2i& p = undistorted[static_cast<std::size_t>(splits_[j])];
            const cv::Point2i& q = c.external[static_cast<std::size_t>(splits_[j])];
            polygonWork_[j] = cv::Point2d(p.x, p.y);
            polygonDistorted_[j] = cv::Point2d(q.x, q.y);
        }

        if (touchesBorder_) {
            determineCornersOnBorder(polygonDistorted_, borderCorners_);
        } else {
            borderCorners_.clear();
        }

        if (helper_) {
            if (!helper_->filterPixelPolygon(polygonWork_, polygonDistorted_,
                                              borderCorners_, touchesBorder_)) {
                continue;
            }
        }

        // make sure it's big enough
        double area = polygonSimpleArea(polygonWork_);

        if (area < minimumArea_) {
            continue;
        }

        // Get the storage for a new polygon. Mirror Java's foundInfo.grow()
        // by appending and filling.
        foundInfo_.emplace_back();
        DetectedInfo& info = foundInfo_.back();

        // save results
        info.splits = splits_;
        info.contourTouchesBorder = touchesBorder_;
        info.external = true;
        info.edgeInside = edgeInside;
        info.edgeOutside = edgeOutside;
        info.contour = c;
        info.polygon = polygonWork_;
        info.polygonDistorted = polygonDistorted_;
        info.borderCorners = borderCorners_;
    }
}

void DetectPolygonFromContour::flip(std::vector<int32_t>& a) {
    // TODO move into ddogleg? primitive flip  <--- I think this is specific to polygons
    int32_t N = static_cast<int32_t>(a.size());
    int32_t H = N / 2;
    for (int32_t i = 1; i <= H; i++) {
        int32_t j = N - i;
        int32_t tmp = a[static_cast<std::size_t>(i)];
        a[static_cast<std::size_t>(i)] = a[static_cast<std::size_t>(j)];
        a[static_cast<std::size_t>(j)] = tmp;
    }
}

void DetectPolygonFromContour::determineCornersOnBorder(
    const std::vector<cv::Point2d>& polygon,
    std::vector<uint8_t>& onImageBorder) const {
    onImageBorder.clear();
    for (std::size_t i = 0; i < polygon.size(); i++) {
        const cv::Point2d& p = polygon[i];

        onImageBorder.push_back(p.x <= 1 || p.y <= 1 ||
                                 p.x >= imageWidth_ - 2 ||
                                 p.y >= imageHeight_ - 2);
    }
}

bool DetectPolygonFromContour::touchesBorder(
    const std::vector<cv::Point2i>& contour) const {
    int32_t endX = imageWidth_ - 1;
    int32_t endY = imageHeight_ - 1;

    for (std::size_t j = 0; j < contour.size(); j++) {
        const cv::Point2i& p = contour[j];
        if (p.x == 0 || p.y == 0 || p.x == endX || p.y == endY) {
            return true;
        }
    }

    return false;
}

}  // namespace boofcv_qr
