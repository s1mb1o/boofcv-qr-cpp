// Port of SquareGraph. Verbatim per CLAUDE.md.
//
// BoofCV uses georegression for the line-intersection / vector-acute /
// circular-index helpers. Those are small enough to inline here.

#include "boofcv_qr/squares/square_graph.hpp"

#include <cmath>
#include <stdexcept>

namespace boofcv_qr {

namespace {

// Mirror of `Intersection2D_F64.intersection(Point a0, Point a1, Point b0,
// Point b1, Point out)` for line-line intersection. Returns true on
// success (lines not parallel), writes intersection into `out`.
//
// BoofCV's geometric form: solve for t such that
//   a0 + t*(a1 - a0)  ==  b0 + s*(b1 - b0)
// using the determinant of the 2x2 direction matrix.
bool lineLineIntersection(const cv::Point2d& a0, const cv::Point2d& a1,
                          const cv::Point2d& b0, const cv::Point2d& b1,
                          cv::Point2d& out) {
    double dxA = a1.x - a0.x;
    double dyA = a1.y - a0.y;
    double dxB = b1.x - b0.x;
    double dyB = b1.y - b0.y;

    double denom = dxA * dyB - dyA * dxB;
    if (std::fabs(denom) < 1e-12) return false;  // parallel / coincident

    double t = ((b0.x - a0.x) * dyB - (b0.y - a0.y) * dxB) / denom;
    out.x = a0.x + t * dxA;
    out.y = a0.y + t * dyA;
    return true;
}

// Mirror of `Intersection2D_F64.intersection(LineSegment2D, LineSegment2D,
// Point)`. SEGMENT-SEGMENT intersection — only succeeds if the
// intersection point is inside BOTH segments.
bool segmentSegmentIntersection(const LineSegment2D& s1,
                                const LineSegment2D& s2,
                                cv::Point2d& out) {
    double x1 = s1.a.x, y1 = s1.a.y;
    double x2 = s1.b.x, y2 = s1.b.y;
    double x3 = s2.a.x, y3 = s2.a.y;
    double x4 = s2.b.x, y4 = s2.b.y;

    double denom = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4);
    if (std::fabs(denom) < 1e-12) return false;

    double t = ((x1 - x3) * (y3 - y4) - (y1 - y3) * (x3 - x4)) / denom;
    double u = -((x1 - x2) * (y1 - y3) - (y1 - y2) * (x1 - x3)) / denom;
    if (t < 0.0 || t > 1.0 || u < 0.0 || u > 1.0) return false;

