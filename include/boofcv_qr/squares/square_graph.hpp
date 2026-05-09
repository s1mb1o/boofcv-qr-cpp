// Port of boofcv.alg.fiducial.calib.squares.SquareGraph (BoofCV v1.3.0).
// Step 7a.

#ifndef BOOFCV_QR_SQUARES_SQUARE_GRAPH_HPP
#define BOOFCV_QR_SQUARES_SQUARE_GRAPH_HPP

#include "boofcv_qr/squares/square_edge.hpp"
#include "boofcv_qr/squares/square_node.hpp"

#include <opencv2/core.hpp>

#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

namespace boofcv_qr {

// 2D line segment used by SquareGraph::findSideIntersect.
struct LineSegment2D {
    cv::Point2d a;
    cv::Point2d b;
};

class SquareGraph {
public:
    // All edges which have been declared. We own them via unique_ptr so
    // SquareNode::edges can be raw observer pointers.
    std::vector<std::unique_ptr<SquareEdge>> declaredEdges;
    // Edges that were retired and ready to be reused.
    std::deque<SquareEdge*> unused;

    // BoofCV's `parallelThreshold = UtilAngle.radian(45)` = 45 degrees.
    double parallelThreshold = 45.0 * 3.14159265358979323846 / 180.0;

    void reset();

    // Compute n.center (intersection of opposing-corner lines), n.sideLengths,
    // n.largestSide, n.smallestSide. Throws if the polygon is degenerate.
    static void computeNodeInfo(SquareNode& n);

    // Removes the edge from the two nodes and recycles the data structure.
    void detachEdge(SquareEdge* edge);

    // Finds the side which intersects the line on the shape. Returns the
    // side index, or -1 if no intersection (which BoofCV considers a bug
    // but doesn't throw).
    int32_t findSideIntersect(const SquareNode& n,
                              const LineSegment2D& line,
                              cv::Point2d& intersection,
                              LineSegment2D& storage) const;

    // Connect a/b on the named sides if neither already has a "better"
    // (shorter-distance) edge there. Returns true on connection.
    bool checkConnect(SquareNode* a, int32_t indexA, SquareNode* b,
                      int32_t indexB, double distance);

    // Acute angle between sides a/sideA and b/sideB, mapped to [0, pi/2].
    double acuteAngle(const SquareNode& a, int32_t sideA,
                      const SquareNode& b, int32_t sideB) const;

    // True if the two sides are within `parallelThreshold` of parallel.
    bool almostParallel(const SquareNode& a, int32_t sideA,
                        const SquareNode& b, int32_t sideB) const;

    double getParallelThreshold() const { return parallelThreshold; }
    void setParallelThreshold(double v) { parallelThreshold = v; }

    // Public for parity testing — Java tests poke at this directly
    // even though it's package-private in BoofCV. Higher-level callers
    // should prefer checkConnect() which has the "better-edge wins"
    // semantics.
    void connect(SquareNode* a, int32_t indexA, SquareNode* b, int32_t indexB,
                 double distance);

private:
    SquareEdge* getUnusedEdge();
};

}  // namespace boofcv_qr

#endif  // BOOFCV_QR_SQUARES_SQUARE_GRAPH_HPP
