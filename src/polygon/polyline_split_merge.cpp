// Port of boofcv.alg.shapes.polyline.splitmerge.PolylineSplitMerge
// (and MaximumLineDistance from the same package). BoofCV v1.3.0.
// Verbatim per CLAUDE.md "Verbatim vs idiomize" — preserve loop
// structure, variable names, comments. Only the inner-class containers
// (DogLinkedList<Corner>, DogArray<Corner>, DogArray<CandidatePolyline>)
// are realised in idiomatic C++ shapes; the algorithmic core is byte-for-
// byte the same as Java.
//
// Inlined helpers cite their georegression / boofcv-ip origin.

#include "boofcv_qr/polyline/polyline_split_merge.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace boofcv_qr {

namespace {

// Mirror of `boofcv.misc.CircularIndex.distanceP(int, int, int)`. Positive
// directional distance from index0 to index1 in a circular buffer of size.
int32_t circularDistanceP(int32_t index0, int32_t index1, int32_t size) {
    int32_t difference = index1 - index0;
    if (difference < 0) {
        difference = size + difference;
    }
    return difference;
}

// Mirror of `boofcv.misc.CircularIndex.plusPOffset(int, int, int)`.
int32_t circularPlusPOffset(int32_t index, int32_t offset, int32_t size) {
    return (index + offset) % size;
}

// Mirror of `boofcv.misc.CircularIndex.minusPOffset(int, int, int)`.
int32_t circularMinusPOffset(int32_t index, int32_t offset, int32_t size) {
    index -= offset;
    if (index < 0) {
        return size + index;
    } else {
        return index;
    }
}

// Mirror of `georegression.metric.Distance2D_F64.distanceSq(LineParametric2D_F64, x, y)`.
// Uses the scaled `closestPointT` form to mitigate slope-magnitude-induced
// rounding (matches the upstream byte-for-byte).
double lineParametricDistanceSq(double px, double py, double sx, double sy,
                                 double x, double y) {
    double scale = std::max(std::fabs(sx), std::fabs(sy));
    double sx_n = sx / scale;
    double sy_n = sy / scale;
    double t = (sx_n * (x - px) + sy_n * (y - py)) /
               (sx_n * sx_n + sy_n * sy_n);
    double a = (sx / scale) * t + px;
    double b = (sy / scale) * t + py;
    double dx = x - a;
    double dy = y - b;
    return dx * dx + dy * dy;
}

// Mirror of `georegression.metric.Distance2D_F64.distanceLineSq(...)` — the
// implementation behind `distanceSq(LineSegment2D_F64, x, y)`.
double lineSegmentDistanceSq(double ax, double ay, double bx, double by,
                              double px, double py) {
    double ab_x = bx - ax;
    double ab_y = by - ay;
    double t = (ab_x * (px - ax) + ab_y * (py - ay)) /
               (ab_x * ab_x + ab_y * ab_y);
    if (t < 0.0) {
        double dx = ax - px, dy = ay - py;
        return dx * dx + dy * dy;
    }
    if (t > 1.0) {
        double dx = bx - px, dy = by - py;
        return dx * dx + dy * dy;
    }
    double cx = ax + t * ab_x;
    double cy = ay + t * ab_y;
    double dx = cx - px, dy = cy - py;
    return dx * dx + dy * dy;
}

// Mirror of `georegression.geometry.UtilPolygons2D_I32.isPositiveZ`.
//
// FIXME(parity): Java's UtilPolygons2D_I32.isPositiveZ computes this cross
// product in 32-bit `int` and relies on defined signed overflow; C++ signed
// overflow is UB so we widen to int64_t. For contour-pixel deltas under
// ~46k the result is identical; QR images in our regression set never come
// close. If a future input is large enough to matter, mirror Java's wrap
// deterministically with `static_cast<int32_t>(int64_product)`.
bool isPositiveZ(const cv::Point2i& a, const cv::Point2i& b, const cv::Point2i& c) {
    int32_t dx0 = a.x - b.x;
    int32_t dy0 = a.y - b.y;
    int32_t dx1 = c.x - b.x;
    int32_t dy1 = c.y - b.y;
    int64_t z = static_cast<int64_t>(dx0) * dy1 - static_cast<int64_t>(dy0) * dx1;
    return z > 0;
}

}  // namespace

// ============================================================================
// ConfigLength
// ============================================================================

