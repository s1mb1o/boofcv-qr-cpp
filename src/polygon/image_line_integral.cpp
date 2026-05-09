// Port of boofcv.alg.interpolate.ImageLineIntegral (BoofCV v1.3.0).
// Verbatim per CLAUDE.md "Verbatim vs idiomize".

#include "boofcv_qr/polygon/image_line_integral.hpp"

#include <cmath>
#include <stdexcept>

namespace boofcv_qr {

namespace {

// Mirror of `org.ejml.UtilEjml.TEST_F64`. Used only inside compute() for
// the very-small-step numerical guard.
constexpr double TEST_F64 = 1e-8;

}  // namespace

void ImageLineIntegral::setImage(const cv::Mat& image) {
    if (image.type() != CV_8UC1) {
        throw std::invalid_argument(
            "ImageLineIntegral: image must be CV_8UC1");
    }
    image_ = image;
    imageWidth_ = image.cols;
    imageHeight_ = image.rows;
}

bool ImageLineIntegral::isInside(double x, double y) const {
    // Mirror of `BoofMiscOps.isInside(width, height, x, y)`. Strict
    // less-than at the upper bound — `(11.99999, 13.999)` is inside a
    // 12×14 image, but `(12, 14)` is not.
    return x >= 0.0 && x < imageWidth_ && y >= 0.0 && y < imageHeight_;
}

double ImageLineIntegral::compute(double x0, double y0, double x1, double y1) {

    double sum = 0;

    double slopeX = x1 - x0;
    double slopeY = y1 - y0;

    length_ = std::sqrt(slopeX * slopeX + slopeY * slopeY);

    int32_t sgnX = static_cast<int32_t>((slopeX > 0) - (slopeX < 0));
    int32_t sgnY = static_cast<int32_t>((slopeY > 0) - (slopeY < 0));

    int32_t px = static_cast<int32_t>(x0);
    int32_t py = static_cast<int32_t>(y0);

    if (slopeX == 0 || slopeY == 0) {
        // handle a pathological case
        if (slopeX == slopeY)
            return 0;

        double t;
        if (slopeX == 0) {
            t = slopeY > 0 ? py + 1 - y0 : py - y0;
        } else {
            t = slopeX > 0 ? px + 1 - x0 : px - x0;
        }

        t /= (slopeX + slopeY);
        if (t > 1) t = 1;
        if (t > 0)
            sum += t * pixel(px, py);
        double deltaT = (sgnX + sgnY) / (slopeX + slopeY);

        while (t < 1) {
            px += sgnX;
            py += sgnY;
            double nextT = t + deltaT;
            double actualDeltaT = deltaT;
            if (nextT > 1) {
                actualDeltaT = 1 - t;
            }
            t = nextT;
            sum += actualDeltaT * pixel(px, py);
        }
    } else {
        double deltaTX = slopeX > 0 ? px + 1 - x0 : px - x0;
        double deltaTY = slopeY > 0 ? py + 1 - y0 : py - y0;
        deltaTX /= slopeX;
        deltaTY /= slopeY;

        double t = std::min(deltaTX, deltaTY);
        if (t > 1) t = 1;
        if (t > 0)
            sum += t * pixel(px, py);

        double x = x0 + t * slopeX;
        double y = y0 + t * slopeY;
        px = static_cast<int32_t>(x);
        py = static_cast<int32_t>(y);
        int32_t nx = px + sgnX;
        int32_t ny = py + sgnY;

        while (t < 1) {
            deltaTX = (nx - x) / slopeX;  // TODO reformulate so that very small slope values can be handled?
            deltaTY = (ny - y) / slopeY;

            double deltaT = std::min(deltaTX, deltaTY);

            // see if it already hit the destination
            if (deltaT <= TEST_F64) {
                deltaT = std::max(deltaTX, deltaTY);
            }
            double nextT = t + deltaT;
            if (nextT > 1) {
                deltaT = 1 - t;
            }

            double sampleT = t + 0.5 * deltaT;
            x = x0 + sampleT * slopeX;
            y = y0 + sampleT * slopeY;

            sum += deltaT * pixel(static_cast<int32_t>(x), static_cast<int32_t>(y));
            t = t + deltaT;
            x = x0 + t * slopeX;
            y = y0 + t * slopeY;
            px = static_cast<int32_t>(x);
            py = static_cast<int32_t>(y);
            nx = px + sgnX;
            ny = py + sgnY;
        }
    }

    sum *= length_;

    return sum;
}

}  // namespace boofcv_qr
