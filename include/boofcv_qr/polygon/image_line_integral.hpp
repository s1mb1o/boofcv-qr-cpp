// Port of boofcv.alg.interpolate.ImageLineIntegral (BoofCV v1.3.0).
// Step 7b (part 3).
//
// Computes the line integral of a line segment across an image: lay
// the line over the image and sum (pixel value) * (line-fraction-in-
// pixel) for every pixel the line touches.
//
// Per CLAUDE.md type mappings: GrayU8 -> cv::Mat CV_8UC1.

#ifndef BOOFCV_QR_POLYGON_IMAGE_LINE_INTEGRAL_HPP
#define BOOFCV_QR_POLYGON_IMAGE_LINE_INTEGRAL_HPP

#include <opencv2/core.hpp>

#include <cstdint>

namespace boofcv_qr {

class ImageLineIntegral {
public:
    // Specify input image. Image must be CV_8UC1.
    void setImage(const cv::Mat& image);

    // Computes the line segment's line integral across the inside of
    // the image. Inside is `0 <= x < width`, `0 <= y < height` (open
    // upper bound).
    double compute(double x0, double y0, double x1, double y1);

    void releaseImage() {
        image_.release();
        imageWidth_ = 0;
        imageHeight_ = 0;
        length_ = 0.0;
    }

    // True if `(x, y)` is inside the image. Strict less-than at the
    // upper bound (matches BoofCV's `BoofMiscOps.isInside`).
    bool isInside(double x, double y) const;

    double getLength() const { return length_; }

private:
    cv::Mat image_;
    int32_t imageWidth_ = 0;
    int32_t imageHeight_ = 0;
    double length_ = 0.0;

    // pixel value at integer coords; truncation-toward-zero (same as
    // BoofCV's `unsafe_get`/`unsafe_getD`).
    inline double pixel(int32_t x, int32_t y) const {
        return static_cast<double>(image_.at<uint8_t>(y, x));
    }
};

}  // namespace boofcv_qr

#endif  // BOOFCV_QR_POLYGON_IMAGE_LINE_INTEGRAL_HPP
