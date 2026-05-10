// Port of boofcv.alg.filter.binary.ContourPacked (BoofCV v1.3.0).
//
// Header struct for one blob's contours. Points live in the shared
// `PackedSetsPoint2D_I32`; this struct just records the per-blob set
// indices and the blob's label id.

#ifndef BOOFCV_QR_BINARY_CONTOUR_PACKED_HPP
#define BOOFCV_QR_BINARY_CONTOUR_PACKED_HPP

#include <cstdint>
#include <vector>

namespace boofcv_qr {

struct ContourPacked {
    // ID of blob in the labeled image. Pixels belonging to this blob
    // in the labeled image have this value.
    int32_t id = -1;

    // Index in the packed set of the external contour.
    int32_t externalIndex = -1;

    // Index for each internal contour (one per hole).
    std::vector<int32_t> internalIndexes;

    void reset() {
        id = -1;
        externalIndex = -1;
        internalIndexes.clear();
    }
};

}  // namespace boofcv_qr

#endif  // BOOFCV_QR_BINARY_CONTOUR_PACKED_HPP
