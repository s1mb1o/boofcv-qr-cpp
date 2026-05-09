// Port of QrCodeBinaryGridToPixel. Homography math via OpenCV per
// CLAUDE.md (cv::getPerspectiveTransform, cv::findHomography,
// cv::perspectiveTransform — never cv::warpPerspective in this path).

#include "boofcv_qr/qr_code_binary_grid_to_pixel.hpp"

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include <limits>
#include <stdexcept>

namespace boofcv_qr {

QrCodeBinaryGridToPixel::QrCodeBinaryGridToPixel() = default;

void QrCodeBinaryGridToPixel::recomputeInverse() {
    cv::invert(H, Hinv);
}

void QrCodeBinaryGridToPixel::setHomographyInv(const cv::Matx33d& Hinv_) {
    Hinv = Hinv_;
    cv::invert(Hinv, H);
}

void QrCodeBinaryGridToPixel::setTransformFromSquare(
    const std::array<cv::Point2d, 4>& square) {
    // 4-point homography: square[0..3] (image pixels) <-> finder corners
    // at grid coordinates (col, row): (0,0),(7,0),(7,7),(0,7).
    // The Java original: set(0,0,square,0); set(0,7,square,1);
    //                    set(7,7,square,2); set(7,0,square,3);
    // (row, col, polygon, corner) — first arg is row, second is col, so
    // grid coord (col, row) = (0,0), (7,0), (7,7), (0,7) for the 4
    // image-side points respectively.
    pairs2D.clear();
    pairs2D.reserve(4);
    pairs2D.push_back({square[0], cv::Point2d(0.0, 0.0)});
    pairs2D.push_back({square[1], cv::Point2d(7.0, 0.0)});
    pairs2D.push_back({square[2], cv::Point2d(7.0, 7.0)});
    pairs2D.push_back({square[3], cv::Point2d(0.0, 7.0)});

    adjustWithFeatures = false;
    computeTransform();
}

namespace {

void appendCorner(std::vector<AssociatedPair>& out,
                  const std::array<cv::Point2d, 4>& polygon, int corner,
                  double row, double col) {
    out.push_back({polygon[static_cast<std::size_t>(corner)],
                   cv::Point2d(col, row)});
}

}  // namespace

void QrCodeBinaryGridToPixel::addAllFeatures(
    const QrCode& qr,
    const std::array<cv::Point2d, 4>& ppCorner,
    const std::array<cv::Point2d, 4>& ppRight,
    const std::array<cv::Point2d, 4>& ppDown,
    const std::vector<cv::Point2d>& alignmentCenters,
    const std::vector<cv::Point2d>& alignmentGridCoords) {
    if (alignmentCenters.size() != alignmentGridCoords.size())
        throw std::invalid_argument(
            "alignmentCenters and alignmentGridCoords must be parallel");

    adjustWithFeatures = false;
    pairs2D.clear();

    int32_t N = qr.getNumberOfModules();

    // Top-left finder pattern.
    appendCorner(pairs2D, ppCorner, 0, 0, 0);  // outside corner
    appendCorner(pairs2D, ppCorner, 1, 0, 7);
    appendCorner(pairs2D, ppCorner, 2, 7, 7);
    appendCorner(pairs2D, ppCorner, 3, 7, 0);

    // Top-right finder pattern.
    appendCorner(pairs2D, ppRight, 0, 0, N - 7);
    appendCorner(pairs2D, ppRight, 1, 0, N);  // outside corner
    appendCorner(pairs2D, ppRight, 2, 7, N);
    appendCorner(pairs2D, ppRight, 3, 7, N - 7);

    // Bottom-left finder pattern.
    appendCorner(pairs2D, ppDown, 0, N - 7, 0);
    appendCorner(pairs2D, ppDown, 1, N - 7, 7);
    appendCorner(pairs2D, ppDown, 2, N, 7);
    appendCorner(pairs2D, ppDown, 3, N, 0);  // outside corner

    // Alignment-pattern centers (caller already converted from
    // QrCode.Alignment.{moduleX, moduleY} to grid coords with +0.5
    // sub-module offsets).
    for (std::size_t i = 0; i < alignmentCenters.size(); i++) {
        pairs2D.push_back({alignmentCenters[i], alignmentGridCoords[i]});
    }
}

void QrCodeBinaryGridToPixel::removeOutsideCornerFeatures() {
    // Indices 0, 5, 11 are the outside corners of ppCorner/ppRight/ppDown
    // per addAllFeatures' insertion order. Remove largest-index first so
    // the lower indices stay valid.
    if (pairs2D.size() < 12)
        throw std::runtime_error(
            "removeOutsideCornerFeatures requires the 12 finder corners");
    pairs2D.erase(pairs2D.begin() + 11);
    pairs2D.erase(pairs2D.begin() + 5);
    pairs2D.erase(pairs2D.begin() + 0);
}

bool QrCodeBinaryGridToPixel::removeFeatureWithLargestError() {
    int32_t selected = -1;
    double largestError = 0.0;

    // Java reference: transform p.p2 (grid coord) through Hinv to get
    // an image-pixel prediction; compare against p.p1 (observed pixel).
    for (std::size_t i = 0; i < pairs2D.size(); i++) {
        const auto& p = pairs2D[i];
        cv::Mat src = (cv::Mat_<cv::Point2d>(1, 1) << p.p2);
        cv::Mat dst;
        cv::perspectiveTransform(src, dst, Hinv);
        cv::Point2d predicted = dst.at<cv::Point2d>(0, 0);
        double dx = predicted.x - p.p1.x;
        double dy = predicted.y - p.p1.y;
        double error = dx * dx + dy * dy;
        if (error > largestError) {
            largestError = error;
            selected = static_cast<int32_t>(i);
        }
    }
    if (selected != -1 && largestError > 4.0) {
        pairs2D.erase(pairs2D.begin() + selected);
        return true;
    }
    return false;
}

void QrCodeBinaryGridToPixel::computeTransform() {
    if (pairs2D.size() < 4)
        throw std::runtime_error("Need >=4 correspondences for homography");

    std::vector<cv::Point2f> src(pairs2D.size()), dst(pairs2D.size());
    for (std::size_t i = 0; i < pairs2D.size(); i++) {
        // Java H maps image -> grid (Hinv = grid -> image). We follow
        // the same convention.
        src[i] = cv::Point2f(static_cast<float>(pairs2D[i].p1.x),
                             static_cast<float>(pairs2D[i].p1.y));
        dst[i] = cv::Point2f(static_cast<float>(pairs2D[i].p2.x),
                             static_cast<float>(pairs2D[i].p2.y));
    }

    cv::Mat H_mat;
    if (pairs2D.size() == 4) {
        H_mat = cv::getPerspectiveTransform(src, dst);
    } else {
        // No RANSAC — DLT only, matching BoofCV's GenerateHomographyLinear.
        H_mat = cv::findHomography(src, dst, 0);
    }
    H_mat.convertTo(H_mat, CV_64F);
    H = cv::Matx33d(H_mat.ptr<double>());
    cv::invert(H, Hinv);

    adjustments.clear();
    if (adjustWithFeatures) {
        adjustments.reserve(pairs2D.size());
        for (const auto& p : pairs2D) {
            cv::Mat in = (cv::Mat_<cv::Point2d>(1, 1) << p.p2);
            cv::Mat out;
            cv::perspectiveTransform(in, out, Hinv);
            cv::Point2d predicted = out.at<cv::Point2d>(0, 0);
            adjustments.push_back(
                cv::Point2d(p.p1.x - predicted.x, p.p1.y - predicted.y));
        }
    }
}

void QrCodeBinaryGridToPixel::imageToGrid(double x, double y,
                                          cv::Point2d& grid) const {
    cv::Mat src = (cv::Mat_<cv::Point2d>(1, 1) << cv::Point2d(x, y));
    cv::Mat dst;
    cv::perspectiveTransform(src, dst, H);
    grid = dst.at<cv::Point2d>(0, 0);
}

void QrCodeBinaryGridToPixel::gridToImage(double row, double col,
                                          cv::Point2d& pixel) const {
    cv::Mat src = (cv::Mat_<cv::Point2d>(1, 1) << cv::Point2d(col, row));
    cv::Mat dst;
    cv::perspectiveTransform(src, dst, Hinv);
    pixel = dst.at<cv::Point2d>(0, 0);

    if (adjustWithFeatures && !adjustments.empty()) {
        // Nearest-pair lookup; cheap, mirrors Java.
        std::size_t closest = 0;
        double best = std::numeric_limits<double>::max();
        for (std::size_t i = 0; i < pairs2D.size(); i++) {
            double dx = pairs2D[i].p2.x - col;
            double dy = pairs2D[i].p2.y - row;
            double d = dx * dx + dy * dy;
            if (d < best) {
                best = d;
                closest = i;
            }
        }
        const cv::Point2d& adj = adjustments[closest];
        pixel.x += adj.x;
        pixel.y += adj.y;
    }
}

}  // namespace boofcv_qr
