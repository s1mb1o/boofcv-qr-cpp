// Port of QrCodeDecoderImage. Verbatim per CLAUDE.md — loop structure,
// variable names, and decode order match the Java original line-by-line.
//
// Notable deviations (all documented inline / in the .md):
//   - Lens distortion not ported (deferred). The Java's
//     `setLensDistortion(...)` plumbing isn't surfaced.
//   - Strategy-injection hooks added per CLAUDE.md "Public API design".
//     Defaults preserve verbatim Java behaviour.

#include "boofcv_qr/qr_code_decoder_image.hpp"

#include "boofcv_qr/qr_code_mask_pattern.hpp"
#include "boofcv_qr/qr_code_polynomial_math.hpp"
#include "boofcv_qr/squares/square_edge.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace boofcv_qr {

namespace {

// Mirror of `Intersection2D_F64.intersection(Point a0, Point a1, Point b0,
// Point b1, Point out)` for line-line intersection. Returns true on
// success (lines not parallel), writes intersection into `out`.
//
// Inlined here from `square_graph.cpp`'s anonymous-namespace helper —
// the orchestrator's `computeBoundingBox` needs the same primitive but
// we don't want to expose it in a public header.
bool lineLineIntersection(const cv::Point2d& a0, const cv::Point2d& a1,
                          const cv::Point2d& b0, const cv::Point2d& b1,
                          cv::Point2d& out) {
    double dxA = a1.x - a0.x;
    double dyA = a1.y - a0.y;
    double dxB = b1.x - b0.x;
    double dyB = b1.y - b0.y;

    double denom = dxA * dyB - dyA * dxB;
    if (denom == 0.0) return false;

    double t = ((b0.x - a0.x) * dyB - (b0.y - a0.y) * dxB) / denom;
    out.x = a0.x + t * dxA;
    out.y = a0.y + t * dyA;
    return true;
}

// `UtilPolygons2D_F64.shiftDown(square)` — rotates a 4-corner polygon
// by 1 (corner i becomes corner (i+1)%4 — i.e. the last element moves
// to the front).
void shiftDown(std::array<cv::Point2d, 4>& square) {
    cv::Point2d last = square[3];
    square[3] = square[2];
    square[2] = square[1];
    square[1] = square[0];
    square[0] = last;
}

// Copy `from` into `to` for a 4-corner polygon. Mirrors Java's
// `Polygon2D_F64.setTo(Polygon2D_F64)`.
void polygonCopy(std::array<cv::Point2d, 4>& to,
                 const std::array<cv::Point2d, 4>& from) {
    for (int32_t i = 0; i < 4; i++) {
        to[static_cast<std::size_t>(i)] = from[static_cast<std::size_t>(i)];
    }
}

// Copy a SquareNode's `square` (std::vector<cv::Point2d>) into a
// QrCode's `Polygon2D_F64`-shaped 4-corner array. Mirrors Java's
// implicit `Polygon2D_F64.setTo(Polygon2D_F64)` when assigning from a
// SquareNode (which BoofCV represents as a 4-corner polygon).
void copyPolygon(std::array<cv::Point2d, 4>& dst,
                 const std::vector<cv::Point2d>& src) {
    if (src.size() != 4)
        throw std::invalid_argument(
            "QrCodeDecoderImage: PositionPatternNode.square must have 4 corners");
    for (int32_t i = 0; i < 4; i++) {
        dst[static_cast<std::size_t>(i)] = src[static_cast<std::size_t>(i)];
    }
}

}  // namespace

