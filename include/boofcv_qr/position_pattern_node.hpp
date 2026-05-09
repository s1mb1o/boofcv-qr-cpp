// Port of boofcv.alg.fiducial.qrcode.PositionPatternNode (BoofCV v1.3.0).
// Step 7a.

#ifndef BOOFCV_QR_POSITION_PATTERN_NODE_HPP
#define BOOFCV_QR_POSITION_PATTERN_NODE_HPP

#include "boofcv_qr/squares/square_node.hpp"

namespace boofcv_qr {

// Information for position detection patterns. These are squares.
// One outer shape that is 1 block thick, inner white space 1 block
// thick, then the stone which is 3 blocks thick. Total of 7 blocks.
//
// Corners in squares must be in CCW order.
class PositionPatternNode : public SquareNode {
public:
    // threshold for binary classification.
    double grayThreshold = -1.0;

    void reset() {
        SquareNode::reset();
        grayThreshold = -1.0;
    }
};

}  // namespace boofcv_qr

#endif  // BOOFCV_QR_POSITION_PATTERN_NODE_HPP