int32_t ConfigLength::computeI(double totalLength) const {
    double size = compute(totalLength);
    if (size >= 0.0) {
        return static_cast<int32_t>(std::lround(size));
    } else {
        throw std::invalid_argument(
            "ConfigLength::computeI: threshold was set to a negative value");
    }
}

// ============================================================================
// MaximumLineDistance
// ============================================================================

void MaximumLineDistance::selectSplitPoint(const std::vector<cv::Point2i>& contour,
                                           int32_t indexA, int32_t indexB,
                                           Results& results) {
    // Mirror of `PolylineSplitMerge.assignLine(contour, indexA, indexB, line)`
    // for LineParametric2D_F64 — inlined here so we don't need the static
    // assignLine entry point on this side.
    {
        const cv::Point2i& endA = contour[static_cast<std::size_t>(indexA)];
        const cv::Point2i& endB = contour[static_cast<std::size_t>(indexB)];
        line.px = endA.x;
        line.py = endA.y;
        line.sx = endB.x - endA.x;
        line.sy = endB.y - endA.y;
    }

    if (indexB >= indexA) {
        results.index = indexA;
        results.score = -1;
        for (int32_t i = indexA + 1; i < indexB; i++) {
            const cv::Point2i& p = contour[static_cast<std::size_t>(i)];
            double distanceSq = lineParametricDistanceSq(line.px, line.py,
                                                         line.sx, line.sy,
                                                         p.x, p.y);

            if (distanceSq > results.score) {
                results.score = distanceSq;
                results.index = i;
            }
        }
    } else {
        results.index = indexA;
        results.score = -1;
        int32_t distance = static_cast<int32_t>(contour.size()) - indexA + indexB;
        for (int32_t i = 1; i < distance; i++) {
            int32_t index = (indexA + i) % static_cast<int32_t>(contour.size());
            const cv::Point2i& p = contour[static_cast<std::size_t>(index)];
            double distanceSq = lineParametricDistanceSq(line.px, line.py,
                                                         line.sx, line.sy,
                                                         p.x, p.y);

            if (distanceSq > results.score) {
                results.score = distanceSq;
                results.index = index;
            }
        }
    }

    //		if( results.index >= contour.size() )
    //			throw new RuntimeException("Egads");
}

int32_t MaximumLineDistance::compareScore(double scoreA, double scoreB) {
    if (scoreA > scoreB)
        return 1;
    else if (scoreA < scoreB)
        return -1;
    else
        return 0;
}

// ============================================================================
// PolylineSplitMerge::CornerPool
// ============================================================================

PolylineSplitMerge::Corner* PolylineSplitMerge::CornerPool::grow() {
    storage_.push_back(std::make_unique<Corner>());
    return storage_.back().get();
}

void PolylineSplitMerge::CornerPool::reset() {
    storage_.clear();
}

// ============================================================================
// PolylineSplitMerge — algorithmic core (verbatim port)
// ============================================================================

bool PolylineSplitMerge::process(const std::vector<cv::Point2i>& contour) {
    // Reset internal book keeping variables
    reset();

    if (loops_) {
        // Reject pathological case
        if (contour.size() < 3)
            return false;

        if (!findInitialTriangle(contour))
            return false;
    } else {
        // Reject pathological case
        if (contour.size() < 2)
            return false;

        // two end points are the seeds. Plus they can't change
        addCorner(0);
        addCorner(static_cast<int32_t>(contour.size()) - 1);
        initializeScore(contour, false);
    }
    savePolyline();

    sequentialSideFit(contour, loops_);

    if (fatalError_)
        return false;

    int32_t MIN_SIZE = loops_ ? 3 : 2;

    double bestScore = std::numeric_limits<double>::max();
    int32_t bestSize = -1;
    // Widen to int64 to mirror Java's wrap-on-overflow semantics for
    // `maxSides - (MIN_SIZE - 1)` when `maxSides == INT_MAX`.
    int64_t cap64 = static_cast<int64_t>(maxSides_) - (MIN_SIZE - 1);
    int32_t cap = cap64 > std::numeric_limits<int32_t>::max()
                      ? std::numeric_limits<int32_t>::max()
                      : static_cast<int32_t>(cap64);
    int32_t limit = std::min(cap, static_cast<int32_t>(polylines_.size()));
    for (int32_t i = 0; i < limit; i++) {
        if (polylines_[static_cast<std::size_t>(i)].score < bestScore) {
            bestPolylineIndex_ = i;
            bestScore = polylines_[static_cast<std::size_t>(i)].score;
            bestSize = i + MIN_SIZE;
        }
    }

    // There was no good match within the min/max size requirement
    if (bestSize < minSides_ || bestPolylineIndex_ < 0) {
        return false;
    }

    // make sure all the sides are within error tolerance
    const CandidatePolyline& best =
        polylines_[static_cast<std::size_t>(bestPolylineIndex_)];
    for (int32_t i = 0, j = bestSize - 1; i < bestSize; j = i, i++) {
        const cv::Point2i& a = contour[static_cast<std::size_t>(
            best.splits[static_cast<std::size_t>(i)])];
        const cv::Point2i& b = contour[static_cast<std::size_t>(
            best.splits[static_cast<std::size_t>(j)])];

        double dx = a.x - b.x, dy = a.y - b.y;
        double length = std::sqrt(dx * dx + dy * dy);
        double thresholdSideError = this->maxSideError_.compute(length);
        if (best.sideErrors[static_cast<std::size_t>(i)] >=
            thresholdSideError * thresholdSideError) {
            bestPolylineIndex_ = -1;
            return false;
        }
    }

    return true;
}

