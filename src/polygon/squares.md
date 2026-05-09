# Square graph utilities — `SquareNode`, `SquareEdge`, `SquareGraph`, `PositionPatternNode`

Companion to `boofcv_qr/squares/{square_node,square_edge,square_graph}.hpp` + `boofcv_qr/position_pattern_node.hpp`. Step 7a — the structural pieces of the finder-pattern detector.

## Summary

A QR finder pattern looks like three nested squares: 7×7 black, 5×5 white, 3×3 black. After binarisation we extract the dark blobs as quadrilateral polygons, then need a graph data structure to talk about which squares are adjacent (i.e. which polygons could be neighbouring finder patterns from the same QR). `SquareNode` is one polygon, `SquareEdge` is a side-to-side connection, `SquareGraph` provides graph operations (computeNodeInfo, connect, detachEdge, almostParallel, acuteAngle, findSideIntersect).

`PositionPatternNode` extends `SquareNode` with a `grayThreshold` field, used by the QR finder-pattern detector to remember the local binarisation threshold so the bit-sampling stage downstream can re-use it.

## Why these are small

These classes are straight data plumbing on top of 2-D geometry helpers. The interesting work happens in the polygon-from-contour stack (step 7b) and the finder-pattern graph generator (step 7c) which use these types. Worth keeping the structural code clean and well-tested before piling more logic on top.

## What changed vs Java

- **`SquareNode::edges`** is `std::array<SquareEdge*, 4>` — non-owning pointers. Edge objects are owned by `SquareGraph::declaredEdges` (a `vector<unique_ptr<SquareEdge>>`); `SquareGraph::unused` is a queue of recycled-but-still-valid `SquareEdge*`. This mirrors BoofCV's recycling-via-DogArray pattern but uses `std::vector<std::unique_ptr<>>` for ownership clarity.
- **`SquareGraph::connect()` is public** rather than package-private. The Java JUnit test calls it directly across the package boundary; we don't have package-private, so this is the cleanest way to keep parity-test fidelity.
- **Inlined geometry helpers** (`lineLineIntersection`, `segmentSegmentIntersection`, `vectorAcute`, `circularIndexAdd`, `angleDist`) instead of pulling in BoofCV's `georegression` module + `org.ejml`. Each is < 20 lines; pulling the modules would balloon the dependency tree.
- **`KdTreeSquareNode`** inner class **not ported**. Used only by `ddogleg`'s KD-tree-based clustering paths that QR doesn't exercise.

## Failure modes

- **`SquareNode::distanceSqCorner`** assumes `square.size() == 4`. Empty `square` will out-of-bound. Caller's responsibility (BoofCV has the same contract).
- **`SquareGraph::computeNodeInfo`** throws `std::runtime_error("BAD")` if the four corners are collinear (line-line intersection between opposite-corner pairs is undefined). Mirrors Java.
- **Edge ownership**: `SquareEdge*` returned by `getUnusedEdge()` is owned by `declaredEdges`. Don't `delete` it. `detachEdge` only resets and re-queues — it never frees.
- **Reset semantics**: `SquareGraph::reset()` resets every declared edge and re-queues all of them as unused; downstream code that holds raw `SquareEdge*` post-reset gets garbage. Consumers should call `reset()` only between detection runs.

## Cross-references

- Upstream BoofCV files (pinned `v1.3.0`):
  - `boofcv-recognition/src/main/java/boofcv/alg/fiducial/calib/squares/SquareNode.java`
  - `boofcv-recognition/src/main/java/boofcv/alg/fiducial/calib/squares/SquareEdge.java`
  - `boofcv-recognition/src/main/java/boofcv/alg/fiducial/calib/squares/SquareGraph.java`
  - `boofcv-recognition/src/main/java/boofcv/alg/fiducial/qrcode/PositionPatternNode.java`
- Tests: `tests/unit/test_square_node.cpp` (mirrors `TestSquareNode.java` + `TestSquareEdge.java`), `tests/unit/test_square_graph.cpp` (mirrors `TestSquareGraph.java`).
- Used by: step 7c (finder-pattern detector wires polygons through this graph to identify finder triplets).
