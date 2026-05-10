// Port of QrCodeBinaryGridReader. CV_8UC1 only.

#include "boofcv_qr/qr_code_binary_grid_reader.hpp"

#include <stdexcept>

namespace boofcv_qr {

void QrCodeBinaryGridReader::setImage(const cv::Mat& image) {
    if (image.type() != CV_8UC1)
        throw std::invalid_argument(
            "QrCodeBinaryGridReader requires CV_8UC1 input");
    image_ = image;
    imageWidth = image.cols;
    imageHeight = image.rows;
}

void QrCodeBinaryGridReader::setSquare(
    const std::array<cv::Point2d, 4>& square, float threshold_) {
    transformGrid.setTransformFromSquare(square);
    threshold = threshold_;
}

void QrCodeBinaryGridReader::setMarker(
    const QrCode& qr, const std::array<cv::Point2d, 4>& ppCorner,
    const std::array<cv::Point2d, 4>& ppRight,
    const std::array<cv::Point2d, 4>& ppDown,
    const std::vector<cv::Point2d>& alignmentCenters,
    const std::vector<cv::Point2d>& alignmentGridCoords) {
    transformGrid.addAllFeatures(qr, ppCorner, ppRight, ppDown,
                                 alignmentCenters, alignmentGridCoords);
    transformGrid.removeOutsideCornerFeatures();
    transformGrid.computeTransform();
    threshold = static_cast<float>(
        (qr.threshCorner + qr.threshDown + qr.threshRight) / 3.0);
}

void QrCodeBinaryGridReader::setMarker(const QrCode& qr) {
    transformGrid.addAllFeatures(qr);
    transformGrid.removeOutsideCornerFeatures();
    transformGrid.computeTransform();
    threshold = static_cast<float>(
        (qr.threshCorner + qr.threshDown + qr.threshRight) / 3.0);
}

void QrCodeBinaryGridReader::setMarkerUnknownVersion(const QrCode& qr,
                                                     float threshold_) {
    transformGrid.setTransformFromLinesSquare(qr);
    threshold = threshold_;
}

void QrCodeBinaryGridReader::imageToGrid(double x, double y,
                                         cv::Point2d& grid) const {
    transformGrid.imageToGrid(x, y, grid);
}

void QrCodeBinaryGridReader::gridToImage(double row, double col,
                                         cv::Point2d& pixel) const {
    transformGrid.gridToImage(row, col, pixel);
}

float QrCodeBinaryGridReader::read(float row, float col) const {
    cv::Point2d pixel;
    transformGrid.gridToImage(row, col, pixel);
    return sampleNearest(pixel.x, pixel.y);
}

void QrCodeBinaryGridReader::readBitIntensity(
    int32_t row, int32_t col, std::vector<float>& intensity) const {
    // The caller (`readBitIntensityAndThresholdDownRight`) accumulates
    // 5 floats per call into a single buffer, so we append. Pre-cycle-4
    // the body issued 5 push_back() calls — each with its own capacity
    // check + size increment. Replace with a single resize() to grow by
    // 5 (capacity is already reserved by the caller, so this is a
    // non-allocating size bump) followed by 5 indexed writes.
    constexpr float center = 0.5f;
    const std::size_t base = intensity.size();
    intensity.resize(base + 5);
    float* out = intensity.data() + base;

    cv::Point2d pixel;
    transformGrid.gridToImage(row + center - 0.2, col + center, pixel);
    out[0] = sampleNearest(pixel.x, pixel.y);
    transformGrid.gridToImage(row + center + 0.2, col + center, pixel);
    out[1] = sampleNearest(pixel.x, pixel.y);
    transformGrid.gridToImage(row + center, col + center - 0.2, pixel);
    out[2] = sampleNearest(pixel.x, pixel.y);
    transformGrid.gridToImage(row + center, col + center + 0.2, pixel);
    out[3] = sampleNearest(pixel.x, pixel.y);
    transformGrid.gridToImage(row + center, col + center, pixel);
    out[4] = sampleNearest(pixel.x, pixel.y);
}

int32_t QrCodeBinaryGridReader::readBit(int32_t row, int32_t col) const {
    constexpr double center = 0.5;
    cv::Point2d pixel;

    transformGrid.gridToImage(row + center - 0.2, col + center, pixel);
    float pixel01 = sampleNearest(pixel.x, pixel.y);
    transformGrid.gridToImage(row + center + 0.2, col + center, pixel);
    float pixel21 = sampleNearest(pixel.x, pixel.y);
    transformGrid.gridToImage(row + center, col + center - 0.2, pixel);
    float pixel10 = sampleNearest(pixel.x, pixel.y);
    transformGrid.gridToImage(row + center, col + center + 0.2, pixel);
    float pixel12 = sampleNearest(pixel.x, pixel.y);
    transformGrid.gridToImage(row + center, col + center, pixel);
    float pixel00 = sampleNearest(pixel.x, pixel.y);

    int32_t total = 0;
    if (pixel01 < threshold) total++;
    if (pixel21 < threshold) total++;
    if (pixel10 < threshold) total++;
    if (pixel12 < threshold) total++;
    if (pixel00 < threshold) total++;

    return total >= 3 ? 1 : 0;
}

}  // namespace boofcv_qr