void PolylineSplitMerge::sequentialSideFit(const std::vector<cv::Point2i>& contour,
                                            bool loops) {
    // by finding more corners than necessary it can recover from mistakes previously.
    //
    // Java relies on int wrap-around: `INT_MAX + INT_MAX` overflows negative
    // and the next check clamps to `contour.size()`. C++ signed overflow is
    // UB, so widen to int64 first.
    int64_t limit64 = static_cast<int64_t>(maxSides_) +
                       static_cast<int64_t>(extraConsider_.computeI(maxSides_));
    int32_t limit;
    if (limit64 > std::numeric_limits<int32_t>::max() || limit64 <= 0) {
        limit = static_cast<int32_t>(contour.size());  // handle the situation where it overflows
    } else {
        limit = static_cast<int32_t>(limit64);
    }
    while (static_cast<int32_t>(list_.size()) < limit && !fatalError_) {
        if (!increaseNumberOfSidesByOne(contour, loops)) {
            break;
        }
    }
    // remove corners and recompute scores. If the result is better it will be saved
    while (!fatalError_) {
        CornerList::Iter c = selectCornerToRemove(contour, sideError_, loops);
        if (c != list_.end()) {
            removeCornerAndSavePolyline(c, sideError_.value);
        } else {
            break;
        }
    }
}

void PolylineSplitMerge::reset() {
    list_.reset();
    corners_.reset();
    polylines_.clear();
    bestPolylineIndex_ = -1;
    fatalError_ = false;
}

bool PolylineSplitMerge::savePolyline() {
    int32_t N = loops_ ? 3 : 2;

    // if a polyline of this size has already been saved then over write it
    CandidatePolyline* c;
    if (static_cast<int32_t>(list_.size()) <=
        static_cast<int32_t>(polylines_.size()) + N - 1) {
        c = &polylines_[static_cast<std::size_t>(
            static_cast<int32_t>(list_.size()) - N)];
        // sanity check
        if (static_cast<int32_t>(c->splits.size()) !=
            static_cast<int32_t>(list_.size()))
            throw std::runtime_error(
                "Egads saved polylines aren't in the expected order");
    } else {
        polylines_.emplace_back();
        c = &polylines_.back();
        c->reset();
        c->score = std::numeric_limits<double>::max();
    }

    double foundScore = computeScore(list_, cornerScorePenalty_, loops_);

    // only save the results if it's an improvement
    if (c->score > foundScore) {
        c->score = foundScore;
        c->splits.clear();
        c->sideErrors.clear();
        CornerList::Iter e = list_.getHead();
        double maxSideError = 0;
        while (e != list_.end()) {
            maxSideError = std::max(maxSideError, (*e)->sideError);
            c->splits.push_back((*e)->index);
            c->sideErrors.push_back((*e)->sideError);
            ++e;
        }
        c->maxSideError = maxSideError;
        return true;
    } else {
        return false;
    }
}

double PolylineSplitMerge::computeScore(CornerList& list, double cornerPenalty,
                                         bool loops) {
    double sumSides = 0;
    CornerList::Iter e = list.getHead();
    CornerList::Iter end = loops ? list.end() : list.getTail();
    while (e != end) {
        sumSides += (*e)->sideError;
        ++e;
    }

    int32_t numSides = loops ? static_cast<int32_t>(list.size())
                              : static_cast<int32_t>(list.size()) - 1;

    return sumSides / numSides + cornerPenalty * numSides;
}

