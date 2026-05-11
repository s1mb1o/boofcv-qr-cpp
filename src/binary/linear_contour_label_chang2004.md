# LinearContourLabelChang2004 — single-pass contour extractor + component labeller

## One-paragraph summary

Takes a binary image (`CV_8UC1`, 0/1 convention — foreground = dark module) and produces two things in a single scan-line pass:

1. A **label image** (`CV_32SC1`) assigning each foreground pixel an integer blob id (1..N), with background pixels set to 0.
2. A list of **contours** per blob — one external boundary, plus zero-or-more internal-hole boundaries — each stored as an ordered sequence of (x,y) points in a `PackedSetsPoint2D_I32`.

The algorithm is from Fu Chang, Chun-jen Chen, Chi-jen Lu, "A linear-time component-labeling algorithm using contour tracing technique", *Computer Vision and Image Understanding*, 2004. Each pixel is visited at most a constant number of times — once during the raster scan, plus once each time a contour passes through it — so the runtime is linear in the image area.

## Algorithm description

### Two cooperating pieces

- **`LinearContourLabelChang2004` (this file).** Walks the binary image scan-line by scan-line, deciding at each foreground pixel whether to (1) start tracing a new external contour, (2) trace an internal hole contour, or (3) just propagate the left neighbour's label. This is the *driver*.
- **`ContourTracer` (sibling file).** A Moore-neighbor 8-connected (or 4-connected) boundary follower. Given a seed pixel and an initial direction, it walks clockwise around the blob boundary until it returns to the start with the same direction, writing the blob's label into the label image and recording each boundary pixel into the packed-sets store.

`ContourTracer` caches two parallel direction tables: linear pixel-index offsets for binary/labeled buffers, and `(dx, dy)` coordinate offsets for the same direction indices. The Java code recomputes `(x, y)` from the linear pixel index after each move; the C++ port updates `(x, y)` directly from the direction table so the hot contour-walk path avoids a division and modulo per boundary pixel. This is a representation-only optimisation: the direction order, seed directions, index updates, emitted contour points, and label writes are unchanged.

### Border padding

Before scanning, the driver copies the input into a 1-pixel-zero-bordered buffer (`border = cv::Mat(H+2, W+2, CV_8UC1)`). This eliminates all bounds-checking inside the tracer's inner loop — neighbour lookups by precomputed pixel-index offsets simply walk into the zero border and immediately fail the `data[idx] == 1` test. Per the BoofCV comment this is ~25% faster than guarded access.

Coordinates inside the tracer are in *border* coordinates (1..W, 1..H). The tracer's `add()` method subtracts (1, 1) when writing a point so the externally visible coordinates are back in input-image space.

### The 3-step driver

For each row `y` from 1 to H, the driver scans left to right looking for foreground pixels. At each foreground pixel it asks three questions in order:

