// Port of boofcv.alg.fiducial.qrcode.QrCodePositionPatternDetector
// (BoofCV v1.3.0). Step 7c.

#ifndef BOOFCV_QR_FINDER_QR_CODE_POSITION_PATTERN_DETECTOR_HPP
#define BOOFCV_QR_FINDER_QR_CODE_POSITION_PATTERN_DETECTOR_HPP

#include "boofcv_qr/finder/square_locator_pattern_detector_base.hpp"
#include "boofcv_qr/position_pattern_node.hpp"

#include <opencv2/core.hpp>

#include <cstdint>
#include <vector>

namespace boofcv_qr {

class QrCodePositionPatternDetector : public SquareLocatorPatternDetectorBase {
public:
    explicit QrCodePositionPatternDetector(
        std::shared_ptr<DetectPolygonBinaryGrayRefine> squareDetector);

    void resetRuntimeProfiling() {
        squareDetector_->resetRuntimeProfiling();
    }

    // Position-pattern candidates surviving the 1:1:3:1:1 check, with
    // their geometric centre and the local gray threshold populated.
    // Reference invalidated by next `process()` call.
    const std::vector<PositionPatternNode>& getPositionPatterns() const {
        return positionPatterns_;
    }

    // Mutable variant — used by step 9's orchestrator which sometimes
    // needs to update node state in place. Mirrors the part-2 wrapper's
    // friend-access pattern.
    std::vector<PositionPatternNode>& getMutablePositionPatterns() {
        return positionPatterns_;
    }

    // ---- TEST-VISIBLE — Java JUnit reaches into these directly.

    // True if the polygon's two centerlines pass the 1:1:3:1:1 ratio
    // check. `square.size()` must be 4 (caller's responsibility).
    bool checkPositionPatternAppearance(const std::vector<cv::Point2d>& square,
                                          float grayThreshold);

    // Static check on a 7-element intensity scan: X.XXX.X (black =
    // below threshold, white = above). Mirrors Java's static helper.
    static bool positionSquareIntensityCheck(const float* values, float threshold);

protected:
    void findLocatorPatternsFromSquares() override;

private:
    // Runs the detector, writes survivors to `positionPatterns_`.
    void squaresToPositionList();

    bool checkLine(const std::vector<cv::Point2d>& square, float grayThreshold,
                    int32_t side);

    std::vector<PositionPatternNode> positionPatterns_;

    // Workspace mirroring Java's class-level fields.
    static constexpr int32_t SAMPLES_LEN = 9 * 5 + 1;  // 46
    static constexpr int32_t RUN_LEN_CAP = 12;
    float samples_[SAMPLES_LEN] = {0};
    int32_t length_[RUN_LEN_CAP] = {0};
    int32_t type_[RUN_LEN_CAP] = {0};
};

}  // namespace boofcv_qr

#endif  // BOOFCV_QR_FINDER_QR_CODE_POSITION_PATTERN_DETECTOR_HPP