bool PolylineSplitMerge::findInitialTriangle(const std::vector<cv::Point2i>& contour) {
    // find the first estimate for a corner
    int32_t cornerSeed = findCornerSeed(contour);

    // see if it can reject the contour immediately
    if (convex_) {
        if (!isConvexUsingMaxDistantPoints(contour, 0, cornerSeed))
            return false;
    }

    // Select the second corner.
    splitter_->selectSplitPoint(contour, 0, cornerSeed, resultsA_);
    splitter_->selectSplitPoint(contour, cornerSeed, 0, resultsB_);

    if (splitter_->compareScore(resultsA_.score, resultsB_.score) >= 0) {
        addCorner(resultsA_.index);
        addCorner(cornerSeed);
    } else {
        addCorner(cornerSeed);
        addCorner(resultsB_.index);
    }

    // Select the third corner. Initial triangle will be complete now
    // the third corner will be the one which maximizes the distance from the first two
    int32_t index0 = (*list_.getHead())->index;
    auto secondIt = list_.getHead();
    ++secondIt;
    int32_t index1 = (*secondIt)->index;
    int32_t index2 = maximumDistance(contour, index0, index1);
    addCorner(index2);

    // enforce CCW requirement
    ensureTriangleOrder(contour);

    return initializeScore(contour, true);
}

bool PolylineSplitMerge::initializeScore(const std::vector<cv::Point2i>& contour,
                                          bool loops) {
    // Score each side
    CornerList::Iter e = list_.getHead();
    CornerList::Iter end = loops ? list_.end() : list_.getTail();
    while (e != end) {
        if (convex_ && !isSideConvex(contour, e))
            return false;

        CornerList::Iter n = e;
        ++n;

        double error;
        if (n == list_.end()) {
            error = computeSideError(contour, (*e)->index, (*list_.getHead())->index);
        } else {
            error = computeSideError(contour, (*e)->index, (*n)->index);
        }
        (*e)->sideError = error;
        e = n;
    }

    // Compute what would happen if a side was split
    e = list_.getHead();
    while (e != end) {
        computePotentialSplitScore(contour, e,
                                   static_cast<int32_t>(list_.size()) < minSides_);
        ++e;
    }

    return true;
}

void PolylineSplitMerge::ensureTriangleOrder(const std::vector<cv::Point2i>& contour) {
    CornerList::Iter e = list_.getHead();
    Corner* a = *e;
    ++e;
    Corner* b = *e;
    ++e;
    Corner* c = *e;

    int32_t distB = circularDistanceP(a->index, b->index,
                                       static_cast<int32_t>(contour.size()));
    int32_t distC = circularDistanceP(a->index, c->index,
                                       static_cast<int32_t>(contour.size()));

    if (distB > distC) {
        list_.reset();
        list_.pushTail(a);
        list_.pushTail(c);
        list_.pushTail(b);
    }
}

PolylineSplitMerge::CornerList::Iter PolylineSplitMerge::addCorner(int32_t where) {
    Corner* c = corners_.grow();
    c->reset();
    c->index = where;
    list_.pushTail(c);
    return list_.getTail();
}

bool PolylineSplitMerge::increaseNumberOfSidesByOne(
    const std::vector<cv::Point2i>& contour, bool loops) {
    //		System.out.println("increase number of sides by one. list = "+list.size());
    CornerList::Iter selected = selectCornerToSplit(loops);

    // No side can be split
    if (selected == list_.end())
        return false;

    // Update the corner who's side was just split
    (*selected)->sideError = (*selected)->splitError0;
    // split the selected side and add a new corner
    Corner* c = corners_.grow();
    c->reset();
    c->index = (*selected)->splitLocation;
    c->sideError = (*selected)->splitError1;
    CornerList::Iter cornerE = list_.insertAfter(selected, c);

    // see if the new side could be convex
    if (convex_ && !isSideConvex(contour, selected))
        return false;
    else {
        // compute the score for sides which just changed
        computePotentialSplitScore(contour, cornerE,
                                   static_cast<int32_t>(list_.size()) < minSides_);
        computePotentialSplitScore(contour, selected,
                                   static_cast<int32_t>(list_.size()) < minSides_);

        // Save the results
        //		printCurrent(contour);
        savePolyline();

        return true;
    }
}

