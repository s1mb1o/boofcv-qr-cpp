// Port of boofcv.struct.ConnectRule (BoofCV v1.3.0).
//
// Tiny enum used by LinearContourLabelChang2004 / ContourTracer to
// select between 4-connectivity and 8-connectivity neighbourhoods.
// Plumbing — no algorithmic content.

#ifndef BOOFCV_QR_BINARY_CONNECT_RULE_HPP
#define BOOFCV_QR_BINARY_CONNECT_RULE_HPP

namespace boofcv_qr {

enum class ConnectRule {
    // Four-connected neighbourhood: (1,0) (0,1) (-1,0) (0,-1).
    FOUR,
    // Eight-connected neighbourhood: includes the four diagonals.
    EIGHT,
};

}  // namespace boofcv_qr

#endif  // BOOFCV_QR_BINARY_CONNECT_RULE_HPP
