// Port of boofcv.alg.fiducial.qrcode.QrCodeBinaryGridReader (BoofCV v1.3.0).
//
// Reads binary values from the QR's grid via a homography. Top-left of
// the QR is (0,0); +col = right, +row = down.
//
// CLAUDE.md mandates that the QR sampler reads the source binary
// directly at homography-mapped coordinates — no cv::warpPerspective.
// We stay faithful to that here: every read is a single-pixel sample
// (with optional 5-sample voting in `readBit`), never a warp.
//
// The Java original is templated on image type (`<T extends ImageGray>`).
// QR uses GrayU8 exclusively, and CLAUDE.md commits to `cv::Mat CV_8UC1`
// at the top level, so we drop the template.
//
// Algorithm description: src/sampler/qr_code_binary_grid_reader.md.

#ifndef BOOFCV_QR_QR_CODE_BINARY_GRID_READER_HPP
#define BOOFCV_QR_QR_CODE_BINARY_GRID_READER_HPP

#include <opencv2/core.hpp>

#include "boofcv_qr/qr_code.hpp"
#include "boofcv_qr/qr_code_binary_grid_to_pixel.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace boofcv_qr {

class QrCodeBinaryGridReader {
public:
    // Number of points sampled in readBitIntensity / readBit.
    static constexpr int32_t BIT_INTENSITY_SAMPLES = 5;

    QrCodeBinaryGridReader() = default;

    // The image must be CV_8UC1. We hold a shallow reference (cv::Mat
    // header copy); caller must keep the underlying buffer alive for
    // the duration of reads.
    void setImage(const cv::Mat& image);

    // 4-corner setup (single finder pattern). Sets the working threshold.
    void setSquare(const std::array<cv::Point2d, 4>& square, float threshold);

    // Full setup: pulls in finder corners + alignment patterns from `qr`.
    //
    // The 6-arg overload predates the QrCode geometry growth in step
    // 9; it is preserved for the step-7/8 callers that still need to
    // pass the polygons explicitly.
    void setMarker(const QrCode& qr,
                   const std::array<cv::Point2d, 4>& ppCorner,
                   const std::array<cv::Point2d, 4>& ppRight,
                   const std::array<cv::Point2d, 4>& ppDown,
                   const std::vector<cv::Point2d>& alignmentCenters,
                   const std::vector<cv::Point2d>& alignmentGridCoords);

    // Java's 1-arg signature — reads `qr.ppCorner` / `ppRight` /
    // `ppDown` / `alignment[]` directly. Called by the step-9
    // orchestrator. Equivalent to:
    //   transformGrid.addAllFeatures(qr);
    //   transformGrid.removeOutsideCornerFeatures();
    //   transformGrid.computeTransform();
    //   threshold = (qr.threshCorner + qr.threshDown + qr.threshRight)/3
    void setMarker(const QrCode& qr);

    // Estimate image-to-grid before the version is known. Used in the
    // orchestrator's `estimateVersionBySize` path. Mirrors Java's
    // `setMarkerUnknownVersion(QrCode, float)`.
    void setMarkerUnknownVersion(const QrCode& qr, float threshold_);

    // Coordinate transforms (forwarded to the underlying QrCodeBinary
    // GridToPixel).
    void imageToGrid(double x, double y, cv::Point2d& grid) const;
    void gridToImage(double row, double col, cv::Point2d& pixel) const;

    // Read a single nearest-neighbour sample at sub-pixel grid coord.
    // Returns the 8-bit intensity as a float (so it composes with the
    // Java float-based threshold in `readBit`).
    float read(float row, float col) const;

    // Append BIT_INTENSITY_SAMPLES = 5 sample intensities at (row, col)
    // (4 neighbours + centre, all at +0.5 sub-module offset).
    void readBitIntensity(int32_t row, int32_t col,
                          std::vector<float>& intensity) const;

    // Returns 0 or 1. Majority-vote of the 5 samples vs `threshold`.
    int32_t readBit(int32_t row, int32_t col) const;

    QrCodeBinaryGridToPixel& getTransformGrid() { return transformGrid; }
    const QrCodeBinaryGridToPixel& getTransformGrid() const { return transformGrid; }

private:
    QrCodeBinaryGridToPixel transformGrid;
    cv::Mat image_;
    int32_t imageWidth = 0;
    int32_t imageHeight = 0;
    float threshold = 127.0f;

    // Nearest-neighbour pixel read with EXTENDED-border clamping.
    // Mirrors BoofCV's `nearestNeighborPixelS` + `BorderType.EXTENDED`.
    float sampleNearest(double x, double y) const;
};

}  // namespace boofcv_qr

#endif  // BOOFCV_QR_QR_CODE_BINARY_GRID_READER_HPP