bool PolylineSplitMerge::isSideConvex(const std::vector<cv::Point2i>& contour,
                                      CornerList::Iter e1) {
    // a conservative estimate for concavity. Assumes a triangle and that the farthest
    // point is equal to the distance between the two corners

    CornerList::Iter e2 = next(e1);

    int32_t length = circularDistanceP((*e1)->index, (*e2)->index,
                                        static_cast<int32_t>(contour.size()));

    const cv::Point2i& p0 = contour[static_cast<std::size_t>((*e1)->index)];
    const cv::Point2i& p1 = contour[static_cast<std::size_t>((*e2)->index)];

    double dx = p0.x - p1.x, dy = p0.y - p1.y;
    double d = std::sqrt(dx * dx + dy * dy);

    return !(length >= d * convexTest_);
}

PolylineSplitMerge::CornerList::Iter PolylineSplitMerge::selectCornerToSplit(
    bool loops) {
    CornerList::Iter selected = list_.end();
    double bestChange = convex_ ? 0.0 : -std::numeric_limits<double>::max();

    // Pick the side that if split would improve the overall score the most
    CornerList::Iter e = list_.getHead();
    CornerList::Iter end = loops ? list_.end() : list_.getTail();

    while (e != end) {
        Corner* c = *e;
        if (!c->splitable) {
            ++e;
            continue;
        }

        // compute how much better the score will improve because of the split
        double change = c->sideError * 2 - c->splitError0 - c->splitError1;
        // it was found that selecting for the biggest change tends to produce better results
        if (change < 0) {
            change = -change;
        }
        if (change > bestChange) {
            bestChange = change;
            selected = e;
        }
        ++e;
    }

    return selected;
}

PolylineSplitMerge::CornerList::Iter PolylineSplitMerge::selectCornerToRemove(
    const std::vector<cv::Point2i>& contour, ErrorValue& sideError, bool loops) {
    if (list_.size() <= 3)
        return list_.end();

    // Pick the side that if split would improve the overall score the most
    CornerList::Iter target, end;

    // if it loops any corner can be split. If it doesn't look the end points can't be removed
    if (loops) {
        target = list_.getHead();
        end = list_.end();
    } else {
        target = list_.getHead();
        ++target;
        end = list_.getTail();
    }

    CornerList::Iter best = list_.end();
    double bestScore = -std::numeric_limits<double>::max();

    while (target != end) {
        CornerList::Iter p = previous(target);
        CornerList::Iter n = next(target);

        // just contributions of the corners in question
        double before = ((*p)->sideError + (*target)->sideError) / 2.0 +
                        cornerScorePenalty_;
        double after = computeSideError(contour, (*p)->index, (*n)->index);

        if (before - after > bestScore) {
            bestScore = before - after;
            best = target;
            sideError.value = after;
        }
        ++target;
    }

    // Mirrors `Objects.requireNonNull(best)` — Java throws NPE if no
    // candidate, which only happens when list.size() <= 3 (already
    // handled above).
    if (best == list_.end()) {
        throw std::runtime_error("selectCornerToRemove: no candidate found");
    }
    return best;
}

bool PolylineSplitMerge::removeCornerAndSavePolyline(
    CornerList::Iter corner, double sideErrorAfterRemoved) {
    //			System.out.println("removing a corner idx="+target.object.index);
    // Note: the corner is "lost" until the next contour is fit. Not worth the effort to recycle
    CornerList::Iter p = previous(corner);

    // go through the hassle of passing in this value instead of recomputing it
    // since recomputing it isn't trivial
    (*p)->sideError = sideErrorAfterRemoved;
    list_.remove(corner);
    // the line below is commented out because right now the current algorithm will
    // never grow after removing a corner. If this changes in the future uncomment it
    //			computePotentialSplitScore(contour,p);
    return savePolyline();
}

int32_t PolylineSplitMerge::findCornerSeed(const std::vector<cv::Point2i>& contour) {
    const cv::Point2i& a = contour[0];

    int32_t best = -1;
    double bestDistance = -std::numeric_limits<double>::max();

    for (std::size_t i = 1; i < contour.size(); i++) {
        const cv::Point2i& b = contour[i];

        double d = distanceSq(a, b);
        if (d > bestDistance) {
            bestDistance = d;
            best = static_cast<int32_t>(i);
        }
    }

    return best;
}

