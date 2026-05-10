// Port of QrCodeBinaryGridToPixel. Homography math via OpenCV per
// CLAUDE.md (cv::getPerspectiveTransform, cv::findHomography,
// cv::perspectiveTransform — never cv::warpPerspective in this path).

#include "boofcv_qr/qr_code_binary_grid_to_pixel.hpp"

#include <opencv2/imgproc.hpp>

#include <array>
#include <cfloat>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace boofcv_qr {

namespace {

// Apply a 3x3 projective transform to a single 2D point. Bit-identical
// to `cv::perspectiveTransform` over a 1-element CV_64FC2 Mat on the
// fast path (`|w| > FLT_EPSILON`), and matches OpenCV's `(0, 0)`
// zero-fill fallback on the degenerate branch (`|w| <= FLT_EPSILON`).
// OpenCV's 64f path also gates on FLT_EPSILON (not DBL_EPSILON) and
// computes the reciprocal once, then multiplies — we mirror both.
//
// Skips the per-call cv::Mat allocation, NAryMatIterator setup, and
// dispatch overhead that dominate when this is hit millions of times
// per QR scan from `QrCodeBinaryGridReader::readBit` (5 calls per
// module bit) and `QrCodeAlignmentPatternLocator`.
inline void applyHomography(const cv::Matx33d& M, double x, double y,
                            cv::Point2d& out) {
    const double w = M(2, 0) * x + M(2, 1) * y + M(2, 2);
    if (std::fabs(w) > FLT_EPSILON) {
        const double iw = 1.0 / w;
        out.x = (M(0, 0) * x + M(0, 1) * y + M(0, 2)) * iw;
        out.y = (M(1, 0) * x + M(1, 1) * y + M(1, 2)) * iw;
    } else {
        out.x = 0.0;
        out.y = 0.0;
    }
}

}  // namespace

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

void QrCodeBinaryGridToPixel::addAllFeatures(const QrCode& qr) {
    // Java reads `a.pixel.x/y` and `a.moduleX/Y + 0.5f` for each entry of
    // `qr.alignment[]`. We translate to the parallel-vector form the
    // 6-arg overload expects.
    std::vector<cv::Point2d> alignmentCenters;
    std::vector<cv::Point2d> alignmentGridCoords;
    alignmentCenters.reserve(qr.alignment.size());
    alignmentGridCoords.reserve(qr.alignment.size());
    for (std::size_t i = 0; i < qr.alignment.size(); i++) {
        const QrCode::Alignment& a = qr.alignment[i];
        alignmentCenters.push_back(a.pixel);
        alignmentGridCoords.push_back(
            cv::Point2d(static_cast<double>(a.moduleX) + 0.5,
                        static_cast<double>(a.moduleY) + 0.5));
    }
    addAllFeatures(qr, qr.ppCorner, qr.ppRight, qr.ppDown, alignmentCenters,
                   alignmentGridCoords);
}

