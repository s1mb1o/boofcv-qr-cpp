// Port of boofcv.alg.filter.binary.ContourTracer (BoofCV v1.3.0).
//
// Used by LinearContourLabelChang2004 to trace external and internal
// contours around dark blobs in a binary image. The tracer mutates the
// binary image (it writes a `0xFF` sentinel — the bit pattern of Java's
// signed `-1` byte — into searched pixels so subsequent searches skip
// them) and the labeled image (writes the blob's label into traced
// pixels).
//
// The input binary image is assumed to have a 1-pixel zero border so
// the tracer never has to bounds-check. The labeled image has the
// same dimensions as the *original* binary image (without the border)
// — `add()` writes via `(x-1, y-1)` to compensate.
//
// Per CLAUDE.md type mappings:
//   GrayU8 binary  -> cv::Mat CV_8UC1
//   GrayS32 labeled -> cv::Mat CV_32SC1
//   byte[] data     -> uint8_t* row pointer base (binary.data)
//   int[] data      -> int32_t* row pointer base (labeled.data)

#ifndef BOOFCV_QR_BINARY_CONTOUR_TRACER_HPP
#define BOOFCV_QR_BINARY_CONTOUR_TRACER_HPP

#include "boofcv_qr/binary/connect_rule.hpp"
#include "boofcv_qr/binary/packed_sets_point2d_i32.hpp"

#include <opencv2/core.hpp>

#include <array>
#include <cstdint>
#include <limits>

namespace boofcv_qr {

class ContourTracer {
public:
    explicit ContourTracer(ConnectRule rule);

    // Specifies the input images. `binary` must have a 1-pixel zero
    // border. `labeled` size matches the original (pre-border) image.
    void setInputs(cv::Mat& binary, cv::Mat& labeled,
                   PackedSetsPoint2D_I32& storagePoints);

    // Traces the contour starting at the specified seed pixel.
    //
    // `external` == true for an external (outside) contour, false for
    // an internal hole.
    void trace(int32_t label, int32_t initialX, int32_t initialY,
               bool external);

    void setMaxContourSize(int32_t v) { maxContourSize = v; }

    ConnectRule getConnectRule() const { return rule; }

private:
    bool searchOne();
    bool searchOne4();
    bool searchOne8();
    bool checkOne(int32_t index);
    void moveToNext();
    void add(int32_t x, int32_t y);

    // Stops saving the contour when it meets or exceeds this value.
    int32_t maxContourSize = std::numeric_limits<int32_t>::max();

    const ConnectRule rule;
    const int32_t ruleN;

    // Storage for contour points (not owned).
    PackedSetsPoint2D_I32* storagePoints = nullptr;

    // Binary image being traced + mutated. Not owned.
    cv::Mat* binary = nullptr;
    // Label image being marked. Not owned.
    cv::Mat* labeled = nullptr;

    // Cached strides + start-of-data pointers. binary.startIndex is 0
    // for a non-subimage cv::Mat — same convention as `mat.ptr()`.
    uint8_t* binaryData = nullptr;
    int32_t binaryStride = 0;
    int32_t binaryStartIndex = 0;  // always 0 for our use
    int32_t* labeledData = nullptr;
    int32_t labeledStride = 0;
    int32_t labeledStartIndex = 0;  // always 0 for our use

    // Coordinate of the pixel currently being examined (x,y).
    int32_t x = 0, y = 0;
    // Label of the object being traced.
    int32_t label = 0;
    // Direction it moved in (index into offsetsBinary).
    int32_t dir = 0;
    // Linear index of the current pixel in the binary buffer.
    int32_t indexBinary = 0;
    // Linear index of the current pixel in the labeled buffer.
    int32_t indexLabel = 0;

    // Pixel-index offsets to each neighbour, both for binary and
    // labeled images. Stride differs (border-padded vs not) so the two
    // tables differ.
    std::array<int32_t, 8> offsetsBinary{};
    std::array<int32_t, 8> offsetsLabeled{};
    // Direction-of-search lookup: given the direction we travelled
    // into the current pixel, which direction should the next clockwise
    // search start from. Size = ruleN.
    std::array<int32_t, 8> nextDirection{};
};

}  // namespace boofcv_qr

#endif  // BOOFCV_QR_BINARY_CONTOUR_TRACER_HPP
