// Port of boofcv.alg.fiducial.qrcode.QrCodePositionPatternGraphGenerator
// (BoofCV v1.3.0). Step 7c.
//
// BoofCV uses ddogleg's `NearestNeighbor` over `KdTreeSquareNode` for
// O(n log n) finder-pattern lookup. We replace that with a brute-force
// O(n²) loop over `KdTreeSquareNode::distance` — for QR-relevant
// inputs the candidate count is O(20) so the cost is negligible. The
// set of considered triplets is unchanged. See `finder_pattern.md`.

#ifndef BOOFCV_QR_FINDER_QR_CODE_POSITION_PATTERN_GRAPH_GENERATOR_HPP
#define BOOFCV_QR_FINDER_QR_CODE_POSITION_PATTERN_GRAPH_GENERATOR_HPP

#include "boofcv_qr/position_pattern_node.hpp"
#include "boofcv_qr/squares/square_graph.hpp"
#include "boofcv_qr/squares/square_node.hpp"

#include <opencv2/core.hpp>

#include <cstdint>
#include <vector>

namespace boofcv_qr {

// Pairs candidate finder patterns into "neighbour" edges on the
// underlying `SquareGraph`. Step 9 walks the graph to find triplets.
class QrCodePositionPatternGraphGenerator {
public:
    QrCodePositionPatternGraphGenerator() = default;
    explicit QrCodePositionPatternGraphGenerator(int32_t maxVersionQR)
        : maxVersionQR_(maxVersionQR) {}

    int32_t getMaxVersionQR() const { return maxVersionQR_; }
    void setMaxVersionQR(int32_t v) { maxVersionQR_ = v; }

    // Connects the supplied position patterns into a graph. Per
    // CLAUDE.md "Public API design" line 32, only the value-typed
    // overload is in the public surface; the pointer-list variant is
    // private since it imports BoofCV's raw-pointer DogArray idiom.
    //
    // The vector backs the pointers used internally by `SquareGraph`'s
    // edge bookkeeping — the caller must not invalidate it during
    // process() (e.g. by adding/removing entries). Reading is fine.
    void process(std::vector<PositionPatternNode>& positionPatterns);

    SquareGraph& getGraph() { return graph_; }
    const SquareGraph& getGraph() const { return graph_; }

    // ---- TEST-VISIBLE — Java JUnit reaches into these directly.
    void considerConnect(SquareNode* node0, SquareNode* node1);

private:
    // Pointer-list internal overload. Used by the public
    // `vector<PositionPatternNode>&` overload after wrapping each
    // entry in a pointer. Kept private per CLAUDE.md "Public API
    // design" — `vector<T*>` is a raw-pointer leak.
    void processPtrList(const std::vector<PositionPatternNode*>& positionPatterns);

    int32_t maxVersionQR_ = 40;  // QrCode::MAX_VERSION

    SquareGraph graph_;

    // Workspace mirroring Java's class-level fields.
    LineSegment2D lineA_{};
    LineSegment2D lineB_{};
    LineSegment2D connectLine_{};
    cv::Point2d intersection_{};
};

}  // namespace boofcv_qr

#endif  // BOOFCV_QR_FINDER_QR_CODE_POSITION_PATTERN_GRAPH_GENERATOR_HPP
