# PolylineSplitMerge — corner finder for closed contours

Companion to `boofcv_qr/polyline/polyline_split_merge.hpp`. Step 7b of the BoofCV port — the deepest single algorithm in the polygon stack at 907 LOC of original Java.

## Summary

Given an ordered list of integer-pixel contour points (typically produced by `cv::findContours` on the binarised image), `PolylineSplitMerge` fits a polyline of variable side count to the contour and returns the corner indices into that contour. For QR detection the output is a 4-corner polygon per dark-blob contour; the alignment-pattern stage and homography-based grid sampler downstream rely on the subpixel-quality corners that BoofCV's algorithm produces.

## Algorithm

The procedure is **grow-then-shrink**:

1. **Initial polyline.** For a closed contour (`loops=true`) build an initial triangle:
   - Pick a seed corner — the contour point farthest from `contour[0]` (`findCornerSeed`).
   - Optionally reject as non-convex right here (`isConvexUsingMaxDistantPoints`) if the user requested `convex=true`.
   - Pick the second corner: split the contour interval `[0, seed]` and `[seed, 0]` and choose whichever interval has the better `MaximumLineDistance` score.
   - Pick the third corner: the one maximising `|.|₁ + |.|₁` distance from the first two corners (`maximumDistance` — note the L¹-norm choice; Peter found L² maximised one side at the expense of the other for skinny shapes).
   - Reorder so the head's two neighbours are arranged in CCW order on the contour (`ensureTriangleOrder`).

   For an open polyline (`loops=false`), the head and tail are pinned to the first and last contour points — they never move. The interior corners are the only moving parts.

