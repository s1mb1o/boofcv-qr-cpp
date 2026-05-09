// Port of SquareNode. Verbatim per CLAUDE.md.

#include "boofcv_qr/squares/square_node.hpp"
#include "boofcv_qr/squares/square_edge.hpp"

#include <limits>

namespace boofcv_qr {

namespace {
double distance2(const cv::Point2d& a, const cv::Point2d& b) {
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    return dx * dx + dy * dy;
}
}  // namespace

double SquareNode::distanceSqCorner(const cv::Point2d& p) const {
    double best = std::numeric_limits<double>::max();
    for (int32_t i = 0; i < 4; i++) {
        double d = distance2(square[static_cast<std::size_t>(i)], p);
        if (d < best) {
            best = d;
        }
    }
    return best;
}

void SquareNode::reset() {
    square.clear();
    touch.clear();
    center = cv::Point2d(-1.0, -1.0);
    largestSide = 0.0;
    smallestSide = std::numeric_limits<double>::max();
    graph = RESET_GRAPH;
    for (std::size_t i = 0; i < edges.size(); i++) {
        edges[i] = nullptr;
        sideLengths[i] = 0.0;
    }
}

int32_t SquareNode::getNumberOfConnections() const {
    int32_t ret = 0;
    int32_t n = static_cast<int32_t>(square.size());
    for (int32_t i = 0; i < n; i++) {
        if (edges[static_cast<std::size_t>(i)] != nullptr)
            ret++;
    }
    return ret;
}

double SquareNode::smallestSideLength() const {
    double smallest = std::numeric_limits<double>::max();
    int32_t n = static_cast<int32_t>(square.size());
    for (int32_t i = 0; i < n; i++) {
        double length = sideLengths[static_cast<std::size_t>(i)];
        if (length < smallest) {
            smallest = length;
        }
    }
    return smallest;
}

SquareEdge* SquareNode::findEdge(const SquareNode* target) const {
    int32_t index = findEdgeIndex(target);
    if (index >= 0)
        return edges[static_cast<std::size_t>(index)];
    return nullptr;
}

int32_t SquareNode::findEdgeIndex(const SquareNode* target) const {
    for (int32_t i = 0; i < 4; i++) {
        SquareEdge* e = edges[static_cast<std::size_t>(i)];
        if (e != nullptr && e->isEndPoint(target)) {
            return i;
        }
    }
    return -1;
}

}  // namespace boofcv_qr