int32_t PolylineSplitMerge::maximumDistance(const std::vector<cv::Point2i>& contour,
                                             int32_t indexA, int32_t indexB) {
    const cv::Point2i& a = contour[static_cast<std::size_t>(indexA)];
    const cv::Point2i& b = contour[static_cast<std::size_t>(indexB)];

    int32_t best = -1;
    double bestDistance = -std::numeric_limits<double>::max();

    for (std::size_t i = 0; i < contour.size(); i++) {
        const cv::Point2i& c = contour[i];
        // can't sum sq distance because some skinny shapes it maximizes one and not the other
        //			double d = Math.sqrt(distanceSq(a,c)) + Math.sqrt(distanceSq(b,c));
        double d = distanceAbs(a, c) + distanceAbs(b, c);
        if (d > bestDistance) {
            bestDistance = d;
            best = static_cast<int32_t>(i);
        }
    }

    return best;
}

double PolylineSplitMerge::computeSideError(const std::vector<cv::Point2i>& contour,
                                             int32_t indexA, int32_t indexB) {
    assignLine(contour, indexA, indexB, line_);

    // don't sample the end points because the error will be zero by definition
    int32_t numSamples;
    double sumOfDistances = 0;
    int32_t length;
    if (indexB >= indexA) {
        length = indexB - indexA - 1;
        numSamples = std::min(length, maxNumberOfSideSamples_);
        for (int32_t i = 0; i < numSamples; i++) {
            int32_t index = indexA + 1 + length * i / numSamples;
            const cv::Point2i& p = contour[static_cast<std::size_t>(index)];
            sumOfDistances += lineSegmentDistanceSq(line_.ax, line_.ay,
                                                     line_.bx, line_.by,
                                                     p.x, p.y);
        }
        sumOfDistances /= numSamples;
    } else {
        length = static_cast<int32_t>(contour.size()) - indexA - 1 + indexB;
        numSamples = std::min(length, maxNumberOfSideSamples_);
        for (int32_t i = 0; i < numSamples; i++) {
            int32_t where = length * i / numSamples;
            int32_t index = (indexA + 1 + where) % static_cast<int32_t>(contour.size());
            const cv::Point2i& p = contour[static_cast<std::size_t>(index)];
            sumOfDistances += lineSegmentDistanceSq(line_.ax, line_.ay,
                                                     line_.bx, line_.by,
                                                     p.x, p.y);
        }
        sumOfDistances /= numSamples;
    }

    // handle divide by zero error
    if (numSamples > 0)
        return sumOfDistances;
    else
        return 0;
}

void PolylineSplitMerge::computePotentialSplitScore(
    const std::vector<cv::Point2i>& contour, CornerList::Iter e0, bool mustSplit) {
    CornerList::Iter e1 = next(e0);

    (*e0)->splitable = canBeSplit(contour, e0, mustSplit);

    if ((*e0)->splitable) {
        setSplitVariables(contour, e0, e1);
    }
}

void PolylineSplitMerge::setSplitVariables(const std::vector<cv::Point2i>& contour,
                                            CornerList::Iter e0,
                                            CornerList::Iter e1) {
    //		int distance0 = CircularIndex.distanceP(e0.object.index, e1.object.index, contour.size());

    int32_t index0 = circularPlusPOffset((*e0)->index, minimumSideLength_,
                                          static_cast<int32_t>(contour.size()));
    int32_t index1 = circularMinusPOffset((*e1)->index, minimumSideLength_,
                                           static_cast<int32_t>(contour.size()));

    splitter_->selectSplitPoint(contour, index0, index1, resultsA_);

    // if convex only perform the split if it would result in a convex polygon
    if (convex_) {
        const cv::Point2i& a = contour[static_cast<std::size_t>((*e0)->index)];
        const cv::Point2i& b = contour[static_cast<std::size_t>(resultsA_.index)];
        const cv::Point2i& c = contour[static_cast<std::size_t>((*next(e0))->index)];

        if (isPositiveZ(a, b, c)) {
            (*e0)->splitable = false;
            return;
        }
    }

    // see if this would result in a side that's too small
    int32_t dist0 = circularDistanceP((*e0)->index, resultsA_.index,
                                       static_cast<int32_t>(contour.size()));
    if (dist0 < minimumSideLength_ ||
        (static_cast<int32_t>(contour.size()) - dist0) < minimumSideLength_) {
        throw std::runtime_error("Should be impossible");
    }

    // this function is only called if splitable is set to true so no need to set it again
    (*e0)->splitLocation = resultsA_.index;
    (*e0)->splitError0 = computeSideError(contour, (*e0)->index, resultsA_.index);
    (*e0)->splitError1 = computeSideError(contour, resultsA_.index, (*e1)->index);

    if ((*e0)->splitLocation >= static_cast<int32_t>(contour.size()))
        throw std::runtime_error("Egads");
}