2. **Grow phase.** Repeatedly call `increaseNumberOfSidesByOne` until either `maxSides + extraConsider` is reached or no further side can be split. Each iteration:
   - For every existing corner `c`, the side error `c.sideError` is the average squared L²-distance from sample points along the contour interval to the chord connecting `c` to its successor (segment-clamped — see `Distance2D_F64.distanceLineSq`).
   - For every splittable corner, `c.splitError0` / `c.splitError1` are the side errors **after** an additional corner is inserted at `c.splitLocation` (chosen by the `SplitSelector` — for QR that's `MaximumLineDistance`, the contour point farthest from the chord).
   - `selectCornerToSplit` picks the corner whose split would change the sum-of-side-errors the most (greedy; an absolute change so big *increases* in error are also seized — see comment in source). The selected corner is split, two side errors replace one, and the polyline is "saved" if the new score is the best yet for that side count.
   - Score is `(sum of side errors) / numSides + cornerScorePenalty * numSides` — average error plus a per-corner regularisation. Without the penalty term the polyline with the largest side count would always win.

3. **Shrink phase.** After the grow loop terminates, repeatedly call `selectCornerToRemove` and remove the worst-cost-effective corner:
   - For each interior corner `target`, compute `before = (prev.sideError + target.sideError)/2 + cornerScorePenalty` (its current contribution) and `after = sideError(prev → next)` (what one side would cost if `target` were removed). The corner with the largest `before - after` is removed; the polyline is saved (per side count). When no removal helps any more, stop.

4. **Best polyline selection.** From the saved polylines for each side count `MIN_SIZE..maxSides`, pick the one with the smallest score that also clears the per-side `maxSideError` threshold. If the best fails the per-side threshold, return false (no detection at all). If `bestSize < minSides`, also fail.

5. **Per-side error gate.** After picking the best polyline, every side has to satisfy `sideError < maxSideError(length)²` where `length` is the chord length and `maxSideError` is the user-configured `ConfigLength` (defaults to 10 % of chord, minimum 3 pixels in BoofCV). Squared because side errors are squared distances.

## Why this approach over alternatives

- **vs. `cv::approxPolyDP` (Douglas–Peucker).** DP picks corners from the contour; PolylineSplitMerge does too. The difference is that DP greedily picks the farthest-point split at each level with no global score, no per-corner penalty, and no shrink phase. CLAUDE.md's *OpenCV substitution policy* explicitly forbids DP here because the QR detection-rate parity tests need PolylineSplitMerge's specific corner selection: removing the shrink phase loses ~2–3 percentage points on the harder dataset categories.
- **vs. RANSAC line fitting.** Robust to outliers but doesn't preserve the "exactly N corners on the contour" property the homography sampler needs. Would also be vastly slower per contour.
- **vs. Hough lines.** Operates on the raw contour, no need to rasterise into an accumulator. Faster, deterministic, no parameter tuning.

## Failure modes

- **Pathologically short contour.** `loops=true` with `contour.size() < 3` returns false immediately. `loops=false` requires `>= 2` points.
- **Degenerate triangle.** `findInitialTriangle` returns false if the convex test fails or `initializeScore` rejects a side as non-convex.
- **All sides too long.** If every side fails `canBeSplit` and the polyline has fewer than `minSides`, `process()` returns false.
- **Side error blow-up.** The per-side gate runs after the best polyline is picked — if any side's error exceeds the threshold, `bestPolyline` is reset to null and `process()` returns false.
- **`minimumSideLength` enforcement.** `setMinimumSideLength(0)` throws (mirrors Java). Default 10 px works for QR finder patterns ≥ ~30 px but will silently fail on tiny QRs — caller must lower it.

## Tunable parameters (with QR defaults)

| Parameter | Default | Effect |
|---|---|---|
| `loops` | `true` | Closed vs open polyline |
| `convex` | `false` | Reject concave shapes earlier |
| `maxSides` | `INT_MAX` | Cap on side count |
| `minSides` | `3` | Minimum for accepting a polyline |
| `minimumSideLength` | `10` | Minimum contour-distance per side |
| `cornerScorePenalty` | `0.25` | Per-corner score penalty (regularisation) |
| `thresholdSideSplitScore` | `0` | Side error below which a side is "perfect" |
| `maxNumberOfSideSamples` | `50` | Sub-sample cap on long sides for cost |
| `convexTest` | `2.5` | Contour-length-to-chord ratio above which the side is rejected as concave |
| `extraConsider` | `relative(1.0, 0)` | Look-ahead beyond `maxSides` (= max once over) |
| `maxSideError` | `relative(0.1, 3)` | Per-side error gate as ConfigLength of chord |

QR finder-pattern detection sets `loops=true`, `convex=true`, `maxSides=4`, `minSides=4`, `convexTest=2.5`, `cornerScorePenalty=0.25`, plus the per-image-size tuning of `minimumSideLength`. Different callsites in BoofCV adjust these knobs.

## Integration points for downstream recovery

`getPolylines()` exposes the `CandidatePolyline` array indexed by `(numSides - MIN_SIZE)`, so a known-prefix recovery path can read out *all* attempted side counts and pick a different polyline if the default best one fails downstream sanity checks. CLAUDE.md *Public API design* line 26 (strategy injection) is partly satisfied by `setSplitter(SplitSelector)` — the JUnit-only `SplitSelector` interface is exposed so consumers can override the split-point heuristic; `MaximumLineDistance` is the default.

## Cross-references

- Upstream BoofCV (pinned `v1.3.0`):
  - `boofcv-feature/src/main/java/boofcv/alg/shapes/polyline/splitmerge/PolylineSplitMerge.java`
  - `boofcv-feature/src/main/java/boofcv/alg/shapes/polyline/splitmerge/MaximumLineDistance.java`
  - `boofcv-feature/src/main/java/boofcv/alg/shapes/polyline/splitmerge/SplitSelector.java`
  - `boofcv-ip/src/main/java/boofcv/misc/CircularIndex.java` (functions inlined)
  - `georegression-0.28.2.jar` `Distance2D_F64.distanceSq(LineSegment2D_F64, x, y)` and `(LineParametric2D_F64, x, y)` (formulas inlined)
  - `georegression-0.28.2.jar` `UtilPolygons2D_I32.isPositiveZ` (inlined)
- Tests: `tests/unit/test_polyline_split_merge.cpp` (mirrors `TestPolylineSplitMerge.java` and `TestMaximumLineDistance.java`).
- Used by: step 7b's `DetectPolygonFromContour` (next file).

## What changed vs Java

- **`DogLinkedList<Corner>`** → a small in-house `CornerList` wrapping `std::list<Corner*>`. Iterators provide pointer-stable element handles; `Element<Corner>` becomes `CornerList::Iter`. The Java field-access style `e.object`/`e.next`/`e.prev` becomes the helper methods `objectAt(it)`, `iterNext(it)`, `iterPrev(it)` to keep the algorithmic core readable. The wrap-around `next()`/`previous()` methods are unchanged.
- **`DogArray<Corner>`** → a small `CornerPool` that owns `std::unique_ptr<Corner>`. Stable identities across pool resizes. Marked `// TODO(perf): recycle` to revisit.
- **`DogArray<CandidatePolyline>`** → `std::vector<CandidatePolyline>` (value-typed; matches Java's `DogArray<CandidatePolyline>` which stores values, not pointers). Public surface is `const std::vector<CandidatePolyline>&` for `getPolylines()` and `std::optional<CandidatePolyline>` by value for `getBestPolyline()` per CLAUDE.md "Public API design".
- **`ConfigLength`** is ported to a tiny local struct (`ConfigLength`) with `compute(double)` and `computeI(double)`. The full BoofCV `ConfigLength` lives in a different module we don't pull in.
- **Inlined geometric helpers**: `lineParametricDistanceSq`, `lineSegmentDistanceSq`, `isPositiveZ`, `circularIndexDistanceP`, `circularIndexPlusPOffset`, `circularIndexMinusPOffset`. Each cites its georegression / boofcv-ip origin and is byte-for-byte the same formula.
- **`SplitSelector`** is a small abstract base with `MaximumLineDistance` as the only concrete implementation (matches BoofCV's only QR-relevant subclass).
- **Exceptions**: BoofCV throws `RuntimeException("Egads")` and `RuntimeException("Should be impossible")` in unreachable branches. We throw `std::runtime_error` for parity — these are programmer errors, not control flow.
