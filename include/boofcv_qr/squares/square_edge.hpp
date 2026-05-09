// Port of boofcv.alg.fiducial.calib.squares.SquareEdge (BoofCV v1.3.0).
// Step 7a.

#ifndef BOOFCV_QR_SQUARES_SQUARE_EDGE_HPP
#define BOOFCV_QR_SQUARES_SQUARE_EDGE_HPP

#include "boofcv_qr/squares/square_node.hpp"

#include <cstdint>
#include <stdexcept>

namespace boofcv_qr {

// Edge in the graph which connects square shapes.
class SquareEdge {
public:
    // destinations
    SquareNode* a = nullptr;
    SquareNode* b = nullptr;

    // which index in the shape this edge belongs to
    int32_t sideA = -1;
    int32_t sideB = -1;

    // the distance between the square's centers
    double distance = -1.0;

    SquareEdge() = default;
    SquareEdge(SquareNode* a_, SquareNode* b_, int32_t sideA_, int32_t sideB_)
        : a(a_), b(b_), sideA(sideA_), sideB(sideB_) {}

    // Returns the destination node opposite from src.
    SquareNode* destination(const SquareNode* src) const {
        if (a == src) return b;
        if (b == src) return a;
        throw std::invalid_argument("BUG! src is not a or b");
    }

    int32_t destinationSide(const SquareNode* src) const {
        if (a == src) return sideB;
        if (b == src) return sideA;
        throw std::invalid_argument("BUG! src is not a or b");
    }

    bool isEndPoint(const SquareNode* target) const {
        return a == target || b == target;
    }

    void reset() {
        a = b = nullptr;
        sideA = sideB = -1;
        distance = -1.0;
    }
};

}  // namespace boofcv_qr

#endif  // BOOFCV_QR_SQUARES_SQUARE_EDGE_HPP
