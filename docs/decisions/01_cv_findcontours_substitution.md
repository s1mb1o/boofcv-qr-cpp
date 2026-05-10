# ADR 01: Use `cv::findContours` instead of porting `LinearContourLabelChang2004`

**Date:** 2026-05-10
**Status:** Accepted
**Deciders:** Project at start (CLAUDE.md "OpenCV substitution policy"); empirical-cost data captured at step-9b close-out.

---

## Context

BoofCV's QR detection pipeline relies on
`boofcv.alg.filter.binary.impl.LinearContourLabelChang2004` (~600 LOC
algorithm) to extract external + internal contours from a binary
image. The contours are then consumed by:

1. `DetectPolygonFromContour` (polygon-fitting via the polyline corner
   finder),
2. `ContourEdgeIntensity` (per-polygon edge-intensity samples used to
   derive the `grayThreshold` for the QR finder check),
3. The 1:1:3:1:1 raster-scan `checkPositionPatternAppearance` step in
   `QrCodePositionPatternDetector`.

CLAUDE.md's `Stack` and `OpenCV substitution policy` sections mandate
using OpenCV for algorithmically-equivalent functionality and
explicitly list contour extraction:

> **Replace with OpenCV** (do NOT port from Java):
> ...
> Contour extraction from a binary image (`cv::findContours` with
> `CHAIN_APPROX_NONE`). Pick the retrieval mode to **match BoofCV's
> `LinearContourLabelChang2004` configuration** — most stages need
> both external and internal contours plus the parent/child hierarchy
> because finder patterns are nested ... Use `RETR_CCOMP` or
> `RETR_TREE` and consume the hierarchy as BoofCV does.

The choice was made at project start. This ADR captures the reasoning
+ the empirical cost discovered at 9b close-out.

## Decision

**Keep `cv::findContours(RETR_CCOMP, CHAIN_APPROX_NONE)`. Do NOT port
`LinearContourLabelChang2004`.** Acknowledge a documented residual
parity cost on borderline images.

## Mechanism of divergence

OpenCV's `findContours` and BoofCV's `LinearContourLabelChang2004` both
emit Moore-neighbor 8-connected contours and both find the same blobs
(boundary disagreement is far below the binarisation noise — measured
~0.6%–1.9% per-pixel diff between Java and C++ binarised images, and
the contour count agrees within ~10–80% across 562 images). But:

- OpenCV emits external contours **CCW-in-image** (CW-in-math); BoofCV
  emits **CW-in-image** (CCW-in-math). We reverse OpenCV's output in
  `buildContoursFromOpenCV` to match BoofCV's winding (step 7b/2 fix-
  up `770f210`).
- After reversal, the **starting pixel** of OpenCV's contour is
  rotated to the topmost-row's leftmost pixel to match BoofCV's
  raster-scan order (step 7b/2 fix-up #2).
- Even after both fixes, the **per-pixel sequence** along the boundary
  differs. OpenCV and BoofCV use slightly different inner-loop
  orderings around diagonal moves. Two pixels that share an edge in
  the binary image can be visited in either of two orders depending
  on which extractor is in use. The net effect is that two
  pixel-sequence-equivalent contours can differ by ~10s of pixels in
  *which* pixel is at index `k` of the contour array.

For most downstream consumers the difference is invisible (polygon
corners agree to within 1 px between Java and C++ on >99% of contours
in the test set). For two specific downstream operations the
difference is amplified into a parity-affecting outcome:

1. **`ContourEdgeIntensity`** averages 30 tangent-direction probe
   samples along the contour. The 30 starting positions are
   `step = contour.size() / 30` apart; if the contour has even slightly
   different point ordering, those 30 probes land on 30 slightly
   different pixels. The resulting `(edgeInside + edgeOutside) / 2`
   `grayThreshold` differs from Java's by ~0–7 grayvalue units
   depending on how much variance there is along the boundary.
2. **`PolylineSplitMerge`** corner finder works directly on the contour
   pixel sequence to fit a polygon by recursive split-merge. Its
   convex check, `maxSideError` filter, and `cornerScorePenalty` filter
   are all sensitive to the per-pixel ordering. For tiny contours
   (perimeter near the `minimumContour` floor), the difference can
   tip the corner finder from "fit a 4-corner polygon" to "rejected".

## Empirical cost (measured at step-9b close-out, dataset
`pricetag-vision-datasets/data/external/boofcv-qrcodes`)

5 borderline images out of 562 fail in C++ but succeed in Java:

| category   | image          | failure stage    | per-image cost |
|------------|----------------|------------------|---------------:|
| `monitor`  | image011       | finder check     |  -5.88pp local |
| `monitor`  | image012       | finder check     |  -5.88pp local |
| `glare`    | image005       | polygon fit      |  -1.89pp local |
| `glare`    | image007 (1/2) | polygon fit      |  -1.89pp local |
| `glare`    | image022       | polygon fit      |  -1.89pp local |

Aggregate impact: ~0.4pp out of 562 GT × 1258 GT-instance-weight.

Per-category impact:
- `monitor`: -11.76pp (2 images out of 17 GT).
- `glare`: -3.77pp (3 GT out of 53).

11 of the 17 dataset categories are within ±2pp of Java baseline.
The remaining 4 residuals (besides `monitor` and `glare`) are
small-N "+" outliers — denominator-noise on small categories where
±1 image swings 2-4pp; see the algorithm doc's "Known parity
residuals" section. Aggregate parity sits at -0.08pp from Java's
74.40%.

## Mechanism of failure on the 5 images

**`monitor/image011` and `image012` (finder-check stage).**

