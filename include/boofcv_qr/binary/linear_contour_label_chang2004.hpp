// Port of boofcv.alg.filter.binary.LinearContourLabelChang2004
// (BoofCV v1.3.0).
//
// Single-pass linear-time component labeller with contour tracing,
// Fu Chang & Chun-jen Chen & Chi-jen Lu, "A linear-time
// component-labeling algorithm using contour tracing technique",
// Computer Vision and Image Understanding, 2004.
//
// Output:
//   - `labeled` (CV_32SC1, same size as input): blob ID per pixel
//     (1..N). Background pixels are 0.
//   - `getContours()`: per-blob `ContourPacked` headers indexing into
//     `getPackedPoints()` for actual point coordinates (external +
//     zero-or-more internal hole contours).
//
// Verbatim port — see CLAUDE.md "Verbatim vs idiomize". The reason
// this exists is per-pixel parity with BoofCV; cv::findContours
// differs in inner-loop pixel ordering around diagonal moves and
// silently degrades two downstream stages (`ContourEdgeIntensity` and
// `PolylineSplitMerge`) by ~0.4pp aggregate on `qrcodes_v3`. See
// `src/binary/linear_contour_label_chang2004.md` for the algorithm
// description and the closing of ADR 01.

#ifndef BOOFCV_QR_BINARY_LINEAR_CONTOUR_LABEL_CHANG2004_HPP
#define BOOFCV_QR_BINARY_LINEAR_CONTOUR_LABEL_CHANG2004_HPP

#include "boofcv_qr/binary/connect_rule.hpp"
#include "boofcv_qr/binary/contour_packed.hpp"
#include "boofcv_qr/binary/contour_tracer.hpp"
#include "boofcv_qr/binary/packed_sets_point2d_i32.hpp"
#include "boofcv_qr/polyline/polyline_split_merge.hpp"  // for ConfigLength

#include <opencv2/core.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace boofcv_qr {

class LinearContourLabelChang2004 {
public:
    explicit LinearContourLabelChang2004(ConnectRule rule);

    // Processes the binary image to find the contour of and label
    // blobs.
    //
    // `binary`  (CV_8UC1, 0/1 convention per CLAUDE.md "Binary image
    //            convention"): not modified externally; internally we
    //            copy into a 1-pixel zero-bordered buffer.
    // `labeled` (CV_32SC1): output. Resized to match `binary`. Each
    //            pixel gets the blob id (1..N) of the blob it belongs
    //            to; 0 for background.
    void process(const cv::Mat& binary, cv::Mat& labeled);

    // Mirrors Java getters/setters (`@Getter @Setter ConfigLength
    // maxContourLength`, etc.).
    const ConfigLength& getMaxContourLength() const { return maxContourLength; }
    void setMaxContourLength(const ConfigLength& v) { maxContourLength.setTo(v); }

    const ConfigLength& getMinContourLength() const { return minContourLength; }
    void setMinContourLength(const ConfigLength& v) { minContourLength.setTo(v); }

    bool isSaveInternalContours() const { return saveInternalContours; }
    void setSaveInternalContours(bool v) { saveInternalContours = v; }

    const std::vector<ContourPacked>& getContours() const { return contours; }

    // Direct access — `TestLinearContourLabelChang2004` reaches in to
    // verify per-set sizes.
    PackedSetsPoint2D_I32& getPackedPoints() { return packedPoints; }
    const PackedSetsPoint2D_I32& getPackedPoints() const { return packedPoints; }

    void setConnectRule(ConnectRule rule);
    ConnectRule getConnectRule() const { return tracer->getConnectRule(); }

    void releaseScratch();

private:
    void handleStep1();
    void handleStep2(cv::Mat& labeled, int32_t label);

    // scanForOne — find the next non-mutated `1` pixel in a row span.
    // Java does pointer arithmetic on a `byte[]`; here we walk a
    // `uint8_t*` from `data + index` until `data + end`.
    static int32_t scanForOne(const uint8_t* data, int32_t index, int32_t end);

    // Maximum number of pixels in an external contour. If exceeded,
    // the external contour is discarded.
    ConfigLength maxContourLength = ConfigLength::fixed(-1);
    // External contours less than this are discarded.
    ConfigLength minContourLength = ConfigLength::fixed(0);
    // If false, internal contours are not saved as they are found.
    bool saveInternalContours = true;

    // Actual contour length constraints in image pixels (resolved
    // per call from the ConfigLengths above).
    int32_t minContourLengthPixels = 0;
    int32_t maxContourLengthPixels = 0;

    // Traces edge pixels.
    std::unique_ptr<ContourTracer> tracer;

    // 1-pixel-bordered copy of the input. Resized lazily.
    cv::Mat border;

    // Predeclared / recycled point storage.
    PackedSetsPoint2D_I32 packedPoints{2000};
    // Per-blob contour headers.
    std::vector<ContourPacked> contours;

    // Internal bookkeeping variables (mirror Java's per-call fields).
    int32_t x = 0, y = 0, indexIn = 0, indexOut = 0;
};

}  // namespace boofcv_qr

#endif  // BOOFCV_QR_BINARY_LINEAR_CONTOUR_LABEL_CHANG2004_HPP