void QrCodeBinaryGridToPixel::setTransformFromLinesSquare(const QrCode& qr) {
    // Verbatim port of Java's
    // `boofcv.alg.geo.h.HomographyDirectLinearTransform.process(points2D,
    // points3D, conics=null, foundH)` for the QR setting where:
    //   - points2D = the three reliable corners of ppCorner (corner [0],
    //     the outside corner, is intentionally skipped — Java comment:
    //     "prone to damage").
    //   - points3D = four direction-line correspondences (z=0 on both
    //     sides) connecting ppCorner to ppRight / ppDown.
    //
    // The DLT solves `s × H f = 0` (cross product) reformatted to
    // `A * vec(H) = 0` and finds the right null vector via SVD. For each
    // 2D point pair (f, s) Java's `addPoints2D` writes 2 rows:
    //   row1: cols 3..5 = (-f.x, -f.y, -1),  cols 6..8 = (s.y*f.x, s.y*f.y, s.y)
    //   row2: cols 0..2 = ( f.x,  f.y,  1),  cols 6..8 = (-s.x*f.x, -s.x*f.y, -s.x)
    //
    // For each 3D point (line direction with z=0) pair (f, s):
    //   row1: cols 3..5 = (-s.z*f.x, -s.z*f.y, -s.z*f.z),  cols 6..8 = (s.y*f.x, s.y*f.y, s.y*f.z)
    //   row2: cols 0..2 = ( s.z*f.x,  s.z*f.y,  s.z*f.z),  cols 6..8 = (-s.x*f.x, -s.x*f.y, -s.x*f.z)
    //
    // The mixed point/line DLT here is not expressible as a point-
    // correspondence problem — line endpoints aren't point
    // correspondences, they're co-linear direction vectors at infinity
    // (z=0). Build the 2N×9 design matrix explicitly and solve via
    // cv::SVDecomp, mirroring Java's SolveNullSpaceSvd_DDRM. The same
    // explicit-DLT pattern is used in computeTransform() for the N>4
    // pure-point case; see CLAUDE.md "OpenCV substitution policy" and
    // the sampler algorithm doc for why cv::findHomography(method=0) is
    // not pure DLT for npoints > 4. Convention everywhere in this file:
    // H maps **image (x, y) → grid (col, row)**, i.e. p1=image, p2=grid.

    // ---- 2D point correspondences (3 of ppCorner — skip outside [0]). ----
    // Java's setLine endpoints in the QR caller use grid coords (col, row)
    // packed into Point3D_F64 as (col, row, 1) for points; mirror that.
    struct Pt2 {
        double f_x, f_y;       // image (p1)
        double s_x, s_y;       // grid  (p2): s.x=col, s.y=row
    };
    std::array<Pt2, 3> points2D{
        Pt2{qr.ppCorner[1].x, qr.ppCorner[1].y, 7.0, 0.0},  // grid (col, row) = (7, 0)
        Pt2{qr.ppCorner[2].x, qr.ppCorner[2].y, 7.0, 7.0},
        Pt2{qr.ppCorner[3].x, qr.ppCorner[3].y, 0.0, 7.0}};

    // ---- 3D direction-line correspondences (4 lines). ----
    // Each `setLine(row0, col0, row1, col1, polygon0, corner0, polygon1,
    // corner1)` builds:
    //   p1 = (image dx, image dy, 0) normalised        ← f
    //   p2 = (grid dcol, grid drow, 0) normalised      ← s   (s.x=dcol, s.y=drow)
    struct Pt3 {
        double f_x, f_y, f_z;  // image direction (p1, z=0)
        double s_x, s_y, s_z;  // grid direction  (p2, z=0): s.x=dcol, s.y=drow
    };

    auto makeLine = [](double row0, double col0, double row1, double col1,
                       const cv::Point2d& c0, const cv::Point2d& c1) -> Pt3 {
        double p1x = c1.x - c0.x;
        double p1y = c1.y - c0.y;
        double p1z = 0.0;
        double p2x = col1 - col0;
        double p2y = row1 - row0;
        double p2z = 0.0;
        double n1 = std::sqrt(p1x * p1x + p1y * p1y + p1z * p1z);
        double n2 = std::sqrt(p2x * p2x + p2y * p2y + p2z * p2z);
        if (n1 > 0.0) { p1x /= n1; p1y /= n1; p1z /= n1; }
        if (n2 > 0.0) { p2x /= n2; p2y /= n2; p2z /= n2; }
        return Pt3{p1x, p1y, p1z, p2x, p2y, p2z};
    };

    std::array<Pt3, 4> points3D{
        makeLine(0, 7,  0, 14, qr.ppCorner[1], qr.ppRight[0]),
        makeLine(7, 7,  7, 14, qr.ppCorner[2], qr.ppRight[3]),
        makeLine(7, 7, 14,  7, qr.ppCorner[2], qr.ppDown[1]),
        makeLine(7, 0, 14,  0, qr.ppCorner[3], qr.ppDown[0])};

    // ---- Build design matrix A: 2*num2D + 2*num3D rows, 9 cols. ----
    // Mirrors Java's HomographyDirectLinearTransform.computeTotalRows
    // (with numConic=0): `2*num2D + 2*num3D + 9*numConic`.
    int32_t num2D = static_cast<int32_t>(points2D.size());
    int32_t num3D = static_cast<int32_t>(points3D.size());
    int32_t numRows = 2 * num2D + 2 * num3D;
    cv::Mat A = cv::Mat::zeros(numRows, 9, CV_64F);
    int32_t rows = 0;

    // ---- addPoints2D ----
    for (std::size_t i = 0; i < points2D.size(); i++) {
        const Pt2& p = points2D[i];
        // Row 1: cols 3..5 = (-f.x, -f.y, -1), cols 6..8 = (s.y*f.x, s.y*f.y, s.y).
        A.at<double>(rows, 3) = -p.f_x;
        A.at<double>(rows, 4) = -p.f_y;
        A.at<double>(rows, 5) = -1.0;
        A.at<double>(rows, 6) = p.s_y * p.f_x;
        A.at<double>(rows, 7) = p.s_y * p.f_y;
        A.at<double>(rows, 8) = p.s_y;
        rows++;
        // Row 2: cols 0..2 = (f.x, f.y, 1), cols 6..8 = (-s.x*f.x, -s.x*f.y, -s.x).
        A.at<double>(rows, 0) = p.f_x;
        A.at<double>(rows, 1) = p.f_y;
        A.at<double>(rows, 2) = 1.0;
        A.at<double>(rows, 6) = -p.s_x * p.f_x;
        A.at<double>(rows, 7) = -p.s_x * p.f_y;
        A.at<double>(rows, 8) = -p.s_x;
        rows++;
    }

    // ---- addPoints3D ----
    for (std::size_t i = 0; i < points3D.size(); i++) {
        const Pt3& p = points3D[i];
        // Row 1: cols 3..5 = (-s.z*f.x, -s.z*f.y, -s.z*f.z),
        //        cols 6..8 = ( s.y*f.x,  s.y*f.y,  s.y*f.z).
        A.at<double>(rows, 3) = -p.s_z * p.f_x;
        A.at<double>(rows, 4) = -p.s_z * p.f_y;
        A.at<double>(rows, 5) = -p.s_z * p.f_z;
        A.at<double>(rows, 6) =  p.s_y * p.f_x;
        A.at<double>(rows, 7) =  p.s_y * p.f_y;
        A.at<double>(rows, 8) =  p.s_y * p.f_z;
        rows++;
        // Row 2: cols 0..2 = ( s.z*f.x,  s.z*f.y,  s.z*f.z),
        //        cols 6..8 = (-s.x*f.x, -s.x*f.y, -s.x*f.z).
        A.at<double>(rows, 0) =  p.s_z * p.f_x;
        A.at<double>(rows, 1) =  p.s_z * p.f_y;
        A.at<double>(rows, 2) =  p.s_z * p.f_z;
        A.at<double>(rows, 6) = -p.s_x * p.f_x;
        A.at<double>(rows, 7) = -p.s_x * p.f_y;
        A.at<double>(rows, 8) = -p.s_x * p.f_z;
        rows++;
    }

    // ---- Solve nullspace via SVD; H = right-singular vector at smallest σ. ----
    // Java uses SolveNullSpaceSvd_DDRM which returns the right-singular
    // vector of the smallest singular value reshaped to 3x3 row-major.
    cv::Mat w, u, vt;
    cv::SVDecomp(A, w, u, vt, cv::SVD::FULL_UV);
    // vt is 9x9; row 8 is the smallest singular vector. Reshape row-major
    // into the 3x3 H.
    H = cv::Matx33d(vt.at<double>(8, 0), vt.at<double>(8, 1), vt.at<double>(8, 2),
                    vt.at<double>(8, 3), vt.at<double>(8, 4), vt.at<double>(8, 5),
                    vt.at<double>(8, 6), vt.at<double>(8, 7), vt.at<double>(8, 8));
    cv::invert(H, Hinv);
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
    cv::Point2d predicted;
    for (std::size_t i = 0; i < pairs2D.size(); i++) {
        const auto& p = pairs2D[i];
        applyHomography(Hinv, p.p2.x, p.p2.y, predicted);
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

    if (pairs2D.size() == 4) {
        // 4-point homography solved exactly by cv::getPerspectiveTransform.
        // Algorithm matches BoofCV's GenerateHomographyLinear at this size.
        std::vector<cv::Point2f> src(4), dst(4);
        for (std::size_t i = 0; i < 4; i++) {
            src[i] = cv::Point2f(static_cast<float>(pairs2D[i].p1.x),
                                 static_cast<float>(pairs2D[i].p1.y));
            dst[i] = cv::Point2f(static_cast<float>(pairs2D[i].p2.x),
                                 static_cast<float>(pairs2D[i].p2.y));
        }
        cv::Mat H_mat = cv::getPerspectiveTransform(src, dst);
        H_mat.convertTo(H_mat, CV_64F);
        H = cv::Matx33d(H_mat.ptr<double>());
    } else {
        // > 4 correspondences: pure DLT via cv::SVD::solveZ on the 2N x 9
        // design matrix. cv::findHomography(method=0) is NOT pure DLT for
        // N > 4: it post-processes the DLT solution with iterative
        // Levenberg-Marquardt refinement (cv::LMSolverImpl::run path,
        // confirmed in profile data) which diverges from BoofCV's
        // GenerateHomographyLinear -> HomographyDirectLinearTransform path
        // (no LM, no Hartley normalization — `shouldNormalize` is hard-coded
        // false in BoofCV's process()). Building A and solving via
        // cv::SVD::solveZ matches BoofCV's SolveNullSpaceSvd_DDRM exactly.
        //
        // For each pair (f = image (p1), s = grid (p2)) Java's addPoints2D
        // writes 2 rows:
        //   row1: cols 3..5 = (-f.x, -f.y, -1),
        //         cols 6..8 = ( s.y*f.x,  s.y*f.y,  s.y)
        //   row2: cols 0..2 = ( f.x,  f.y,  1),
        //         cols 6..8 = (-s.x*f.x, -s.x*f.y, -s.x)
        // Same structure as setTransformFromLinesSquare's 2D block.
        const int32_t numRows = 2 * static_cast<int32_t>(pairs2D.size());
        cv::Mat A = cv::Mat::zeros(numRows, 9, CV_64F);
        for (std::size_t i = 0; i < pairs2D.size(); i++) {
            const double f_x = pairs2D[i].p1.x;
            const double f_y = pairs2D[i].p1.y;
            const double s_x = pairs2D[i].p2.x;
            const double s_y = pairs2D[i].p2.y;
            double* r0 = A.ptr<double>(static_cast<int32_t>(2 * i));
            double* r1 = A.ptr<double>(static_cast<int32_t>(2 * i + 1));
            r0[3] = -f_x;
            r0[4] = -f_y;
            r0[5] = -1.0;
            r0[6] = s_y * f_x;
            r0[7] = s_y * f_y;
            r0[8] = s_y;
            r1[0] = f_x;
            r1[1] = f_y;
            r1[2] = 1.0;
            r1[6] = -s_x * f_x;
            r1[7] = -s_x * f_y;
            r1[8] = -s_x;
        }

        cv::Mat h;
        cv::SVD::solveZ(A, h);
        // h is a 9-element column vector; reshape row-major into the 3x3 H.
        // Note: scale and sign of H are projectively irrelevant — both
        // applyHomography (perspective divide cancels scalars) and
        // cv::invert handle any non-zero scalar multiple identically.
        // BoofCV's AdjustHomographyMatrix (post-DLT scale/sign canon) is
        // not needed for our consumers.
        const double* hp = h.ptr<double>();
        H = cv::Matx33d(hp[0], hp[1], hp[2],
                        hp[3], hp[4], hp[5],
                        hp[6], hp[7], hp[8]);
    }
    cv::invert(H, Hinv);

    adjustments.clear();
    if (adjustWithFeatures) {
        adjustments.reserve(pairs2D.size());
        cv::Point2d predicted;
        for (const auto& p : pairs2D) {
            applyHomography(Hinv, p.p2.x, p.p2.y, predicted);
            adjustments.push_back(
                cv::Point2d(p.p1.x - predicted.x, p.p1.y - predicted.y));
        }
    }
}

// Slow-path helper for the inlined `gridToImage` (header). Hit only when
// `adjustWithFeatures` is on AND `adjustments` is non-empty — neither is
// true on the bit-sampler hot path, so this stays out-of-line to keep
// the inlined fast-path small. Body is unchanged from the pre-cycle-5
// gridToImage tail; mirrors BoofCV's per-pair nearest-neighbour residual
// lookup.
void QrCodeBinaryGridToPixel::applyAdjustment(double row, double col,
                                              cv::Point2d& pixel) const {
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

}  // namespace boofcv_qr