The QR's bottom-left finder polygon is detected by both Java and C++
(corners agree to within 1 px). Java's `ContourEdgeIntensity` reports
`edgeInside=56.9, edgeOutside=164.9` → `grayThreshold = 110.9`. C++
reports `edgeInside=20.1, edgeOutside=187.7` → `grayThreshold = 103.9`.
The 7-grayvalue difference is enough to flip the `1:1:3:1:1`
centerline raster-scan check that decides whether the polygon is a
finder pattern — Java's threshold places the dark/light boundaries
correctly, ours doesn't. The finder polygon is rejected; with only 2
of 3 expected finders, the graph generator fails to form a triplet
and the orchestrator returns 0 detections + 0 failures.

**`glare/image005`, `image007`, `image022` (polygon-fit stage).**

The QR is small (~70 px wide; each finder ~5–10 px). Tested OpenCV's
`findContours` directly on the same binarised image: 14 candidate
contours within 15 px of the missing finder's location, the largest
only 43 pixels of perimeter — exactly at the `minimumContour =
ConfigLength::fixed(40)` floor. Our `PolylineSplitMerge` rejects the
contour as a 4-corner polygon (likely fails the convex check or
`maxSideError` filter on the tiny boundary), where Java's
`PolylineSplitMerge` running on the equivalent BoofCV-emitted contour
accepts it. The finder polygon never enters the candidate list at all.

## Alternatives considered

### (W) Port `LinearContourLabelChang2004`

- Cost: ~600 LOC algorithmic port + JUnit-equivalent tests + algorithm
  doc per CLAUDE.md "Algorithm documentation requirement". Not
  trivial — `LinearContourLabelChang2004` is a non-recursive
  contour-tracing algorithm with a non-obvious labeling phase.
- Benefit: ~0.4pp aggregate gain; recovers 5 borderline images.
- **Rejected**. Direct violation of CLAUDE.md "Replace with OpenCV"
  policy. The substitution was a deliberate architectural choice at
  project start; reverting it for 5 images is a poor cost/benefit
  trade-off when aggregate parity is already at -0.08pp.

### (X) Substitute the `ThresholdBlockOtsu`-derived per-block threshold for the edge-intensity-derived `grayThreshold` in `checkPositionPatternAppearance`

- Cost: ~50 LOC refactor of `QrCodePositionPatternDetector` to consume
  the binarizer's threshold map.
- Benefit: would clear `monitor` (the finder-check-stage failures);
  uncertain effect on `glare` (different mechanism); risks regressing
  other categories that depend on the edge-intensity threshold being
  finer-grained than the binarizer's per-block threshold.
- **Rejected**. CLAUDE.md mandates "Verbatim where algorithmic" for
  finder-pattern detection. The edge-intensity-derived threshold is
  Java's chosen mechanism for this check; substituting it is an
  algorithmic deviation, not just a different implementation. The
  per-block binarizer threshold has fundamentally different statistical
  properties (per-tile Otsu summary vs per-contour edge sample) and
  could regress the `nominal` / `rotations` / `lots` categories where
  edge-intensity threshold currently works correctly.

### (Y) Accept the residual; document. **CHOSEN.**

- Cost: docs only.
- Benefit: 9b complete with parity at -0.08pp aggregate.
- Consistent with CLAUDE.md substitution policy (W) and verbatim-port
  rule (X).

## Consequences

**Positive:**

- Aggregate parity at -0.08pp from Java baseline (target was ±1pp).
- 11 of 17 categories strictly within ±2pp band.
- The cv::findContours substitution stays consistent with the rest
  of the project's OpenCV-substitution decisions
  (`cv::getPerspectiveTransform`, `cv::findHomography(method=0)`,
  `cv::imread`, etc.).
- The port is ~600 LOC smaller than it would be with a verbatim
  contour extractor.

**Negative:**

- 5 borderline images fail to detect where Java succeeds: `monitor/
  image011`, `monitor/image012`, `glare/image005`, `glare/image007`
  (1 of 2 GT), `glare/image022`.
- Per-category impact: `monitor` -11.76pp (large local delta on a
  small-N category), `glare` -3.77pp.
- Future engineers debugging "why does Java decode this image and we
  don't" need to know about this trade-off and not chase the symptom.
  This ADR is the durable record.

## When to revisit

Reopen this decision if any of the following are true:

1. **Downstream consumer's input distribution shifts toward the
   borderline cases.** E.g. pricetag-vision encounters a shelf-photo
   scenario where reflective glare on plastic price tags makes the
   `glare`-style failure mode dominant. At that point, the cost of
   porting `LinearContourLabelChang2004` (one-time) is amortised
   against decode-rate improvement on a real distribution.
2. **A simpler fix appears.** For example, if a future OpenCV release
   adds a `findContours` mode that matches BoofCV's pixel ordering
   exactly, we can switch to it without porting.
3. **Compositional regressions on the existing in-band categories.**
   If a future change drops more than ~1pp on any currently-in-band
   category and the residual is traceable to the contour-encoding
   divergence, the cumulative cost may justify the port.

## References

- CLAUDE.md "Stack" and "OpenCV substitution policy" sections.
- `src/decoder/qr_code_decoder_image.md` "Known parity residuals"
  section (per-image breakdown).
- ChangeLog entries for cycles 9b.3 (a), (b), (c), and (d).
- Upstream `boofcv.alg.filter.binary.impl.LinearContourLabelChang2004`
  in 3rdparty/BoofCV (pinned tag in `UPSTREAM_VERSION`).
- OpenCV's `cv::findContours` documentation:
  `cv::CHAIN_APPROX_NONE`, `cv::RETR_CCOMP`.
