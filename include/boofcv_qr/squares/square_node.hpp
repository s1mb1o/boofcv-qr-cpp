// Port of boofcv.alg.fiducial.calib.squares.SquareNode (BoofCV v1.3.0).
//
// Step 7a of the porting plan.
//
// Per CLAUDE.md type mappings: Polygon2D_F64 -> std::vector<cv::Point2d>,
// DogArray_B -> std::vector<uint8_t>, Point2D_F64 -> cv::Point2d.

#ifndef BOOFCV_QR_SQUARES_SQUARE_NODE_HPP
#define BOOFCV_QR_SQUARES_SQUARE_NODE_HPP

#include <opencv2/core.hpp>

#include <cstdint>
#include <vector>

namespace boofcv_qr {

class SquareEdge;  // fwd

class SquareNode {
public:
    static constexpr int32_t RESET_GRAPH = -2;

    // polygon which this node represents.
    // cw or ccw ordering of edges doesn't matter
    std::vector<cv::Point2d> square;
    // does a corner touch the border?
    std::vector<std::uint8_t> touch;

    // intersection of line 0 and 2 with 1 and 3.
    cv::Point2d center{-1.0, -1.0};
    // length of sides. side = i and i+1. Resized by `updateArrayLength`
    // to match `square.size()`. Default 4 entries for QR's 4-sided
    // polygons (set by reset()).
    std::vector<double> sideLengths{0.0, 0.0, 0.0, 0.0};
    // the largest length
    double largestSide = 0.0;
    double smallestSide = 0.0;

    // marker used to indicate that this has been traversed by different algorithms
    int32_t graph = RESET_GRAPH;

    // edges in the graph. One for each side in the shape. Resized by
    // `updateArrayLength` to match `square.size()`.
    std::vector<SquareEdge*> edges{nullptr, nullptr, nullptr, nullptr};

    // Finds the Euclidean distance squared of the closest corner to point p.
    double distanceSqCorner(const cv::Point2d& p) const;

    // Discards previous information.
    void reset();

    // Resize edges/sideLengths to match square.size(). BoofCV's
    // SquareNode.updateArrayLength does the same. QR always uses
    // 4-sided polygons but we keep the API for parity.
    void updateArrayLength();

    // Computes the number of edges attached to this node.
    int32_t getNumberOfConnections() const;

    double smallestSideLength() const;

    // Returns the edge that connects this node to target, or nullptr.
    SquareEdge* findEdge(const SquareNode* target) const;

    int32_t findEdgeIndex(const SquareNode* target) const;
};

// Mirror of SquareNode.KdTreeSquareNode — distance functor for
// ddogleg's KD-tree. The QR finder-pattern graph generator
// (step 7c) uses a nearest-neighbour search; this provides the
// same squared-centre distance + 2-D coordinate access contract.
class KdTreeSquareNode {
public:
    // Squared centre-to-centre distance.
    static double distance(const SquareNode* a, const SquareNode* b);

    // valueAt(node, 0) → centre.x, valueAt(node, 1) → centre.y.
    static double valueAt(const SquareNode* node, int32_t index);

    // KD-tree dimensionality.
    static constexpr int32_t length() { return 2; }
};

}  // namespace boofcv_qr

#endif  // BOOFCV_QR_SQUARES_SQUARE_NODE_HPP
