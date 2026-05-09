// Port of boofcv.alg.fiducial.calib.squares.SquareNode (BoofCV v1.3.0).
//
// Step 7a of the porting plan.
//
// Per CLAUDE.md type mappings: Polygon2D_F64 -> std::vector<cv::Point2d>,
// DogArray_B -> std::vector<uint8_t>, Point2D_F64 -> cv::Point2d.

#ifndef BOOFCV_QR_SQUARES_SQUARE_NODE_HPP
#define BOOFCV_QR_SQUARES_SQUARE_NODE_HPP

#include <opencv2/core.hpp>

#include <array>
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
    // length of sides. side = i and i+1
    std::array<double, 4> sideLengths{};
    // the largest length
    double largestSide = 0.0;
    double smallestSide = 0.0;

    // marker used to indicate that this has been traversed by different algorithms
    int32_t graph = RESET_GRAPH;

    // edges in the graph. One for each side in the shape
    std::array<SquareEdge*, 4> edges{nullptr, nullptr, nullptr, nullptr};

    // Finds the Euclidean distance squared of the closest corner to point p.
    double distanceSqCorner(const cv::Point2d& p) const;

    // Discards previous information.
    void reset();

    // Computes the number of edges attached to this node.
    int32_t getNumberOfConnections() const;

    double smallestSideLength() const;

    // Returns the edge that connects this node to target, or nullptr.
    SquareEdge* findEdge(const SquareNode* target) const;

    int32_t findEdgeIndex(const SquareNode* target) const;
};

}  // namespace boofcv_qr

#endif  // BOOFCV_QR_SQUARES_SQUARE_NODE_HPP
