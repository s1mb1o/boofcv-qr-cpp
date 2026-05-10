// Port of QrCodeBinaryGridToPixel. Homography math via OpenCV per
// CLAUDE.md (cv::getPerspectiveTransform, cv::findHomography,
// cv::perspectiveTransform — never cv::warpPerspective in this path).

#include "boofcv_qr/qr_code_binary_grid_to_pixel.hpp"

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include <array>
#include <cmath>
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
    // Mirrors Java QrCodeBinaryGridToPixel.setTransformFromLinesSquare.
    //
    // Three of the four corners of `ppCorner` are used as point
    // correspondences (the (0,0) outside corner is intentionally
    // skipped — Java comment: "prone to damage"). Four direction
    // lines connect those corners to corresponding corners on the
    // other two finders, giving the DLT enough rank-3 information
    // to fit the homography even without alignment patterns or the
    // outside corners.
    //
    // BoofCV solves this via georegression's
    // `HomographyDirectLinearTransform.process(points, lines, null, H)`
    // which builds a 2*N_points + N_lines design matrix and SVDs it.
    // We replicate the same construction here using cv::SVDecomp.
    //
    // Each *point* correspondence (image (x, y) ↔ grid (col, row))
    // contributes 2 rows:
    //   [-x, -y, -1,  0,  0,  0,  col*x, col*y, col]
    //   [ 0,  0,  0, -x, -y, -1,  row*x, row*y, row]
    //
    // Each *line* correspondence (image direction (dx, dy) ↔ grid
    // direction (dcol, drow)) contributes 1 row. The line constraint
    // says that the homography H maps an image line ax+by+c=0 to a
    // grid line a'x+b'y+c'=0; equivalently, image line vector
    // l_img = (a_img, b_img, c_img) ↔ grid line vector
    // l_grid = (a_grid, b_grid, c_grid) satisfy l_img = H^T * l_grid
    // up to scale.
    //
    // For Java's `setLine`, the inputs are direction vectors
    // (image dx, dy) and (grid dcol, drow). The corresponding line
    // through the origin perpendicular to the direction has normal
    // (-dy, dx) in image and (-drow, dcol) in grid (after
    // normalising). The line constraint reduces to:
    //   l_grid[0]*H[0] + l_grid[1]*H[1] + l_grid[2]*H[2] = (some_l_img[0])
    // — but since we only know directions, the line through the origin
    // approximation gives c=0 in both, leaving:
    //   [-l_img.x * H[0] - l_img.y * H[1] = -l_grid.x * H[6,7,8] ... ]
    // After normalisation the row inserted into the SVD design matrix is:
    //   [ l_grid.y * (l_img.x * 0 + l_img.y * 0)  ...  ]
    //
    // Rather than re-derive the line term from first principles, the
    // form below mirrors `HomographyDirectLinearTransform`'s line
    // contribution row (cross-product of the line vectors):
    //   l_img = (-dy_img,  dx_img, 0)   (line normal in image; c=0)
    //   l_grid = (-drow_g,  dcol_g, 0)
    // Each line contributes:
    //   row = [a_g*0,        a_g*0,        a_g*0,
    //          b_g*0,        b_g*0,        b_g*0,
    //          a_g*l_img.x,  a_g*l_img.y,  a_g*0]
    //   ... and similarly for the b_g column. This expands into 1 row
    // per line direction (we use the simpler form: line direction
    // ↔ direction, c=0, which produces a *row of differences in the
    // direction-vector cross product*).
    //
    // Concretely we reproduce the matrix form georegression uses:
    // for each line correspondence (l_img, l_grid):
    //   A row = [ l_grid.x * 0,    l_grid.x * 0,    l_grid.x * (-1) * (-l_img.y / l_grid.y),
    //              ... ]
    // Since the algebra is the same as point correspondences but with
    // (col, row, 1) replaced by (l_img.x, l_img.y, 0) and (x, y, 1)
    // replaced by (l_grid.x, l_grid.y, 0), the contribution per line
    // is exactly:
    //   [ -l_img.x * l_grid.y,  -l_img.y * l_grid.y,  0,
    //      l_img.x * l_grid.x,   l_img.y * l_grid.x,  0,
    //      0,                    0,                    0 ]
    // i.e. one row enforcing l_img × l_grid_homog = 0.

    // Three point correspondences from ppCorner (skip [0]).
    struct Pt {
        double x, y;     // image
        double col, row; // grid
    };
    std::array<Pt, 3> pts{
        Pt{qr.ppCorner[1].x, qr.ppCorner[1].y, 7.0, 0.0},
        Pt{qr.ppCorner[2].x, qr.ppCorner[2].y, 7.0, 7.0},
        Pt{qr.ppCorner[3].x, qr.ppCorner[3].y, 0.0, 7.0}};

    // Four direction-line correspondences. Each is given by two
    // grid-coord endpoints (giving the grid direction) and two
    // image-pixel corners (giving the image direction). The Java
    // code normalises both directions to unit length before SVD.
    struct Ln {
        double dxi, dyi;     // image direction (normalised)
        double dcol, drow;   // grid direction (normalised)
    };

    auto makeLine = [](double row0, double col0, double row1, double col1,
                       const cv::Point2d& c0, const cv::Point2d& c1) -> Ln {
        double dxi = c1.x - c0.x;
        double dyi = c1.y - c0.y;
        double dcol = col1 - col0;
        double drow = row1 - row0;
        double ni = std::sqrt(dxi * dxi + dyi * dyi);
        double ng = std::sqrt(dcol * dcol + drow * drow);
        if (ni > 0.0) { dxi /= ni; dyi /= ni; }
        if (ng > 0.0) { dcol /= ng; drow /= ng; }
        return Ln{dxi, dyi, dcol, drow};
    };

    std::array<Ln, 4> lns{
        makeLine(0, 7, 0, 14, qr.ppCorner[1], qr.ppRight[0]),
        makeLine(7, 7, 7, 14, qr.ppCorner[2], qr.ppRight[3]),
        makeLine(7, 7, 14, 7, qr.ppCorner[2], qr.ppDown[1]),
        makeLine(7, 0, 14, 0, qr.ppCorner[3], qr.ppDown[0])};

    // Design matrix: 2 rows per point + 1 row per line direction.
    // Java's `HomographyDirectLinearTransform` builds a 2*N+M × 9
    // system; we follow the same row layout.
    //
    // Direction of mapping: image (x, y) → grid (col, row) — i.e.
    // H * [x, y, 1]^T = lambda * [col, row, 1]^T. This matches the
    // image→grid convention everywhere else in the file.
    int32_t totalRows = static_cast<int32_t>(2 * pts.size() + lns.size());
    cv::Mat A = cv::Mat::zeros(totalRows, 9, CV_64F);
    int32_t r = 0;
    for (std::size_t i = 0; i < pts.size(); i++) {
        const Pt& p = pts[i];
        // Row 1: -x, -y, -1, 0, 0, 0, col*x, col*y, col
        A.at<double>(r, 0) = -p.x;
        A.at<double>(r, 1) = -p.y;
        A.at<double>(r, 2) = -1.0;
        A.at<double>(r, 6) = p.col * p.x;
        A.at<double>(r, 7) = p.col * p.y;
        A.at<double>(r, 8) = p.col;
        r++;
        // Row 2: 0, 0, 0, -x, -y, -1, row*x, row*y, row
        A.at<double>(r, 3) = -p.x;
        A.at<double>(r, 4) = -p.y;
        A.at<double>(r, 5) = -1.0;
        A.at<double>(r, 6) = p.row * p.x;
        A.at<double>(r, 7) = p.row * p.y;
        A.at<double>(r, 8) = p.row;
        r++;
    }
    for (std::size_t i = 0; i < lns.size(); i++) {
        const Ln& l = lns[i];
        // Direction-line constraint: image direction (dxi, dyi, 0)
        // maps to grid direction (dcol, drow, 0). Same row form as
        // a point but with the homogeneous w-component zero on both
        // sides — only the col*x, col*y and row*x, row*y entries
        // remain. Stack as one row enforcing the cross-product
        // collinearity.
        //
        // Per georegression's `addLineConstraint`:
        //   [ -dxi * drow,  -dyi * drow,  0,
        //      dxi * dcol,   dyi * dcol,  0,
        //      0,            0,            0 ]
        A.at<double>(r, 0) = -l.dxi * l.drow;
        A.at<double>(r, 1) = -l.dyi * l.drow;
        A.at<double>(r, 3) =  l.dxi * l.dcol;
        A.at<double>(r, 4) =  l.dyi * l.dcol;
        r++;
    }

    // Solve for the right null vector via SVD; smallest singular value's
    // right-singular vector is the homogeneous H solution.
    cv::Mat w, u, vt;
    cv::SVDecomp(A, w, u, vt, cv::SVD::FULL_UV);
    // vt is 9x9; last row is the smallest singular vector.
    cv::Mat h = vt.row(8).t();  // 9x1
    H = cv::Matx33d(h.at<double>(0, 0), h.at<double>(1, 0), h.at<double>(2, 0),
                    h.at<double>(3, 0), h.at<double>(4, 0), h.at<double>(5, 0),
                    h.at<double>(6, 0), h.at<double>(7, 0), h.at<double>(8, 0));
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
