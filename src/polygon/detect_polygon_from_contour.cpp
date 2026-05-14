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

#include <chrono>
#include <cmath>
#include <stdexcept>
#include <utility>

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

    return sampleInside(x, y);
}

float ContourEdgeIntensity::sampleInside(float x, float y) const {
    int32_t x0 = static_cast<int32_t>(x);
    int32_t y0 = static_cast<int32_t>(y);
    int32_t x1 = std::min(x0 + 1, imageWidth_ - 1);
    int32_t y1 = std::min(y0 + 1, imageHeight_ - 1);
    float ax = x - static_cast<float>(x0);
    float ay = y - static_cast<float>(y0);

    const uint8_t* row0 = image_.ptr<uint8_t>(y0);
    const uint8_t* row1 = image_.ptr<uint8_t>(y1);
    float v00 = static_cast<float>(row0[x0]);
    float v10 = static_cast<float>(row0[x1]);
    float v01 = static_cast<float>(row1[x0]);
    float v11 = static_cast<float>(row1[x1]);

    float v0 = v00 + (v10 - v00) * ax;
    float v1 = v01 + (v11 - v01) * ax;
    return v0 + (v1 - v0) * ay;
}

void ContourEdgeIntensity::process(const std::vector<cv::Point2i>& contour,
                                    bool isCCW_) {
    if (imageWidth_ == 0)
        throw std::runtime_error("You didn't call setImage()");

    const int32_t contourSize = static_cast<int32_t>(contour.size());
    const float maxX = static_cast<float>(imageWidth_ - 1);
    const float maxY = static_cast<float>(imageHeight_ - 1);

    // How many pixels along the contour it will step between samples
    int32_t step;
    if (contourSize <= contourSamples_)
        step = 1;
    else
        step = contourSize / contourSamples_;

    // Want the local tangent. How many contour points forward it will sample to get the tangent
    int32_t sample = std::max(1, std::min(step / 2, 5));

    edgeOutsideAverage_ = edgeInsideAverage_ = 0;
    int32_t totalInside = 0;
    int32_t totalOutside = 0;

    // traverse the contour
    for (int32_t i = 0; i < contourSize; i += step) {
        const cv::Point2i& a = contour[static_cast<std::size_t>(i)];
        const cv::Point2i& b = contour[static_cast<std::size_t>(
            (i + sample) % contourSize)];

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
            if (x >= 0 && y >= 0 && x <= maxX && y <= maxY) {
                edgeOutsideAverage_ += sampleInside(x, y);
                totalOutside++;
            }

            x = static_cast<float>(a.x) - length * dy;
            y = static_cast<float>(a.y) + length * dx;
            if (x >= 0 && y >= 0 && x <= maxX && y <= maxY) {
                edgeInsideAverage_ += sampleInside(x, y);
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
    // Cycle B of the LinearContour port (closes ADR 01). Pre-port we
    // ran `cv::findContours(RETR_CCOMP, CHAIN_APPROX_NONE)` and
    // post-processed each contour with a reversal + a topmost-leftmost
    // start-pixel rotation to recover BoofCV's winding + scan order.
    // The new `LinearContourLabelChang2004` emits BoofCV-native pixel
    // ordering directly — no fix-up needed.
    //
    // Push only `maxContour` into the labeller — mirrors Java's
    // `BinaryContourFinder.setMaxContour` from
    // `SquareLocatorPatternDetectorBase.configureContourDetector`.
    // The `minContour` filter stays downstream in
    // `findCandidateShapes` to match Java's flow exactly (Java's
    // `LinearContourLabelChang2004.minContourLength` defaults to
    // `fixed(0)` = no floor, and BoofCV doesn't override it).
    contourLabeller_.setMaxContourLength(maximumContour_);
    contourLabeller_.setSaveInternalContours(saveInternalContours_);
    contourLabeller_.process(binary, labeled_);
    buildContoursFromPort();

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

    // Same `computeNegMaxI` behaviour for the upper cap — negative
    // threshold means "no cap" (INT32_MAX). Mirrors Java's
    // `BinaryContourFinder.setMaxContour(ConfigLength)` which feeds
    // through `computeNegMaxI`.
    double maxSize = maximumContour_.compute(std::sqrt(static_cast<double>(width) * height));
    if (maxSize >= 0.0)
        this->maximumContourPixels_ = static_cast<int32_t>(std::lround(maxSize));
    else
        this->maximumContourPixels_ = std::numeric_limits<int32_t>::max();

    if (helper_)
        helper_->setImageShape(width, height);
}

void DetectPolygonFromContour::buildContoursFromPort() {
    contours_.clear();

    // The labeller stores its output in two parallel structures: per-
    // blob `ContourPacked` headers (`getContours()`) carrying the
    // blob id + set indices, and a global `PackedSetsPoint2D_I32` that
    // holds the actual (x,y) coordinates one set per contour.
    //
    // Winding + start pixel are BoofCV-native by construction (see
    // `src/binary/linear_contour_label_chang2004.md` "Why CW external
    // / CCW internal" and "Why this approach over alternatives"). No
    // reversal or rotation is required, in contrast to the prior
    // `cv::findContours`-based path that landed at commit `770f210`.
    //
    // Wiped externals: the labeller replaces a set with an empty one
    // when its pixel count violates min/max. Skip those entries so
    // downstream stages don't see phantom 0-point contours.
    const auto& cps = contourLabeller_.getContours();
    const auto& packed = contourLabeller_.getPackedPoints();

    contours_.reserve(cps.size());
    for (std::size_t i = 0; i < cps.size(); i++) {
        const ContourPacked& cp = cps[i];

        int32_t extSize = packed.sizeOfSet(cp.externalIndex);
        if (extSize == 0) continue;  // wiped: too long or too short

        Contour c;

        c.external.reserve(static_cast<std::size_t>(extSize));
        packed.appendSetTo(cp.externalIndex, c.external);

        if (saveInternalContours_) {
            c.internal.reserve(cp.internalIndexes.size());
            for (std::size_t j = 0; j < cp.internalIndexes.size(); j++) {
                int32_t innerIdx = cp.internalIndexes[j];
                int32_t innerSize = packed.sizeOfSet(innerIdx);
                std::vector<cv::Point2i> inner;
                inner.reserve(static_cast<std::size_t>(innerSize));
                packed.appendSetTo(innerIdx, inner);
                c.internal.push_back(std::move(inner));
            }
        }

        contours_.push_back(std::move(c));
    }
}

void DetectPolygonFromContour::findCandidateShapes(const cv::Mat& /*gray*/) {
    // find blobs where all 4 edges are lines
    for (std::size_t i = 0; i < contours_.size(); i++) {
        Contour& c = contours_[i];

        // Mirror Java's `DetectPolygonFromContour.findCandidateShapes`
        // pre-filter: discard contours below the min. The upper cap
        // is enforced upstream inside `LinearContourLabelChang2004`
        // (we push `maxContour` into the labeller in `process()`,
        // mirroring Java's `BinaryContourFinder.setMaxContour`); over-
        // cap contours come back as empty externals and are skipped
        // by `buildContoursFromPort`. A downstream `>` check here
        // would diverge from Java AND interact badly with the cached
        // `maximumContourPixels_` (only refreshed on image-shape
        // change), so it has been removed.
        int32_t contourSize = static_cast<int32_t>(c.external.size());
        if (contourSize < minimumContourPixels_)
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
        info.contour = std::move(c);
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

void DetectPolygonFromContour::releaseScratch() {
    splits_.clear();
    borderCorners_.clear();
    polygonWork_.clear();
    polygonDistorted_.clear();
    contourTmp_.clear();
    polygonPixel_.clear();
    foundInfo_.clear();
    contours_.clear();

    std::vector<int32_t>().swap(splits_);
    std::vector<uint8_t>().swap(borderCorners_);
    std::vector<cv::Point2d>().swap(polygonWork_);
    std::vector<cv::Point2d>().swap(polygonDistorted_);
    std::vector<cv::Point2i>().swap(contourTmp_);
    std::vector<cv::Point2i>().swap(polygonPixel_);
    std::vector<DetectedInfo>().swap(foundInfo_);
    std::vector<Contour>().swap(contours_);

    labeled_.release();
    contourLabeller_.releaseScratch();
    if (contourEdgeIntensity_)
        contourEdgeIntensity_->releaseImage();
}

}  // namespace boofcv_qr
