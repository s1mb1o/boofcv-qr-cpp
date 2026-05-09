// Port of boofcv.alg.fiducial.qrcode.QrCodePositionPatternDetector
// (BoofCV v1.3.0). Verbatim per CLAUDE.md "Verbatim vs idiomize".

#include "boofcv_qr/finder/qr_code_position_pattern_detector.hpp"
#include "boofcv_qr/squares/square_graph.hpp"

namespace boofcv_qr {

namespace {

// Mirror of `georegression.geometry.UtilPoint2D_F64.mean(a, b, out)`.
inline void pointMean(const cv::Point2d& a, const cv::Point2d& b, cv::Point2d& out) {
    out.x = (a.x + b.x) / 2.0;
    out.y = (a.y + b.y) / 2.0;
}

}  // namespace

QrCodePositionPatternDetector::QrCodePositionPatternDetector(
    std::shared_ptr<DetectPolygonBinaryGrayRefine> squareDetector)
    : SquareLocatorPatternDetectorBase(std::move(squareDetector)) {}

void QrCodePositionPatternDetector::findLocatorPatternsFromSquares() {
    squaresToPositionList();
}

void QrCodePositionPatternDetector::squaresToPositionList() {
    positionPatterns_.clear();
    auto& infoList = squareDetector_->getMutablePolygonInfo();
    for (std::size_t i = 0; i < infoList.size(); i++) {
        DetectPolygonFromContour::DetectedInfo& info = infoList[i];

        // The test below has been commented out because the new external only contour
        // detector discards all information related to internal contours
        // squares with no internal contour cannot possibly be a finder pattern
        //			if( !info.hasInternal() )
        //				continue;

        // See if the appearance matches a finder pattern
        double grayThreshold = (info.edgeInside + info.edgeOutside) / 2;
        if (!checkPositionPatternAppearance(info.polygon, static_cast<float>(grayThreshold)))
            continue;

        // refine the edge estimate
        squareDetector_->refine(info);

        positionPatterns_.emplace_back();
        PositionPatternNode& pp = positionPatterns_.back();
        pp.square = info.polygon;
        pp.grayThreshold = grayThreshold;

        SquareGraph::computeNodeInfo(pp);
    }
}

bool QrCodePositionPatternDetector::checkPositionPatternAppearance(
    const std::vector<cv::Point2d>& square, float grayThreshold) {
    return (checkLine(square, grayThreshold, 0) ||
            checkLine(square, grayThreshold, 1));
}

bool QrCodePositionPatternDetector::checkLine(
    const std::vector<cv::Point2d>& square, float grayThreshold, int32_t side) {
    // find the mid point between two parallel sides
    int32_t c0 = side;
    int32_t c1 = (side + 1) % 4;
    int32_t c2 = (side + 2) % 4;
    int32_t c3 = (side + 3) % 4;

    cv::Point2d segA, segB;
    pointMean(square[static_cast<std::size_t>(c0)],
              square[static_cast<std::size_t>(c1)], segA);
    pointMean(square[static_cast<std::size_t>(c2)],
              square[static_cast<std::size_t>(c3)], segB);

    // Mirror of `UtilLine2D_F64.convert(segment, parametric)` —
    // p = segment.a, slope = (segment.b - segment.a).
    double pX = segA.x;
    double pY = segA.y;
    double slopeX = segB.x - segA.x;
    double slopeY = segB.y - segA.y;

    // Scan along the line plus some extra
    constexpr int32_t SAMPLES_LEN_LOCAL = SAMPLES_LEN;
    int32_t period = SAMPLES_LEN_LOCAL / 9;
    double N = SAMPLES_LEN_LOCAL - 2 * period - 1;

    for (int32_t i = 0; i < SAMPLES_LEN_LOCAL; i++) {
        double location = (i - period) / N;

        float x = static_cast<float>(pX + location * slopeX);
        float y = static_cast<float>(pY + location * slopeY);

        samples_[i] = sampleBilinear(x, y);
    }

    // threshold and compute run length encoding

    int32_t size = 0;
    bool black = samples_[0] < grayThreshold;
    type_[0] = black ? 0 : 1;
    // Reset length_ tracking — Java's class fields are zeroed at
    // class init then mutated; we re-zero per call to keep the state
    // local to this scan.
    for (int32_t k = 0; k < RUN_LEN_CAP; k++) length_[k] = 0;

    for (int32_t i = 0; i < SAMPLES_LEN_LOCAL; i++) {
        bool b = samples_[i] < grayThreshold;

        if (black == b) {
            length_[size]++;
        } else {
            black = b;
            if (size < RUN_LEN_CAP - 1) {
                size += 1;
                type_[size] = black ? 0 : 1;
                length_[size] = 1;
            } else {
                break;
            }
        }
    }
    size++;

    // if too simple or too complex reject
    if (size < 5 || size > 9)
        return false;
    // detect finder pattern inside RLE
    for (int32_t i = 0; i + 5 <= size; i++) {
        if (type_[i] != 0)
            continue;

        int32_t black0 = length_[i];
        int32_t black1 = length_[i + 2];
        int32_t black2 = length_[i + 4];

        int32_t white0 = length_[i + 1];
        int32_t white1 = length_[i + 3];

        // the center black area can get exagerated easily
        if (black0 < 0.4 * white0 || black0 > 3 * white0)
            continue;
        if (black2 < 0.4 * white1 || black2 > 3 * white1)
            continue;

        int32_t black02 = black0 + black2;

        if (black1 >= black02 && black1 <= 2 * black02)
            return true;
    }
    return false;
}

bool QrCodePositionPatternDetector::positionSquareIntensityCheck(
    const float* values, float threshold) {
    if (values[0] > threshold || values[1] < threshold)
        return false;
    if (values[2] > threshold || values[3] > threshold || values[4] > threshold)
        return false;
    if (values[5] < threshold || values[6] > threshold)
        return false;
    return true;
}

}  // namespace boofcv_qr
