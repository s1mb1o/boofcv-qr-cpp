// Port of boofcv.alg.fiducial.qrcode.QrCodeAlignmentPatternLocator
// (BoofCV v1.3.0). Step 8.
//
// Per CLAUDE.md type mappings: ImageGray<T> -> cv::Mat CV_8UC1
// (template <T> dropped).

#ifndef BOOFCV_QR_ALIGNMENT_QR_CODE_ALIGNMENT_PATTERN_LOCATOR_HPP
#define BOOFCV_QR_ALIGNMENT_QR_CODE_ALIGNMENT_PATTERN_LOCATOR_HPP

#include "boofcv_qr/qr_code.hpp"
#include "boofcv_qr/qr_code_binary_grid_reader.hpp"

#include <opencv2/core.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace boofcv_qr {

class QrCodeAlignmentPatternLocator {
public:
    QrCodeAlignmentPatternLocator() = default;

    // Uses the previously-detected finder-pattern geometry (passed as
    // explicit parameters since our `QrCode` doesn't have geometry
    // fields yet — see algorithm doc) to seed the search for the
    // alignment patterns. Result lands on `qr.alignment[]`.
    //
    // `image` must be CV_8UC1. `ppCorner` / `ppRight` / `ppDown` are
    // the four-corner polygons of the three finder patterns in the
    // expected QR finder layout. The other args mirror
    // `QrCodeBinaryGridReader::setMarker`.
    bool process(const cv::Mat& image, QrCode& qr,
                  const std::array<cv::Point2d, 4>& ppCorner,
                  const std::array<cv::Point2d, 4>& ppRight,
                  const std::array<cv::Point2d, 4>& ppDown,
                  const std::vector<cv::Point2d>& priorAlignmentCenters,
                  const std::vector<cv::Point2d>& priorAlignmentGridCoords);

    // Lens distortion — deferred (same pattern as steps 5 / 7b).
    void setLensDistortion(int32_t /*width*/, int32_t /*height*/) {}
    void clearLensDistortion() {}

    // Toggle the commented-out Java `localize()` edge-scan path on.
    // Default false (matches upstream which has this branch commented
    // out at line 139). When true, `localize()` runs *between*
    // `centerOnSquare` and `meanshift` like the upstream-but-disabled
    // call — the parity diagnostic path. See algorithm doc.
    bool getUseEdgeScan() const { return useEdgeScan_; }
    void setUseEdgeScan(bool v) { useEdgeScan_ = v; }

    QrCodeBinaryGridReader& getReader() { return reader_; }

    // ---- TEST-VISIBLE — Java JUnit reaches into these directly.

    // Populates `qr.alignment[]` and `lookup_[]` with the expected
    // grid coordinates, skipping the three corners. Same name as Java.
    void initializePatterns(QrCode& qr);

    // Static utility: index of the steepest down-step in the array.
    // Returns -1 if no down-step is found. Mirrors Java.
    static int32_t greatestDown(const std::vector<float>& array);
    // Same, but for the steepest up-step. Mirrors Java's signature.
    static int32_t greatestUp(const std::vector<float>& array, int32_t start);

    // Coarse subpixel centring on the alignment-pattern stone via 3x3
    // grey-image gradient walk.
    bool centerOnSquare(QrCode::Alignment& pattern, float guessY, float guessX);

    // Edge-scan localizer (commented out at line 139 of the Java
    // source). Reachable via setUseEdgeScan(true).
    bool localize(QrCode::Alignment& pattern, float guessY, float guessX);

    // Final subpixel refinement via mean-shift on the local grey.
    bool meanshift(QrCode::Alignment& pattern, float guessY, float guessX);

private:
    bool localizePositionPatterns(const std::vector<int32_t>& alignmentLocations);

    QrCodeBinaryGridReader reader_;

    // Workspace for `localize()` (the edge-scan path) — sized 12 in
    // Java; same here. `samples_` is for `centerOnSquare`'s 3x3 grid.
    std::vector<float> arrayX_{12, 0.0f};
    std::vector<float> arrayY_{12, 0.0f};
    std::array<float, 9> samples_{};

    // Lookup grid for the 2-D index → Alignment* mapping. Slots that
    // overlap a finder corner store nullptr (Java's `null`); other
    // slots point into `qr.alignment[]` (which is grown contiguously
    // by `initializePatterns` and not resized afterwards — see the
    // algorithm doc).
    std::vector<QrCode::Alignment*> lookup_;

    QrCode* qr_ = nullptr;

    float threshold_ = 0.0f;

    bool useEdgeScan_ = false;
};

}  // namespace boofcv_qr

#endif  // BOOFCV_QR_ALIGNMENT_QR_CODE_ALIGNMENT_PATTERN_LOCATOR_HPP
