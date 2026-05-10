// Port of QrCodeBinaryGridReader. CV_8UC1 only.

#include "boofcv_qr/qr_code_binary_grid_reader.hpp"

#include <cmath>
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

float QrCodeBinaryGridReader::sampleNearest(double x, double y) const {
    // BoofCV's `NearestNeighborPixel_U8.get(x, y)` does `(int)x`, `(int)y`
    // — truncation toward zero — then clamps via the EXTENDED border.
    // Use std::floor (== truncation for non-negatives, but stable for the
    // possible-negative coords cv::perspectiveTransform can produce).
    int32_t ix = static_cast<int32_t>(std::floor(x));
    int32_t iy = static_cast<int32_t>(std::floor(y));
    if (ix < 0) ix = 0;
    if (iy < 0) iy = 0;
    if (ix >= imageWidth) ix = imageWidth - 1;
    if (iy >= imageHeight) iy = imageHeight - 1;
    return static_cast<float>(image_.at<std::uint8_t>(iy, ix));
}

float QrCodeBinaryGridReader::read(float row, float col) const {
    cv::Point2d pixel;
    transformGrid.gridToImage(row, col, pixel);
    return sampleNearest(pixel.x, pixel.y);
}

void QrCodeBinaryGridReader::readBitIntensity(
    int32_t row, int32_t col, std::vector<float>& intensity) const {
    constexpr float center = 0.5f;
    cv::Point2d pixel;

    transformGrid.gridToImage(row + center - 0.2, col + center, pixel);
    intensity.push_back(sampleNearest(pixel.x, pixel.y));
    transformGrid.gridToImage(row + center + 0.2, col + center, pixel);
    intensity.push_back(sampleNearest(pixel.x, pixel.y));
    transformGrid.gridToImage(row + center, col + center - 0.2, pixel);
    intensity.push_back(sampleNearest(pixel.x, pixel.y));
    transformGrid.gridToImage(row + center, col + center + 0.2, pixel);
    intensity.push_back(sampleNearest(pixel.x, pixel.y));
    transformGrid.gridToImage(row + center, col + center, pixel);
    intensity.push_back(sampleNearest(pixel.x, pixel.y));
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