static double elapsedMs(std::chrono::steady_clock::time_point start,
                        std::chrono::steady_clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

QrCodeDecoderImage::QrCodeDecoderImage()
    : QrCodeDecoderImage(Config{}) {}

QrCodeDecoderImage::QrCodeDecoderImage(Config cfg)
    : decoder_(std::move(cfg.forceEncoding), std::move(cfg.defaultEncoding)),
      considerTransposed_(cfg.considerTransposed),
      rsHook_(std::move(cfg.rs_decoder)),
      alignmentHook_(std::move(cfg.alignment_locator)) {
    // Plumb ignorePaddingBytes through to the bits decoder. Mirrors
    // Java's ConfigQrCode.ignorePaddingBytes (default true) being
    // propagated into QrCodeDecoderBits.ignorePaddingBytes by the
    // FactoryFiducial.qrcode wiring.
    decoder_.ignorePaddingBytes = cfg.ignorePaddingBytes;
}

bool QrCodeDecoderImage::runAlignmentLocator(const cv::Mat& gray, QrCode& qr) {
    if (alignmentHook_) {
        return alignmentHook_(gray, qr);
    }
    // The built-in `QrCodeAlignmentPatternLocator::process` predates
    // QrCode's geometry growth and takes explicit polygon args; bridge
    // to the explicit-polygon form. Empty alignment-prior vectors —
    // same as Java's call site.
    std::vector<cv::Point2d> emptyCenters, emptyGrid;
    alignmentLocator_.getReader().setImage(gray);
    return alignmentLocator_.process(gray, qr, qr.ppCorner, qr.ppRight,
                                     qr.ppDown, emptyCenters, emptyGrid);
}

bool QrCodeDecoderImage::runRsCorrect(QrCode& qr) {
    if (rsHook_)
        return rsHook_(qr);
    return decoder_.applyErrorCorrection(qr);
}

// ---- Java line ~88: process(List<PositionPatternNode> pps, T gray) ----
void QrCodeDecoderImage::process(const std::vector<PositionPatternNode>& pps,
                                 const cv::Mat& gray) {
    process(pps, gray, nullptr);
}

void QrCodeDecoderImage::process(const std::vector<PositionPatternNode>& pps,
                                 const cv::Mat& gray,
                                 QrCodeDecoderImageTiming* timing) {
    if (timing)
        *timing = QrCodeDecoderImageTiming{};

    gridReader_.setImage(gray);
    storageQR_.clear();
    successes_.clear();
    failures_.clear();

    for (std::size_t i = 0; i < pps.size(); i++) {
        const PositionPatternNode& ppn = pps[i];

        // Java: for (int j = 3, k = 0; k < 4; j = k, k++)
        for (int32_t j = 3, k = 0; k < 4; j = k, k++) {
            if (ppn.edges[static_cast<std::size_t>(j)] != nullptr &&
                ppn.edges[static_cast<std::size_t>(k)] != nullptr) {
                storageQR_.emplace_back();
                QrCode& qr = storageQR_.back();
                if (timing)
                    timing->candidates++;

                setPositionPatterns(ppn, j, k, qr);
                computeBoundingBox(qr);

                // Decode the entire marker now
                if (decode(gray, qr, timing)) {
                    if (qr.failureCause == Failure::NONE) {
                        successes_.push_back(qr);
                    } else {
                        failures_.push_back(qr);
                    }
                } else {
                    // Consider the possibility that the QR code was encoded
                    // incorrectly with transposed bits
                    bool success = false;
                    if (considerTransposed_) {
                        if (timing)
                            timing->transposedAttempts++;
                        transposePositionPatterns(qr);
                        success = decode(gray, qr, timing);
                    }

                    if (success) {
                        qr.bitsTransposed = true;
                        if (qr.failureCause == Failure::NONE) {
                            successes_.push_back(qr);
                        } else {
                            failures_.push_back(qr);
                        }
                    } else {
                        failures_.push_back(qr);
                    }
                }
            }
        }
    }
}

// ---- Java line ~142: transposePositionPatterns(QrCode qr) ----
void QrCodeDecoderImage::transposePositionPatterns(QrCode& qr) {
    polygonCopy(tempTranspose_, qr.ppDown);
    polygonCopy(qr.ppDown, qr.ppRight);
    polygonCopy(qr.ppRight, tempTranspose_);

    transposeCorners(qr.ppCorner);
    transposeCorners(qr.ppRight);
    transposeCorners(qr.ppDown);
}

// ---- Java line ~153: transposeCorners(Polygon2D_F64 c) (static) ----
void QrCodeDecoderImage::transposeCorners(std::array<cv::Point2d, 4>& c) {
    double tmpX = c[1].x;
    double tmpY = c[1].y;

    c[1] = c[3];
    c[3] = cv::Point2d(tmpX, tmpY);
}

// ---- Java line ~175: setPositionPatterns(...) (static) ----
void QrCodeDecoderImage::setPositionPatterns(const PositionPatternNode& ppn,
                                             int32_t cornerToRight,
                                             int32_t cornerToDown,
                                             QrCode& qr) {
    // copy the 3 position patterns over
    SquareEdge* edgeR = ppn.edges[static_cast<std::size_t>(cornerToRight)];
    SquareEdge* edgeD = ppn.edges[static_cast<std::size_t>(cornerToDown)];
    if (edgeR == nullptr || edgeD == nullptr)
        throw std::invalid_argument(
            "QrCodeDecoderImage::setPositionPatterns: edges must be present");

    // Java's `edge.destination(this)` returns the SquareNode at the
    // other end of the edge. Mirror that here.
    SquareNode* rightNode = (edgeR->a == &ppn) ? edgeR->b : edgeR->a;
    SquareNode* downNode = (edgeD->a == &ppn) ? edgeD->b : edgeD->a;

    PositionPatternNode* right = static_cast<PositionPatternNode*>(rightNode);
    PositionPatternNode* down = static_cast<PositionPatternNode*>(downNode);

    copyPolygon(qr.ppRight, right->square);
    copyPolygon(qr.ppCorner, ppn.square);
    copyPolygon(qr.ppDown, down->square);

    qr.threshRight = right->grayThreshold;
    qr.threshCorner = ppn.grayThreshold;
    qr.threshDown = down->grayThreshold;

    // Put it into canonical orientation
    int32_t indexR = right->findEdgeIndex(&ppn);
    int32_t indexD = down->findEdgeIndex(&ppn);

    rotateUntilAt(qr.ppRight, indexR, 3);
    rotateUntilAt(qr.ppCorner, cornerToRight, 1);
    rotateUntilAt(qr.ppDown, indexD, 0);
}

// ---- Java line ~198: rotateUntilAt(square, current, desired) (static) ----
void QrCodeDecoderImage::rotateUntilAt(std::array<cv::Point2d, 4>& square,
                                       int32_t current, int32_t desired) {
    while (current != desired) {
        shiftDown(square);
        current = (current + 1) % 4;
    }
}

// ---- Java line ~209: computeBoundingBox(QrCode qr) (static) ----
void QrCodeDecoderImage::computeBoundingBox(QrCode& qr) {
    qr.bounds[0] = qr.ppCorner[0];
    qr.bounds[1] = qr.ppRight[1];
    if (!lineLineIntersection(qr.ppRight[1], qr.ppRight[2], qr.ppDown[3],
                              qr.ppDown[2], qr.bounds[2])) {
        // Mirrors Java's behaviour — Intersection2D_F64.intersection
        // returns false on parallel lines but Java doesn't check it.
        // Leave bounds[2] at its default; downstream consumers will
        // see the geometry is degenerate via bounds[2] == (0, 0).
        qr.bounds[2] = cv::Point2d(0.0, 0.0);
    }
    qr.bounds[3] = qr.ppDown[3];
}

// ---- Java line ~226: decode(T gray, QrCode qr) ----
bool QrCodeDecoderImage::decode(const cv::Mat& gray, QrCode& qr,
                                QrCodeDecoderImageTiming* timing) {
    if (timing)
        timing->decodeAttempts++;

    auto t0 = std::chrono::steady_clock::now();
    bool formatOk = extractFormatInfo(qr);
    auto t1 = std::chrono::steady_clock::now();
    if (timing)
        timing->formatMs += elapsedMs(t0, t1);
    if (!formatOk) {
        qr.failureCause = Failure::FORMAT;
        return false;
    }
    t0 = std::chrono::steady_clock::now();
    bool versionOk = extractVersionInfo(qr);
    t1 = std::chrono::steady_clock::now();
    if (timing)
        timing->versionMs += elapsedMs(t0, t1);
    if (!versionOk) {
        qr.failureCause = Failure::VERSION;
        return false;
    }

    t0 = std::chrono::steady_clock::now();
    bool alignmentOk = runAlignmentLocator(gray, qr);
    t1 = std::chrono::steady_clock::now();
    if (timing)
        timing->alignmentMs += elapsedMs(t0, t1);
    if (!alignmentOk) {
        qr.failureCause = Failure::ALIGNMENT;
        return false;
    }

    // First try using all the features then remove features if they have
    // large errors and might have incorrectly localized
    bool success = false;
    gridReader_.setMarker(qr);
    gridReader_.getTransformGrid().addAllFeatures(qr);
    // by default, it removes outside corners. This works most of the time
    for (int32_t i = 0; i < 6; i++) {
        t0 = std::chrono::steady_clock::now();
        if (i > 0) {
            bool removed = gridReader_.getTransformGrid().removeFeatureWithLargestError();
            if (!removed) {
                t1 = std::chrono::steady_clock::now();
                if (timing)
                    timing->transformMs += elapsedMs(t0, t1);
                break;
            }
        }

        gridReader_.getTransformGrid().computeTransform();
        t1 = std::chrono::steady_clock::now();
        if (timing)
            timing->transformMs += elapsedMs(t0, t1);

        qr.failureCause = Failure::NONE;
        if (timing)
            timing->samplingAttempts++;
        t0 = std::chrono::steady_clock::now();
        bool rawOk = readRawData(qr);
        t1 = std::chrono::steady_clock::now();
        if (timing)
            timing->samplingMs += elapsedMs(t0, t1);
        if (!rawOk) {
            qr.failureCause = Failure::READING_BITS;
            continue;
        }
        if (timing)
            timing->rsAttempts++;
        t0 = std::chrono::steady_clock::now();
        bool rsOk = runRsCorrect(qr);
        t1 = std::chrono::steady_clock::now();
        if (timing)
            timing->rsMs += elapsedMs(t0, t1);
        if (!rsOk) {
            qr.failureCause = Failure::ERROR_CORRECTION;
            continue;
        }

        success = true;
        break;
    }

    if (success) {
        // Could have failed previously, then successfully decoded it
        qr.failureCause = Failure::NONE;

        // Parses the message. Return value is ignored since the parse
        // error is encoded in the message and we want to return true if
        // it could apply error correction
        if (timing)
            timing->messageAttempts++;
        t0 = std::chrono::steady_clock::now();
        decoder_.decodeMessage(qr);
        t1 = std::chrono::steady_clock::now();
        if (timing)
            timing->messageMs += elapsedMs(t0, t1);
    }

    qr.Hinv = gridReader_.getTransformGrid().Hinv;
    return success;
}

// ---- Java line ~291: extractFormatInfo(QrCode qr) ----
bool QrCodeDecoderImage::extractFormatInfo(QrCode& qr) {
    for (int32_t i = 0; i < 2; i++) {
        // probably a better way to do this would be to go with the region
        // that has the smallest hamming distance
        if (i == 0)
            readFormatRegion0(qr);
        else
            readFormatRegion1(qr);
        int32_t bitField = bits_.read(0, 15, false);
        bitField ^= QrCode::FORMAT_MASK;

        int32_t message;
        if (QrCodePolynomialMath::checkFormatBits(bitField)) {
            message = bitField >> 10;
        } else {
            message = QrCodePolynomialMath::correctFormatBits(bitField);
        }
        if (message >= 0) {
            QrCodePolynomialMath::decodeFormatMessage(message, qr);
            return true;
        }
    }
    return false;
}

// ---- Java line ~319: readFormatRegion0(QrCode qr) ----
bool QrCodeDecoderImage::readFormatRegion0(QrCode& qr) {
    // set the coordinate system to the closest pp to reduce position errors
    gridReader_.setSquare(qr.ppCorner, static_cast<float>(qr.threshCorner));

    bits_.resize(15);
    bits_.zero();
    for (int32_t i = 0; i < 6; i++) {
        read(i, i, 8);
    }

    read(6, 7, 8);
    read(7, 8, 8);
    read(8, 8, 7);

    for (int32_t i = 0; i < 6; i++) {
        read(9 + i, 8, 5 - i);
    }

    return true;
}

// ---- Java line ~343: readFormatRegion1(QrCode qr) ----
bool QrCodeDecoderImage::readFormatRegion1(QrCode& qr) {
    // set the coordinate system to the closest pp to reduce position errors
    gridReader_.setSquare(qr.ppRight, static_cast<float>(qr.threshRight));

    bits_.resize(15);
    bits_.zero();
    for (int32_t i = 0; i < 8; i++) {
        read(i, 8, 6 - i);
    }

    gridReader_.setSquare(qr.ppDown, static_cast<float>(qr.threshDown));

    for (int32_t i = 0; i < 7; i++) {
        read(i + 8, i, 8);
    }

    return true;
}

// ---- Java line ~365: readRawData(QrCode qr) ----
bool QrCodeDecoderImage::readRawData(QrCode& qr) {
    const VersionInfo& info =
        QrCode::VERSION_INFO()[static_cast<std::size_t>(qr.version)];

    qr.rawbits.assign(static_cast<std::size_t>(info.codewords), 0);

    // predeclare memory
    bits_.resize(info.codewords * 8);

    // Get the location of each bit. Java caches `QrCode.LOCATION_BITS[v]`;
    // we recompute on demand (cheap; ~v² modules) — the v=40 case is
    // the worst at 28k modules and still well under a millisecond.
    QrCodeCodeWordLocations locations =
        QrCodeCodeWordLocations::qrcode(qr.version);
    const std::vector<Point2I>& locationBits = locations.bits;

    // read the pixel intensity values at each bit while computing a
    // threshold for the lower right corner region
    qr.threshDownRight = readBitIntensityAndThresholdDownRight(qr, locationBits);

    // Convert the sampled intensity at each bit into a binary number
    bitIntensityToBitValue(qr, locationBits);

    // copy over the results
    for (std::size_t i = 0; i < qr.rawbits.size(); i++) {
        qr.rawbits[i] = bits_.data[i];
    }

    // CLAUDE.md "Public API design" mandate: surface the de-interleaved
    // pre-RS codewords for downstream multi-frame fusion / known-prefix
    // recovery pipelines.
    qr.rawCodewords = qr.rawbits;

    return true;
}

// ---- Java line ~396: readBitIntensityAndThresholdDownRight(...) ----
float QrCodeDecoderImage::readBitIntensityAndThresholdDownRight(
    QrCode& qr, const std::vector<Point2I>& locationBits) {
    // Sample the image intensity around each bit
    int32_t numModules = qr.getNumberOfModules();
    intensityBits_.clear();
    intensityBits_.reserve(locationBits.size() * 5);

    // end at bits.size instead of locationBits.size because location
    // might point to useless bits
    int32_t start = std::max(8, numModules - 10);

    // sum of pixel intensity in lower right
    float sumLowerRight = 0.0f;
    // Number of points which contribute to the sum
    int32_t total = 0;

    // measure the intensity around each bit's location
    for (int32_t bitIndex = 0; bitIndex < bits_.size; bitIndex++) {
        const Point2I& b = locationBits[static_cast<std::size_t>(bitIndex)];
        gridReader_.readBitIntensity(b.y, b.x, intensityBits_);

        // only consider points in the lower right corner
        if (b.x < start && b.y < start)
            continue;

        total += QrCodeBinaryGridReader::BIT_INTENSITY_SAMPLES;
        std::size_t end = intensityBits_.size();
        std::size_t startIdx =
            end - static_cast<std::size_t>(
                      QrCodeBinaryGridReader::BIT_INTENSITY_SAMPLES);
        for (std::size_t i = startIdx; i < end; i++) {
            sumLowerRight += intensityBits_[i];
        }
    }

    // simple average for the threshold. Could be improved with Otsu, but
    // be more expensive
    if (total == 0)
        return static_cast<float>(qr.threshCorner);
    return sumLowerRight / static_cast<float>(total);
}

// ---- Java line ~434: bitIntensityToBitValue(...) ----
void QrCodeDecoderImage::bitIntensityToBitValue(
    QrCode& qr, const std::vector<Point2I>& locationBits) {
    float gridSize = static_cast<float>(qr.getNumberOfModules()) - 1.0f;
    float threshold00 = static_cast<float>(qr.threshCorner);
    float threshold01 = static_cast<float>(qr.threshRight);
    float threshold10 = static_cast<float>(qr.threshDown);
    float threshold11 = static_cast<float>(qr.threshDownRight);

    int32_t intensitySize = static_cast<int32_t>(intensityBits_.size());
    for (int32_t intensityIndex = 0; intensityIndex < intensitySize;) {
        int32_t bitIndex = intensityIndex / 5;

        const Point2I& b = locationBits[static_cast<std::size_t>(bitIndex)];

        float bx = static_cast<float>(b.x) / gridSize;
        float by = static_cast<float>(b.y) / gridSize;

        // Compute threshold by performing bilinear interpolation between
        // local thresholds at each corner
        float threshold = 0.0f;
        threshold += (1.0f - bx) * (1.0f - by) * threshold00;
        threshold += bx * (1.0f - by) * threshold01;
        threshold += bx * by * threshold11;
        threshold += (1.0f - bx) * by * threshold10;

        int32_t votes = 0;
        votes += intensityBits_[static_cast<std::size_t>(intensityIndex++)] < threshold ? 1 : 0;
        votes += intensityBits_[static_cast<std::size_t>(intensityIndex++)] < threshold ? 1 : 0;
        votes += intensityBits_[static_cast<std::size_t>(intensityIndex++)] < threshold ? 1 : 0;
        votes += intensityBits_[static_cast<std::size_t>(intensityIndex++)] < threshold ? 1 : 0;
        votes += intensityBits_[static_cast<std::size_t>(intensityIndex++)] < threshold ? 1 : 0;

        int32_t bit = votes >= 3 ? 1 : 0;

        bits_.set(bitIndex, qr.mask->apply(b.y, b.x, bit));
    }
}

// ---- Java line ~476: read(int bit, int row, int col) ----
void QrCodeDecoderImage::read(int32_t bit, int32_t row, int32_t col) {
    int32_t value = gridReader_.readBit(row, col);
    if (value == -1) {
        // The requested region is outside the image. A partial QR code
        // can be read so let's just assign it a value of zero and let
        // error correction handle this
        value = 0;
    }
    bits_.set(bit, value);
}

// ---- Java line ~492: extractVersionInfo(QrCode qr) ----
bool QrCodeDecoderImage::extractVersionInfo(QrCode& qr) {
    int32_t version = estimateVersionBySize(qr);

    // For version 7 and beyond use the version which has been encoded
    // into the qr code
    if (version >= QrCode::VERSION_ENCODED_AT) {
        readVersionRegion0(qr);
        int32_t version0 = decodeVersion();
        readVersionRegion1(qr);
        int32_t version1 = decodeVersion();

        if (version0 < 1 && version1 < 1) {  // both decodings failed
            version = -1;
        } else if (version0 < 1) {  // one failed so use the good one
            version = version1;
        } else if (version1 < 1) {
            version = version0;
        } else if (version0 != version1) {
            version = -1;
        } else {
            version = version0;
        }
    } else if (version <= 0) {
        version = -1;
    }

    qr.version = version;
    return version >= 1 && version <= QrCode::MAX_VERSION;
}

// ---- Java line ~526: decodeVersion() ----
int32_t QrCodeDecoderImage::decodeVersion() {
    int32_t bitField = bits_.read(0, 18, false);
    int32_t message;
    // see if there's any errors
    if (QrCodePolynomialMath::checkVersionBits(bitField)) {
        message = bitField >> 12;
    } else {
        message = QrCodePolynomialMath::correctVersionBits(bitField);
    }
    // sanity check results
    if (message > QrCode::MAX_VERSION || message < QrCode::VERSION_ENCODED_AT)
        return -1;

    return message;
}

// ---- Java line ~546: estimateVersionBySize(QrCode qr) ----
int32_t QrCodeDecoderImage::estimateVersionBySize(QrCode& qr) {
    // Just need the homography for this corner square square
    gridReader_.setMarkerUnknownVersion(qr, 0.0f);

    // Compute location of position patterns relative to corner PP
    gridReader_.imageToGrid(qr.ppRight[0].x, qr.ppRight[0].y, grid_);

    // see if pp is miss aligned. Probably not a flat surface
    // or they don't belong to the same qr code
    if (std::fabs(grid_.y / grid_.x) >= 0.3)
        return -1;

    double versionX = ((grid_.x + 7) - 17) / 4;

    gridReader_.imageToGrid(qr.ppDown[0].x, qr.ppDown[0].y, grid_);

    if (std::fabs(grid_.x / grid_.y) >= 0.3)
        return -1;

    double versionY = ((grid_.y + 7) - 17) / 4;

    // Sanity check the estimated version
    int32_t averageVersion = static_cast<int32_t>((versionX + versionY) / 2.0 + 0.5);
    double versionError = std::fabs(versionX - versionY);

    // If the two are very close, always accept
    if (versionError <= 2.0)
        return averageVersion;

    // If it thinks the version is low, there will be no encoding to read
    if (averageVersion < QrCode::VERSION_ENCODED_AT)
        return -1;

    // If they are drastically not in agreement, probably not a QR code
    if (versionError / averageVersion > 0.4)
        return -1;

    return averageVersion;
}

// ---- Java line ~589: readVersionRegion0(QrCode qr) ----
bool QrCodeDecoderImage::readVersionRegion0(QrCode& qr) {
    // set the coordinate system to the closest pp to reduce position errors
    gridReader_.setSquare(qr.ppRight, static_cast<float>(qr.threshRight));

    bits_.resize(18);
    bits_.zero();
    for (int32_t i = 0; i < 18; i++) {
        int32_t row = i / 3;
        int32_t col = i % 3;
        read(i, row, col - 4);
    }

    return true;
}

// ---- Java line ~609: readVersionRegion1(QrCode qr) ----
bool QrCodeDecoderImage::readVersionRegion1(QrCode& qr) {
    // set the coordinate system to the closest pp to reduce position errors
    gridReader_.setSquare(qr.ppDown, static_cast<float>(qr.threshDown));

    bits_.resize(18);
    bits_.zero();
    for (int32_t i = 0; i < 18; i++) {
        int32_t row = i % 3;
        int32_t col = i / 3;
        read(i, row - 4, col);
    }

    return true;
}

// ============================================================
// CLAUDE.md "Public API design" mandates — stage-isolation entry points.
// ============================================================

// `find_finders` walks the supplied PositionPatternNode graph the same
// way `process()` does, but stops after `setPositionPatterns` /
// `computeBoundingBox` and emits a value-typed PositionPatternTriplet
// per accepted (j,k) pair. No mutable state on the orchestrator so
// this can be `static`.
std::vector<PositionPatternTriplet> QrCodeDecoderImage::find_finders(
    const std::vector<PositionPatternNode>& pps) {
    std::vector<PositionPatternTriplet> out;
    for (std::size_t i = 0; i < pps.size(); i++) {
        const PositionPatternNode& ppn = pps[i];
        for (int32_t j = 3, k = 0; k < 4; j = k, k++) {
            if (ppn.edges[static_cast<std::size_t>(j)] != nullptr &&
                ppn.edges[static_cast<std::size_t>(k)] != nullptr) {
                QrCode qr;
                setPositionPatterns(ppn, j, k, qr);
                computeBoundingBox(qr);
                PositionPatternTriplet t;
                t.ppCorner = qr.ppCorner;
                t.ppRight = qr.ppRight;
                t.ppDown = qr.ppDown;
                t.threshCorner = qr.threshCorner;
                t.threshRight = qr.threshRight;
                t.threshDown = qr.threshDown;
                out.push_back(t);
            }
        }
    }
    return out;
}

// `detect_polygons_only` — runs binarize → polygon → finder →
// version-estimation → alignment. The `version` field IS populated
// via `estimateVersionBySize` so the alignment locator can run with
// the right number of expected patterns; format/RS/mode-decode are
// skipped. Alignment runs through `runAlignmentLocator` so the
// injected alignment hook (CLAUDE.md mandate — clipped-QR fallback
// case) is honoured here too.
PolygonOnlyResult QrCodeDecoderImage::detect_polygons_only(
    const std::vector<PositionPatternNode>& pps, const cv::Mat& gray) {
    PolygonOnlyResult out;
    gridReader_.setImage(gray);

    for (std::size_t i = 0; i < pps.size(); i++) {
        const PositionPatternNode& ppn = pps[i];
        for (int32_t j = 3, k = 0; k < 4; j = k, k++) {
            if (ppn.edges[static_cast<std::size_t>(j)] != nullptr &&
                ppn.edges[static_cast<std::size_t>(k)] != nullptr) {
                QrCode qr;
                setPositionPatterns(ppn, j, k, qr);
                computeBoundingBox(qr);

                // Run version estimation only (no version-info bits read).
                int32_t v = estimateVersionBySize(qr);
                if (v >= 1 && v <= QrCode::MAX_VERSION) {
                    qr.version = v;
                    // Try the alignment locator via the same hook-or-
                    // default path that `decode()` uses. Don't propagate
                    // failures — polygon-only mode reports whatever it
                    // found.
                    runAlignmentLocator(gray, qr);
                }
                out.qrCodes.push_back(std::move(qr));
            }
        }
    }
    return out;
}

bool QrCodeDecoderImage::sample_bit_matrix(const cv::Mat& gray, QrCode& qr) {
    gridReader_.setImage(gray);
    gridReader_.setMarker(qr);
    gridReader_.getTransformGrid().addAllFeatures(qr);
    gridReader_.getTransformGrid().computeTransform();
    return readRawData(qr);
}

bool QrCodeDecoderImage::extract_raw_codewords(const cv::Mat& gray,
                                               QrCode& qr) {
    // Same path as sample_bit_matrix — readRawData() already mirrors
    // `qr.rawCodewords = qr.rawbits` per the mandate.
    return sample_bit_matrix(gray, qr);
}

bool QrCodeDecoderImage::rs_correct(QrCode& qr) {
    return runRsCorrect(qr);
}

bool QrCodeDecoderImage::decode_message(QrCode& qr) {
    return decoder_.decodeMessage(qr);
}

}  // namespace boofcv_qr
