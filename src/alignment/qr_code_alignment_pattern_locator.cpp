// Port of boofcv.alg.fiducial.qrcode.QrCodeAlignmentPatternLocator
// (BoofCV v1.3.0). Verbatim per CLAUDE.md "Verbatim vs idiomize".

#include "boofcv_qr/alignment/qr_code_alignment_pattern_locator.hpp"

#include <algorithm>
#include <cmath>

namespace boofcv_qr {

bool QrCodeAlignmentPatternLocator::process(
    const cv::Mat& image, QrCode& qr,
    const std::array<cv::Point2d, 4>& ppCorner,
    const std::array<cv::Point2d, 4>& ppRight,
    const std::array<cv::Point2d, 4>& ppDown,
    const std::vector<cv::Point2d>& priorAlignmentCenters,
    const std::vector<cv::Point2d>& priorAlignmentGridCoords) {
    qr_ = &qr;
    // this must be cleared before calling setMarker or else the distortion will be messed up
    qr.alignment.clear();

    reader_.setImage(image);
    reader_.setMarker(qr, ppCorner, ppRight, ppDown,
                       priorAlignmentCenters, priorAlignmentGridCoords);

    threshold_ = static_cast<float>(qr.threshCorner);

    initializePatterns(qr);

    // version 1 has no alignment patterns
    if (qr.version <= 1)
        return true;
    return localizePositionPatterns(
        QrCode::VERSION_INFO()[static_cast<std::size_t>(qr.version)].alignment);
}

void QrCodeAlignmentPatternLocator::initializePatterns(QrCode& qr) {
    const std::vector<int32_t>& where =
        QrCode::VERSION_INFO()[static_cast<std::size_t>(qr.version)].alignment;
    qr.alignment.clear();
    lookup_.clear();

    // First pass: count the non-corner cells to size qr.alignment so
    // pointers from `lookup_` stay valid for the rest of process().
    int32_t size = static_cast<int32_t>(where.size());
    int32_t nonCorner = 0;
    for (int32_t row = 0; row < size; row++) {
        for (int32_t col = 0; col < size; col++) {
            bool skip = false;
            if (row == 0 && col == 0)
                skip = true;
            else if (row == 0 && col == size - 1)
                skip = true;
            else if (row == size - 1 && col == 0)
                skip = true;
            if (!skip) nonCorner++;
        }
    }
    qr.alignment.reserve(static_cast<std::size_t>(nonCorner));

    for (int32_t row = 0; row < size; row++) {
        for (int32_t col = 0; col < size; col++) {
            bool skip = false;
            if (row == 0 && col == 0)
                skip = true;
            else if (row == 0 && col == size - 1)
                skip = true;
            else if (row == size - 1 && col == 0)
                skip = true;

            if (skip) {
                lookup_.push_back(nullptr);
            } else {
                qr.alignment.emplace_back();
                QrCode::Alignment& a = qr.alignment.back();
                a.moduleX = where[static_cast<std::size_t>(col)];
                a.moduleY = where[static_cast<std::size_t>(row)];
                lookup_.push_back(&a);
            }
        }
    }
}

bool QrCodeAlignmentPatternLocator::localizePositionPatterns(
    const std::vector<int32_t>& alignmentLocations) {
    int32_t size = static_cast<int32_t>(alignmentLocations.size());

    for (int32_t row = 0; row < size; row++) {
        for (int32_t col = 0; col < size; col++) {
            QrCode::Alignment* a = lookup_[static_cast<std::size_t>(row * size + col)];
            if (a == nullptr)
                continue;

            // adjustment from previously found alignment patterns
            double adjY = 0, adjX = 0;

            if (row > 0) {
                QrCode::Alignment* p =
                    lookup_[static_cast<std::size_t>((row - 1) * size + col)];
                if (p != nullptr)
                    adjY = p->moduleY + 0.5 - p->moduleFound.y;
            }
            if (col > 0) {
                QrCode::Alignment* p =
                    lookup_[static_cast<std::size_t>(row * size + col - 1)];
                if (p != nullptr)
                    adjX = p->moduleX + 0.5 - p->moduleFound.x;
            }

            if (!centerOnSquare(*a,
                                  static_cast<float>(a->moduleY + 0.5 + adjY),
                                  static_cast<float>(a->moduleX + 0.5 + adjX))) {
                return false;
            }

            // The Java source has a `localize()` call here, but it's
            // commented out at line 139 of the upstream source. We
            // make it reachable behind `setUseEdgeScan(true)` for
            // parity diagnostics; default flow skips it.
            if (useEdgeScan_) {
                if (!localize(*a, static_cast<float>(a->moduleFound.y),
                                static_cast<float>(a->moduleFound.x))) {
                    return false;
                }
            }

            if (!meanshift(*a, static_cast<float>(a->moduleFound.y),
                              static_cast<float>(a->moduleFound.x))) {
                return false;
            }
        }
    }
    return true;
}

bool QrCodeAlignmentPatternLocator::centerOnSquare(QrCode::Alignment& pattern,
                                                    float guessY, float guessX) {
    float step = 1;
    float bestMag = std::numeric_limits<float>::max();
    float bestX = guessX;
    float bestY = guessY;

    for (int32_t i = 0; i < 10; i++) {
        for (int32_t row = 0; row < 3; row++) {
            float gridy = guessY - 1.0f + row;
            for (int32_t col = 0; col < 3; col++) {
                float gridx = guessX - 1.0f + col;

                samples_[static_cast<std::size_t>(row * 3 + col)] =
                    reader_.read(gridy, gridx);
            }
        }

        float dx = (samples_[2] + samples_[5] + samples_[8]) -
                   (samples_[0] + samples_[3] + samples_[6]);
        float dy = (samples_[6] + samples_[7] + samples_[8]) -
                   (samples_[0] + samples_[1] + samples_[2]);

        float r = std::sqrt(dx * dx + dy * dy);

        if (bestMag > r) {
            //				System.out.println("good step at "+i);
            bestMag = r;
            bestX = guessX;
            bestY = guessY;
        } else {
            //				System.out.println("bad step at "+i);
            step *= 0.75f;
        }

        if (r > 0) {
            guessX = bestX + step * dx / r;
            guessY = bestY + step * dy / r;
        } else {
            break;
        }
    }

    pattern.moduleFound.x = bestX;
    pattern.moduleFound.y = bestY;

    reader_.gridToImage(pattern.moduleFound.y, pattern.moduleFound.x,
                         pattern.pixel);

    return true;
}

bool QrCodeAlignmentPatternLocator::localize(QrCode::Alignment& pattern,
                                               float guessY, float guessX) {
    // sample along the middle. Try to not sample the outside edges which could confuse it
    int32_t N = static_cast<int32_t>(arrayY_.size());
    for (int32_t i = 0; i < N; i++) {
        float x = guessX - 1.5f + i * 3.0f / 12.0f;
        float y = guessY - 1.5f + i * 3.0f / 12.0f;

        arrayX_[static_cast<std::size_t>(i)] = reader_.read(guessY, x);
        arrayY_[static_cast<std::size_t>(i)] = reader_.read(y, guessX);
    }

    // TODO turn this into an exhaustive search of the array for best up and down point?
    int32_t downX = greatestDown(arrayX_);
    if (downX == -1) return false;
    int32_t upX = greatestUp(arrayX_, downX);
    if (upX == -1) return false;

    int32_t downY = greatestDown(arrayY_);
    if (downY == -1) return false;
    int32_t upY = greatestUp(arrayY_, downY);
    if (upY == -1) return false;

    pattern.moduleFound.x = guessX - 1.5f + (downX + upX) * 3.0f / 24.0f;
    pattern.moduleFound.y = guessY - 1.5f + (downY + upY) * 3.0f / 24.0f;

    reader_.gridToImage(pattern.moduleFound.y, pattern.moduleFound.x,
                         pattern.pixel);

    return true;
}

bool QrCodeAlignmentPatternLocator::meanshift(QrCode::Alignment& pattern,
                                                float guessY, float guessX) {
    //		System.out.println("before "+guessX+" "+guessY);
    float step = 1;
    float decay = 0.7f;
    for (int32_t i = 0; i < 10; i++) {
        float sumX = 0;
        float sumY = 0;
        float total = 0;

        for (int32_t y = 0; y < 8; y++) {
            float dy = -1.5f + 3.0f * y / 7.0f;
            float gridY = guessY + dy;
            for (int32_t x = 0; x < 8; x++) {
                float dx = -1.5f + 3.0f * x / 7.0f;
                float gridX = guessX + dx;
                float v = reader_.read(gridY, gridX);
                float r = std::sqrt(dx * dx + dy * dy);

                float w = std::max(-10.0f,
                                     (r > 0.5f ? v - threshold_ : threshold_ - v));
                total += std::fabs(w);
                sumX += w * dx;
                sumY += w * dy;
            }
        }

        guessX += step * sumX / total;
        guessY += step * sumY / total;
        step *= decay;
    }

    //		System.out.println("after "+guessX+" "+guessY+"\n");

    pattern.moduleFound.x = guessX;
    pattern.moduleFound.y = guessY;

    reader_.gridToImage(pattern.moduleFound.y, pattern.moduleFound.x,
                         pattern.pixel);

    return true;
}

int32_t QrCodeAlignmentPatternLocator::greatestDown(
    const std::vector<float>& array) {
    int32_t best = -1;
    float bestScore = 0;

    int32_t N = static_cast<int32_t>(array.size());
    for (int32_t i = 5; i < N; i++) {
        float diff = (4.0f / 2.0f) *
                     (array[static_cast<std::size_t>(i - 5)] +
                      array[static_cast<std::size_t>(i)]);
        diff -= array[static_cast<std::size_t>(i - 4)] +
                array[static_cast<std::size_t>(i - 3)] +
                array[static_cast<std::size_t>(i - 2)] +
                array[static_cast<std::size_t>(i - 1)];

        if (diff > bestScore) {
            bestScore = diff;
            best = i - 4;
        }
    }
    return best;
}

int32_t QrCodeAlignmentPatternLocator::greatestUp(
    const std::vector<float>& array, int32_t start) {
    int32_t best = -1;
    float bestScore = 0;

    int32_t N = static_cast<int32_t>(array.size());
    for (int32_t i = start; i < N; i++) {
        float diff = array[static_cast<std::size_t>(i)] -
                     array[static_cast<std::size_t>(i - 1)];
        if (diff > bestScore) {
            bestScore = diff;
            best = i - 1;
        }
    }
    return best;
}

}  // namespace boofcv_qr
