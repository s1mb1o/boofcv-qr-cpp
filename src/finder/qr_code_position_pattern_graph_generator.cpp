// Port of boofcv.alg.fiducial.qrcode.QrCodePositionPatternGraphGenerator
// (BoofCV v1.3.0). Verbatim per CLAUDE.md "Verbatim vs idiomize".
//
// Deviation: BoofCV uses ddogleg's `NearestNeighbor` over `KdTreeSquareNode`
// for asymptotic O(n log n). We use brute-force O(n²) — for QR-relevant
// inputs the candidate count is O(20). To make the C++ traversal
// deterministic on exact-distance ties (which can otherwise flip
// `SquareGraph::checkConnect`'s best-edge-wins decision since it keeps
// the FIRST equal-score edge), we sort candidates by squared distance
// ascending before considerConnect. See src/finder/finder_pattern.md.

#include "boofcv_qr/finder/qr_code_position_pattern_graph_generator.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace boofcv_qr {

namespace {

double pointDistance(const cv::Point2d& a, const cv::Point2d& b) {
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

double lineSegmentLength(const LineSegment2D& s) {
    return pointDistance(s.a, s.b);
}

}  // namespace

void QrCodePositionPatternGraphGenerator::processPtrList(
    const std::vector<PositionPatternNode*>& positionPatterns) {
    // Reset the graph and compute node information
    graph_.reset();

    for (std::size_t i = 0; i < positionPatterns.size(); i++) {
        PositionPatternNode* f = positionPatterns[i];

        // The QR code version specifies the number of "modules"/blocks across the marker is
        // A position pattern is 7 blocks. A version 1 qr code is 21 blocks. Each version past one increments
        // by 4 blocks. The search is relative to the center of each position pattern, hence the - 7
        double maximumQrCodeWidth =
            f->largestSide * (17.0 + 4.0 * maxVersionQR_ - 7.0) / 7.0;
        double searchRadius = 1.2 * maximumQrCodeWidth;  // search 1/2 the width + some fudge factor
        double searchRadiusSq = searchRadius * searchRadius;

        // Connect all the finder patterns which are near by each other together in a graph.
        //
        // Substitution per the algorithm doc: brute-force O(n²) over
        // `KdTreeSquareNode::distance` (the same squared-centre metric
        // BoofCV's KdTree uses). For QR-relevant inputs n ≈ 20.
        //
        // To make the C++ traversal deterministic on exact-distance
        // ties — `SquareGraph::checkConnect` keeps the FIRST equal-
        // score edge, so different orders give different graphs on
        // floating-point ties — collect (distSq, candidate) pairs and
        // sort ascending by distSq before invoking considerConnect.
        // Java's KdTree does not guarantee distance-sorted output, so
        // on a tie our graph and Java's may differ; in practice ties
        // are unmeasurable on real images.
        std::vector<std::pair<double, PositionPatternNode*>> neighbours;
        neighbours.reserve(positionPatterns.size());
        for (std::size_t j = 0; j < positionPatterns.size(); j++) {
            PositionPatternNode* candidate = positionPatterns[j];
            if (candidate == f) continue;  // skip over if it's the square that initiated the search

            double dSq = KdTreeSquareNode::distance(f, candidate);
            if (dSq > searchRadiusSq) continue;

            neighbours.emplace_back(dSq, candidate);
        }
        std::sort(neighbours.begin(), neighbours.end(),
                   [](const std::pair<double, PositionPatternNode*>& a,
                      const std::pair<double, PositionPatternNode*>& b) {
                       return a.first < b.first;
                   });
        for (const auto& pr : neighbours) {
            considerConnect(f, pr.second);
        }
    }
}

void QrCodePositionPatternGraphGenerator::process(
    std::vector<PositionPatternNode>& positionPatterns) {
    std::vector<PositionPatternNode*> ptrs;
    ptrs.reserve(positionPatterns.size());
    for (auto& pp : positionPatterns) ptrs.push_back(&pp);
    processPtrList(ptrs);
}

void QrCodePositionPatternGraphGenerator::considerConnect(SquareNode* node0,
                                                            SquareNode* node1) {
    // Find the side on each line which intersects the line connecting the two centers
    lineA_.a = node0->center;
    lineA_.b = node1->center;

    int32_t intersection0 =
        graph_.findSideIntersect(*node0, lineA_, intersection_, lineB_);
    connectLine_.a = intersection_;
    int32_t intersection1 =
        graph_.findSideIntersect(*node1, lineA_, intersection_, lineB_);
    connectLine_.b = intersection_;

    if (intersection1 < 0 || intersection0 < 0) {
        return;
    }

    double side0 = node0->sideLengths[static_cast<std::size_t>(intersection0)];
    double side1 = node1->sideLengths[static_cast<std::size_t>(intersection1)];

    // it should intersect about in the middle of the line

    double sideLoc0 = pointDistance(connectLine_.a,
                                      node0->square[static_cast<std::size_t>(intersection0)]) /
                      side0;
    double sideLoc1 = pointDistance(connectLine_.b,
                                      node1->square[static_cast<std::size_t>(intersection1)]) /
                      side1;

    if (std::fabs(sideLoc0 - 0.5) > 0.35 || std::fabs(sideLoc1 - 0.5) > 0.35)
        return;

    // distance measure for how far away from the center it is. 0 is best
    double sideCenterDistance =
        (std::fabs(sideLoc0 - 0.5) + std::fabs(sideLoc1 - 0.5)) / 2.0;

    // see if connecting sides are of similar size
    if (std::fabs(side0 - side1) / std::max(side0, side1) > 0.25) {
        return;
    }

    // Checks to see if the two sides selected above are closest to being parallel to each other.
    // Perspective distortion will make the lines not parallel, but will still have a smaller
    // acute angle than the adjacent sides
    if (!graph_.almostParallel(*node0, intersection0, *node1, intersection1)) {
        return;
    }

    double ratio = std::max(node0->smallestSide / node1->largestSide,
                              node1->smallestSide / node0->largestSide);

    //		System.out.println("ratio "+ratio);
    if (ratio > 1.3)
        return;

    double angle = graph_.acuteAngle(*node0, intersection0, *node1, intersection1);
    double score = lineSegmentLength(lineA_) * (1.0 + angle + sideCenterDistance / 2);
    // this score function is a little brittle.

    //		System.out.println("  length: "+lineA.getLength()+" angle="+angle);

    graph_.checkConnect(node0, intersection0, node1, intersection1, score);
}

}  // namespace boofcv_qr