    out.x = x1 + t * (x2 - x1);
    out.y = y1 + t * (y2 - y1);
    return true;
}

// Mirror of `UtilAngle.dist(double, double)` — circular distance on the
// real line modulo 2*pi, i.e. the minimum |a - b + k*2*pi|.
double angleDist(double a, double b) {
    double d = std::fabs(a - b);
    while (d > M_PI) d -= 2.0 * M_PI;
    return std::fabs(d);
}

// Mirror of `Vector2D_F64.acute(Vector2D_F64)` — the unsigned angle
// between two vectors in [0, pi].
double vectorAcute(double ax, double ay, double bx, double by) {
    double dot = ax * bx + ay * by;
    double na = std::sqrt(ax * ax + ay * ay);
    double nb = std::sqrt(bx * bx + by * by);
    if (na == 0.0 || nb == 0.0) return 0.0;
    double c = dot / (na * nb);
    if (c > 1.0) c = 1.0;
    if (c < -1.0) c = -1.0;
    return std::acos(c);
}

// Mirror of `CircularIndex.addOffset(int index, int value, int size)`.
int32_t circularIndexAdd(int32_t index, int32_t value, int32_t size) {
    int32_t r = (index + value) % size;
    if (r < 0) r += size;
    return r;
}

double pointDistance(const cv::Point2d& a, const cv::Point2d& b) {
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

}  // namespace

void SquareGraph::reset() {
    for (auto& e : declaredEdges) e->reset();
    unused.clear();
    for (auto& e : declaredEdges) unused.push_back(e.get());
}

void SquareGraph::computeNodeInfo(SquareNode& n) {
    // Under perspective distortion the geometric center is the
    // intersection of the lines formed by opposing corners.
    if (!lineLineIntersection(n.square[0], n.square[2], n.square[1],
                              n.square[3], n.center)) {
        // This should be impossible for a non-degenerate quad.
        throw std::runtime_error("BAD");
    }

    // side lengths
    n.largestSide = 0.0;
    n.smallestSide = std::numeric_limits<double>::max();
    for (int32_t j = 0, i = 3; j < 4; i = j, j++) {
        double l = pointDistance(n.square[static_cast<std::size_t>(j)],
                                 n.square[static_cast<std::size_t>(i)]);
        n.sideLengths[static_cast<std::size_t>(i)] = l;
        n.largestSide = std::max(n.largestSide, l);
        n.smallestSide = std::min(n.smallestSide, l);
    }
}

void SquareGraph::detachEdge(SquareEdge* edge) {
    edge->a->edges[static_cast<std::size_t>(edge->sideA)] = nullptr;
    edge->b->edges[static_cast<std::size_t>(edge->sideB)] = nullptr;
    edge->reset();
    unused.push_back(edge);
}

int32_t SquareGraph::findSideIntersect(const SquareNode& n,
                                       const LineSegment2D& line,
                                       cv::Point2d& intersection,
                                       LineSegment2D& storage) const {
    for (int32_t j = 0, i = 3; j < 4; i = j, j++) {
        storage.a = n.square[static_cast<std::size_t>(i)];
        storage.b = n.square[static_cast<std::size_t>(j)];

        if (segmentSegmentIntersection(line, storage, intersection)) {
            return i;
        }
    }
    return -1;
}

bool SquareGraph::checkConnect(SquareNode* a, int32_t indexA, SquareNode* b,
                               int32_t indexB, double distance) {
    SquareEdge* eA = a->edges[static_cast<std::size_t>(indexA)];
    if (eA != nullptr && eA->distance > distance) {
        detachEdge(eA);
    }
    SquareEdge* eB = b->edges[static_cast<std::size_t>(indexB)];
    if (eB != nullptr && eB->distance > distance) {
        detachEdge(eB);
    }
    if (a->edges[static_cast<std::size_t>(indexA)] == nullptr &&
        b->edges[static_cast<std::size_t>(indexB)] == nullptr) {
        connect(a, indexA, b, indexB, distance);
        return true;
    }
    return false;
}

void SquareGraph::connect(SquareNode* a, int32_t indexA, SquareNode* b,
                          int32_t indexB, double distance) {
    SquareEdge* edge = getUnusedEdge();
    edge->a = a;
    edge->sideA = indexA;
    edge->b = b;
    edge->sideB = indexB;
    edge->distance = distance;
    a->edges[static_cast<std::size_t>(indexA)] = edge;
    b->edges[static_cast<std::size_t>(indexB)] = edge;
}

bool SquareGraph::almostParallel(const SquareNode& a, int32_t sideA,
                                 const SquareNode& b, int32_t sideB) const {
    double selected = acuteAngle(a, sideA, b, sideB);
    return selected <= parallelThreshold;
}

double SquareGraph::acuteAngle(const SquareNode& a, int32_t sideA,
                               const SquareNode& b, int32_t sideB) const {
    const cv::Point2d& a0 = a.square[static_cast<std::size_t>(sideA)];
    const cv::Point2d& a1 =
        a.square[static_cast<std::size_t>(circularIndexAdd(sideA, 1, 4))];
    const cv::Point2d& b0 = b.square[static_cast<std::size_t>(sideB)];
    const cv::Point2d& b1 =
        b.square[static_cast<std::size_t>(circularIndexAdd(sideB, 1, 4))];

    double acute = vectorAcute(a1.x - a0.x, a1.y - a0.y, b1.x - b0.x,
                                b1.y - b0.y);
    return std::min(angleDist(M_PI, acute), acute);
}

SquareEdge* SquareGraph::getUnusedEdge() {
    if (!unused.empty()) {
        SquareEdge* e = unused.front();
        unused.pop_front();
        return e;
    }
    declaredEdges.push_back(std::make_unique<SquareEdge>());
    return declaredEdges.back().get();
}

}  // namespace boofcv_qr