- **Step 1 — new external contour?** The pixel is unlabeled AND the pixel directly above is not a `1` (either it's background `0`, OR it's already been *visited and marked* `0xFF` by an earlier external trace). If so, this is the topmost-leftmost pixel of a new blob — increment the contour count and start an external trace, with the tracer seeded at initial direction 7 (=NE for 8-conn) so the first clockwise search finds the contour going down-right.
- **Step 2 — internal hole contour?** The pixel directly below is `0` (genuine background, not a mark — i.e. there's a hole opening below). If so, start an internal trace, seeded at direction 3 (=SW for 8-conn) so the first clockwise search goes around the hole.
- **Step 3 — propagate label?** Neither of the above. The current pixel is inside an already-labeled blob; if it doesn't already carry the label, copy the left neighbour's label into it.

Note that step 1 and step 2 are *not* mutually exclusive — a pixel can simultaneously be the top of a new blob AND the top of an interior hole. The driver handles both.

### Why the `0xFF` sentinel in the binary image

`ContourTracer::checkOne` writes `0xFF` (Java: `-1`, same bit pattern) into pixels it has examined and rejected during a clockwise neighbour search. This serves two purposes:

1. **Pruning the next scan.** The driver's step-1 check (`binaryData[indexIn - binaryStride] != 1`) succeeds for `0xFF` just as it does for `0` — so once a contour has been traced, scanning will not redundantly start a fresh contour from any of its already-explored boundary pixels.
2. **Pruning future searches.** When a tracer revisits the same area (or another contour passes nearby), `checkOne` short-circuits on `0xFF` cells. Without the mark, the tracer would re-test the same pixels repeatedly and the linear-time guarantee would break.

Crucially, **the mark is only on pixels examined-and-rejected**, not on pixels that turned out to be foreground (those keep value `1`). The equality test `data[idx] == 1` is bit-identical between signed Java `-1` and unsigned C++ `0xFF`; we never do arithmetic or signed comparisons on the marker. See "Failure modes and known limits" for why this matters.

### Output structure

- `contours` (vector of `ContourPacked`): one entry per labeled blob. Each entry stores `id` (the label, 1..N), `externalIndex` (index into `packedPoints` for the external boundary), and `internalIndexes` (a list of indices for any holes).
- `packedPoints` (a `PackedSetsPoint2D_I32`): block-allocated `int[]` buffers storing (x,y) pairs. Each "set" is one contour. The order of sets in `packedPoints` is *not* per-blob — internal contours of blob A may sit between blob B's external and blob C's external. Callers must walk `contours[i].externalIndex` and `contours[i].internalIndexes[j]` to find the right sets.
- **Contour discard.** If a contour exceeds `maxContourLengthPixels` OR falls below `minContourLengthPixels`, its set is replaced with an empty set (`removeTail()` then `grow()` again). The contour's *existence* is still recorded in the `contours` list — only the points are dropped. This matters for the driver's step-2 dispatch, which needs the blob to still be present in the labeled image even when its boundary is too long to record.

The packed point store preserves allocated blocks across `reset()` calls using a logical active-block count. This follows Java's reusable-array intent while avoiding per-frame block shrink/reallocation on images with many or long contours. `appendSetTo()` materialises one contour into a caller vector block-by-block; it emits the same `(x,y)` sequence as `SetIterator`, but avoids per-point division/modulo in the polygon-detector bridge.

### Why CW external / CCW internal

Java's seed directions (external=7, internal=3 for 8-conn) produce specific windings:

- External: starts at top-left of the blob, first search goes E/NE, contour winds **clockwise in image coords** (which is CCW in math, since image y points down).
- Internal: starts at top of the hole, first search goes W/SW, contour winds **counter-clockwise in image coords**.

This is the convention `PolylineSplitMerge` was designed for. The C++ port preserves it pixel-for-pixel — no winding flip, no start-pixel rotation. Closing ADR 01's per-pixel-sequence divergence is the entire reason for this port.

## Why this approach over alternatives

### vs. `LinearExternalContours` (the actual upstream Java wiring)

A subtle disclosure caught at cycle B review: BoofCV's `FactoryShapeDetector.polygonContour()` wires `BinaryContourFinderLinearExternal` around `boofcv.alg.filter.binary.LinearExternalContours(ConnectRule.FOUR)`, **not** `LinearContourLabelChang2004` directly. Cycle A's JUnit suite proved parity against `TestLinearContourLabelChang2004.java`; the actual QR-path upstream is a sister algorithm.

`LinearExternalContours` is class-level-documented in upstream as "follows a similar pattern to other finding/tracing algorithms from [Chang 2004]" — algorithmically related, external-only, with its own `0xFF`/`-2` sentinel scheme. The C++ port substitutes the full `LinearContourLabelChang2004` labeller and configures `setSaveInternalContours(false)` for the QR finder-pattern path (matching the external-only behaviour).

Empirical justification: cycle B's dataset regression on `qrcodes_v3` (562 images / 1258 GT) is **+0.00pp byte-identical to Java baseline** across every category. Substituting `LinearExternalContours` for `LinearContourLabelChang2004` would add ~250 LOC of additional algorithmic port for no measured parity gain. See `docs/decisions/05_perf_linear_contour_label_chang2004_port.md` for the full justification chain.

If a future cycle observes a parity regression that traces to internal contour handling, that's the signal to port `LinearExternalContours` separately.

### Connect rule — EIGHT (port) vs FOUR (upstream Java)

The polygon detector wires this stage with `ConnectRule::EIGHT`. Upstream Java's `FactoryShapeDetector.polygonContour` wires `LinearExternalContours(ConnectRule.FOUR)`. This is a deliberate deviation.

Cycle C measured the FOUR-connect alternative on the full `qrcodes_v3` regression set: aggregate -0.79pp from Java baseline, with `close` and `damaged` falling out of the ±2pp band (close -2.50pp, damaged +2.33pp) and four accepted-residual categories drifting past their bands (glare -9.43pp vs -3.77, monitor -5.88 vs -11.76, noncompliant -3.85 vs +3.85, high_version -2.70 vs +2.70). EIGHT happens to match the locked Java baseline on this dataset better than FOUR; the `cv::findContours` pre-cycle-B path was 8-connected, so the locked baseline numbers were captured against 8-connectivity end-to-end.

This is documented in ADR 05 "Connect rule choice"; revisiting FOUR is conditional on future binarizer or downstream changes making the relative comparison different.

### vs. `cv::findContours(RETR_CCOMP, CHAIN_APPROX_NONE)`

OpenCV's `findContours` is the obvious substitute and is what BoofCV C++'s sister-port projects use. We started there. Empirically (see ADR 01) it produces:

- The same **set** of contours as Chang 2004 (~99% blob-count agreement).
- **Opposite winding** (external CCW in image vs CW). Reversable.
- **Different start pixel** (somewhere on the boundary, not the top-leftmost). Rotation-fixable.
- After both fixes: **different per-pixel ordering** along the boundary. NOT fixable with a wrapper — OpenCV and BoofCV use different inner-loop orderings around diagonal moves, and the resulting contours differ by ~10s of pixels in *which* pixel is at index `k`.

The per-pixel-ordering divergence is invisible to most consumers but propagates into two stages:

- **`ContourEdgeIntensity`** picks 30 sample positions `step = contour.size() / 30` apart; different ordering → different sample positions → different grayThreshold (±7 grayvalue units on borderline images).
- **`PolylineSplitMerge`** walks the contour pixel-by-pixel to fit corners; per-pixel ordering matters for the convex check, side-error filter, and corner-score-penalty filter, especially on small contours near the `minimumContour=40` floor.

Aggregate cost on `qrcodes_v3`: ~0.4pp (5 borderline images on 562 total). Per-category: `monitor` -11.76pp, `glare` -3.77pp.

### Contour-tracer micro-optimisation

A 2026-05-11 profile pass after the `LinearContourLabelChang2004` port found `ContourTracer::searchOne8()` as the top C++ self-time function on noisy high-resolution images (`bright_spots/image012`: 35.6% of samples) and still a leading function on multi-QR images (`lots/image005`: 14.0%). The first safe optimisation keeps the algorithm identical but removes two avoidable arithmetic costs in that hot path:

- Direction wrap in the unrolled 4- and 8-neighbour search uses bit masks (`& 3`, `& 7`) instead of `% 4` / `% 8`. `dir` is always in `[0, ruleN)`, so this is exactly equivalent.
- `moveToNext()` updates `(x, y)` from precomputed coordinate deltas instead of recomputing them from the linear pixel index with division/modulo. Linear indices are still updated from the original stride-dependent offset tables, so label and binary writes hit the same pixels.

Because this is algorithmic core, any future changes in this area must keep the JUnit-mirror contour tests and full `qrcodes_v3` regression green before commit.

### Packed-set materialisation micro-optimisation

A later 2026-05-11 profile pass showed remaining contour-stage allocation and packed-set copy cost after the tracer arithmetic cleanup. The safe representation change keeps every emitted contour point and contour header identical:

- `PackedSetsPoint2D_I32::reset()` keeps allocated block buffers and resets only logical state.
- `grow()` / `removeTail()` / `addPointToTail()` reuse those inactive blocks before allocating new ones.
- `DetectPolygonFromContour::buildContoursFromPort()` calls `appendSetTo()` so external and internal contours are copied in block spans instead of through the generic iterator.

This is intentionally narrower than full `Contour` object reuse. A broader slot-reuse trial improved `bright_spots` but regressed `lots`, so it was not committed.

### vs. classic flood-fill + boundary trace as separate passes

Chang 2004 does both in a single raster scan, in linear time. The classic two-pass approach (`cv::connectedComponents` then boundary-walk per blob) is also linear-time but does strictly more work — every pixel is visited twice, and the boundary walk has to re-discover the topology that the scan-line driver already computed.

### vs. recursive flood-fill

`O(N)` worst-case stack depth on long thin blobs; defeats the linear-time guarantee for adversarial inputs. Not used by BoofCV; not considered here.

## Failure modes and known limits

- **Maximum-contour discard is silent at the point-storage level.** A blob whose boundary exceeds `maxContourLengthPixels` still appears in `contours` with `id` set, but its `externalIndex` set is empty. Downstream consumers that walk `packedPoints.sizeOfSet(c.externalIndex)` will see 0 and need to skip — this is the same Java behaviour, by design.
- **Minimum-contour discard is also silent**. Same shape, same caller-responsibility.
- **`0xFF` sentinel mutates the input.** The internal `border` buffer is mutated — but the *input* `binary` `cv::Mat` is **not** modified (we copy in via `copyTo(border(rect))`). The Java code mutates its input directly via the `border.subimage(...).setTo(binary)` indirection; we always copy. This is a deliberate idiomization for clarity.
- **Connectivity 4 vs 8.** Both are supported. QR uses `EIGHT` (matches `ConfigQrCode.polygon.detector.contourRule`). The JUnit test covers both for synthetic fixtures.
- **`maxContourLength = fixed(-1)` and `minContourLength = fixed(0)`** are the defaults. `fixed(-1)` is interpreted as "no cap" via `computeNegMaxI` (returns INT32_MAX). `fixed(0)` is "no floor". DetectPolygonFromContour overrides both for QR.

### Sentinel arithmetic audit (per cycle A review checklist)

The team-lead's checklist specifically flagged the `-1`-vs-`0xFF` sentinel. Audit results — 5 byte-access sites total:

1. `ContourTracer::checkOne` (`contour_tracer.cpp:182`) tests `binaryData[index] == 1`. Equality with `1` is bit-identical between signed `-1` and unsigned `0xFF`.
2. `ContourTracer::checkOne` (`contour_tracer.cpp:186`) writes the marker `binaryData[index] = static_cast<uint8_t>(0xFF)`. Pure write — no comparison.
3. `LinearContourLabelChang2004::process` step-1 (`linear_contour_label_chang2004.cpp:102`) tests `binaryData[indexIn - binaryStride] != 1`. Inequality with `1`; bit-identical.
4. `LinearContourLabelChang2004::process` step-2 (`linear_contour_label_chang2004.cpp:108`) tests `binaryData[indexIn + binaryStride] == 0`. The pixel below has never been written by the tracer (a step-2 dispatch happens when the pixel below is *background*, not a marker); the equality test is on raw input data only.
5. `LinearContourLabelChang2004::scanForOne` (`linear_contour_label_chang2004.cpp:129`) tests `data[index] != 1`. Inequality with `1`; bit-identical.

**No `<0`, `>=0`, `+1`, or signed arithmetic on a marker pixel anywhere in the trace path.** Safe.

*Cycle A's audit doc originally counted 4 sites; cycle B's review correctly noted the count is 5 (the cycle-A doc lumped `checkOne`'s read and write into one bullet and missed counting `scanForOne` as the fifth distinct location). Substantive conclusion is unchanged — all 5 sites are equality-only.*

## Tunable parameters

| Parameter | Default | Range | Effect |
|---|---|---|---|
| `ConnectRule rule` | `EIGHT` (per QR) | `FOUR` or `EIGHT` | Neighbourhood for connectivity. `EIGHT` makes diagonally-touching pixels part of the same blob. |
| `ConfigLength maxContourLength` | `fixed(-1)` (= no cap) | any positive integer, or `fixed(-1)` for no cap | Per-contour pixel cap. Contours over the cap are recorded as empty sets. QR's `SquareLocatorPatternDetectorBase` overrides this. |
| `ConfigLength minContourLength` | `fixed(0)` (= no floor) | any non-negative integer | Per-contour pixel floor. Contours below the floor are recorded as empty sets. Not exercised by the current QR call path; provided for downstream consumers. |
| `bool saveInternalContours` | `true` | `true` / `false` | When false, internal-hole contours still get a slot in `internalIndexes` but the slot is empty. `maxContourSize=0` is set on the tracer for the trace call, which discards every point on the fly. |

## Integration points for downstream recovery

**Cycle B (wired in).** `DetectPolygonFromContour` now consumes this stage in place of `cv::findContours(RETR_CCOMP, CHAIN_APPROX_NONE)` + the `770f210` reversal + topmost-leftmost rotation fix-up. The port emits BoofCV-native winding (external CW in image, internal CCW) and start pixel directly. Aggregate parity on `qrcodes_v3` held at +0.00pp byte-identical to Java; per-category parity is unchanged from the prior cv::findContours path on every category. Total dataset wallclock dropped ~2× (36.9s → 18.6s) — biggest wins on cv::findContours-dominated categories (`bright_spots` -81%, `brightness` -66%, `curved` -64%).

**Note on the `monitor` / `glare` residuals.** ADR 01 (pre-cycle-B) attributed those residuals to the per-pixel encoding cascade out of `cv::findContours`. Cycle B did *not* close them — they remain at exactly -11.76pp / -3.77pp, byte-identical to the pre-cycle-B state. The cycle-A JUnit suite proves the new port emits Java-identical contours on the same binary input, so the actual root cause must be upstream: the binarizer (`ThresholdBlockOtsu`) produces a binary image that differs from Java's by ~0.6%–1.9% per pixel (ADR 01 reports this number too), and those pixel differences propagate into the contour pixel sequence regardless of which extractor processes them. Cycle C will update ADR 01 to record the re-attribution.

Per CLAUDE.md "Public API design", stages must be reachable in isolation. This stage is exposed via the public header `boofcv_qr/binary/linear_contour_label_chang2004.hpp`. Concrete uses by downstream recovery pipelines:

- **Custom blob-filtering.** Call `process(binary, labeled)`, walk `getContours()` manually, ignore the polygon-fitting downstream. Useful for application-specific region pre-filters.
- **Topology debugging.** `labeled` (`CV_32SC1`) is a per-pixel blob-id map. Save as PNG (scale by 255/max-id) for visual inspection — matches BoofCV's `tools/java_reference` dump output byte-for-byte.
- **Internal-contour-only walk.** `c.internalIndexes` enumerates holes; useful for nested-finder-pattern detection if the consumer wants to bypass the polygon detector entirely.

Alternative implementations that consumers might swap in must preserve: (a) winding (external CW in image, internal CCW), (b) start pixel at topmost-leftmost foreground pixel, (c) the `0xFF` mutation contract on the `border` buffer is internal only — the input `binary` is not modified.

## Cross-references

- Upstream Java: `boofcv.alg.filter.binary.LinearContourLabelChang2004` (228 LOC). Companions: `ContourTracer` (285 LOC, in this port), `ContourPacked`, `Contour`. Pinned tag: see `UPSTREAM_VERSION` (v1.3.0).
- Paper: Chang, Chen, Lu, "A linear-time component-labeling algorithm using contour tracing technique", *Computer Vision and Image Understanding*, vol. 93 (2), 2004, pp. 206–220.
- Closes: `docs/decisions/01_cv_findcontours_substitution.md` (the "When to revisit" conditions were met as documented in cycle 5 perf close-out — see `docs/decisions/04_perf_cycle5_gridToImage_inline.md`).
- Feeds: `src/polygon/detect_polygon_from_contour.cpp` (call site swap is cycle B of this port).
- Related: `src/binary/threshold_block_otsu.cpp` (binarization stage; produces the `binary` input to this stage).