bool PolylineSplitMerge::canBeSplit(const std::vector<cv::Point2i>& contour,
                                     CornerList::Iter e0, bool mustSplit) {
    CornerList::Iter e1 = next(e0);

    // NOTE: The contour is passed in but only the size of the contour matters. This was done to prevent
    //       changing the signature if the algorithm was changed later on.
    int32_t length = circularDistanceP((*e0)->index, (*e1)->index,
                                        static_cast<int32_t>(contour.size()));

    // needs to be <= to prevent it from trying to split a side less than 1
    // times two because the two new sides would have to have a length of at least min
    if (length <= 2 * minimumSideLength_) {
        return false;
    }

    // threshold is greater than zero ti prevent it from saying it can split a perfect side
    return mustSplit || (*e0)->sideError > thresholdSideSplitScore_;
}

PolylineSplitMerge::CornerList::Iter PolylineSplitMerge::next(CornerList::Iter e) {
    auto n = e;
    ++n;
    if (n == list_.end()) {
        return list_.getHead();
    } else {
        return n;
    }
}

PolylineSplitMerge::CornerList::Iter PolylineSplitMerge::previous(CornerList::Iter e) {
    if (list_.isHead(e)) {
        return list_.getTail();
    } else {
        auto p = e;
        --p;
        return p;
    }
}

bool PolylineSplitMerge::isConvexUsingMaxDistantPoints(
    const std::vector<cv::Point2i>& contour, int32_t indexA, int32_t indexB) {
    double d = std::sqrt(distanceSq(contour[static_cast<std::size_t>(indexA)],
                                     contour[static_cast<std::size_t>(indexB)]));

    // conservative upper bounds would be 1/2 a circle, including interior side.
    int32_t maxAllowed = static_cast<int32_t>((M_PI + 1) * d + 0.5);

    int32_t length0 = circularDistanceP(indexA, indexB,
                                         static_cast<int32_t>(contour.size()));
    int32_t length1 = circularDistanceP(indexB, indexA,
                                         static_cast<int32_t>(contour.size()));

    return length0 <= maxAllowed && length1 <= maxAllowed;
}

double PolylineSplitMerge::distanceSq(const cv::Point2i& a, const cv::Point2i& b) {
    double dx = b.x - a.x;
    double dy = b.y - a.y;

    return dx * dx + dy * dy;
}

double PolylineSplitMerge::distanceAbs(const cv::Point2i& a, const cv::Point2i& b) {
    double dx = b.x - a.x;
    double dy = b.y - a.y;

    return std::fabs(dx) + std::fabs(dy);
}

void PolylineSplitMerge::assignLine(const std::vector<cv::Point2i>& contour,
                                     int32_t indexA, int32_t indexB,
                                     LineParametric2D& line) {
    const cv::Point2i& endA = contour[static_cast<std::size_t>(indexA)];
    const cv::Point2i& endB = contour[static_cast<std::size_t>(indexB)];

    line.px = endA.x;
    line.py = endA.y;
    line.sx = endB.x - endA.x;
    line.sy = endB.y - endA.y;
}

void PolylineSplitMerge::assignLine(const std::vector<cv::Point2i>& contour,
                                     int32_t indexA, int32_t indexB,
                                     LineSegment2D& line) {
    const cv::Point2i& endA = contour[static_cast<std::size_t>(indexA)];
    const cv::Point2i& endB = contour[static_cast<std::size_t>(indexB)];

    line.ax = endA.x;
    line.ay = endA.y;
    line.bx = endB.x;
    line.by = endB.y;
}

void PolylineSplitMerge::setMinimumSideLength(int32_t minimumSideLength) {
    if (minimumSideLength <= 0)
        throw std::invalid_argument("Minimum length must be at least 1");
    this->minimumSideLength_ = minimumSideLength;
}

}  // namespace boofcv_qr
