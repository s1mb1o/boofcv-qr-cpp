# ADR 02: Stop performance work after cycle 1; do NOT port `LinearContourLabelChang2004` for perf reasons

**Date:** 2026-05-10
**Status:** **Superseded in part by [ADR 03](03_perf_findhomography_and_sampler_cycles.md).** ADR 02's "do NOT port `LinearContourLabelChang2004` for perf reasons" decision still stands. ADR 02's "stop the perf cycle here" decision is reversed by ADR 03 (which adds two more profile-confirmed cycles, dropping the decoder ratio from 5.73× to 4.05× C++/Java and closing the -0.08pp aggregate parity residual ADR 02 logged as the ship state).
**Deciders:** User decision after escalation; profile data captured at cycle-2 hotspot investigation.
**Related:**
- [ADR 01 — `cv::findContours` substitution](01_cv_findcontours_substitution.md). The two ADRs together cover both the parity-driven and perf-driven decisions on the same architectural fork (port `LinearContourLabelChang2004` vs. keep the OpenCV substitution).
- [ADR 03 — Resume the perf cycle](03_perf_findhomography_and_sampler_cycles.md). Documents what ADR 02 missed: profile-confirmed bottlenecks outside `cv::findContours`, on workloads ADR 02 didn't profile.

---

## Context

After step-9b parity close-out (commit `bda1650`, aggregate 74.32% / -0.08pp from Java's 74.40%), regression timing data showed a substantial **performance gap between the C++ port and BoofCV-Java** on the same dataset. Decoder-only sums diverged depending on snapshot but were consistently in the ~5–9× C++/Java range. The user authorised profile-driven perf cycles (one fix per cycle, profile-confirmed before any code change, parity preserved byte-identically).

Cycle 1 ran. Cycle 2 was attempted, escalated, and stopped. This ADR captures the perf state and the decision not to continue.

## Cycle 1 — committed (`340d038` + fix-up `bfbc2e2`)

**Hotspot found.** Sample profile on `detection/high_version/image029.jpg` (3525×1317, prior C++ time 1304ms vs Java 30ms = **42.7×**) showed >70% of CPU time spent in `cv::Mat::create` / `cv::Mat::release` / `cv::StdMatAllocator::{allocate,deallocate}` chains, all reached from `boofcv_qr::QrCodeBinaryGridToPixel::gridToImage`. That function (and its sibling `imageToGrid`) was implemented as `cv::perspectiveTransform` over a freshly-heap-allocated 1-element `cv::Mat<Point2d>` — heap-allocating per call. The QR bit sampler (`QrCodeBinaryGridReader::readBit`) calls `gridToImage` 5× per module bit, so a Version-40 QR speculative read burns ~156k mat allocations.

**Fix.** Replaced four call sites with an inlined `applyHomography(M, x, y, out)` doing the projective transform directly. Arithmetic order matches OpenCV's `perspectiveTransform_64f`; gating on `|w| > FLT_EPSILON` and zero-fill on the degenerate branch matches OpenCV's behaviour exactly. Output is bit-identical to OpenCV's on both branches.

**Result.**

| metric                                     | pre (`bda1650`) | post (`bfbc2e2`) | speedup |
|--------------------------------------------|----------------:|-----------------:|--------:|
| Decoder-only sum (562 images)              |        ~74.2 s  |          45.9 s  | **1.62×** |
| `high_version` category                    |       14.7 s   |          0.75 s  | **19.58×** |
| `lots`                                     |       10.3 s   |          3.3 s   |   3.08× |
| `decoding`                                 |       244 ms   |          47 ms   |   5.24× (faster than Java's 69 ms) |
| Wall-clock total (562 images)              |       81.3 s   |          52.6 s  |   1.55× |
| Aggregate decode rate                      |       74.32%   |          74.32%  | **byte-identical** |
| Per-category decode rates (17 categories)  |       —         |          —       | byte-identical to `bda1650` |
| Unit tests                                 |     421/421     |        421/421   | pass    |

`high_version` was the worst-case category at 44.4× C++/Java; cycle 1 drove it to 2.27× — i.e. the C++ port is now within a factor of 2 of Java on the high-module-count images that previously dominated decoder runtime.

## Cycle 2 — escalated, not implemented

The cycle-1 changelog hinted at `ThresholdBlockOtsu` tile sweep + `QrCodeBinaryGridReader::sampleNearest` as next pixel-access hotspots. Cycle 2 was scoped to row-pointer rewrites in those sites. Two findings forced an escalation before any code change:

### Finding 1 — `ThresholdBlockOtsu` is already row-pointer-based

`src/binary/threshold_block_otsu.cpp` already accesses pixel data via `input.ptr<std::uint8_t>(y)` in both hot loops:

- `computeBlockStatistics:118-123` (per-block histogram build)
- `thresholdBlock:212-218` (per-block threshold apply)

`grep` for `at<uchar>` / `at<uint8_t>` / `at<std::uint8_t>` in `src/binary/` and `include/boofcv_qr/`: zero hits. The premise of the cycle-2 brief was a stale read of the file from cycle 1's perspective; the binarizer was already done at the time of porting.

### Finding 2 — `cv::findContours` is the actual top hotspot, by a wide margin

Sample profile on `detection/brightness/image010.jpg` (3024×4032, ~825 ms/iter; 21,144 main-thread samples), inclusive samples by function:

| function                                                | inclusive | % of CPU |
|---------------------------------------------------------|----------:|---------:|
| `boofcv_qr::DetectPolygonFromContour::process`          |    19835  | **93.8%** |
| `boofcv_qr::ThresholdBlockOtsu::process`                |      974  |     4.6% |
| (rest)                                                  |     ~335  |     1.6% |

Top-of-stack samples (where the cycles actually burn) — all inside OpenCV's contour scanner:

| frame                                                                     | samples |
|---------------------------------------------------------------------------|--------:|
| `cv::ContourScanner_::findFirstBoundingContour`                           |  16762  |
| `cv::icvFetchContourEx<signed char>`                                      |   1266  |
| `cv::ContourScanner_::findNextX`                                          |    556  |
| `cv::ContourScanner_::contourScan`                                        |    281  |
| `cv::ContourScanner_::findNext`                                           |    142  |
| `cv::BlockStorage<Point_<int>, 1024, 0>::push_back`                       |    124  |
| `_platform_memmove`                                                       |    114  |
| `_platform_memset`                                                        |    103  |
| `cv::ContourDataStorage<Point_<int>, 1024, 0>::push_back`                 |     77  |
| `cv::TreeIterator<cv::Contour>::getNext_s`                                |     73  |
| `tiny_malloc_from_free_list`                                              |     71  |
| `cv::Tree<cv::Contour>::newElem`                                          |     42  |

Same shape on `detection/bright_spots/image008.jpg` (3024×4032, ~1090 ms/iter): findContours dominates, Otsu ~2%.

**~95% of decode time on noisy categories (`brightness`, `bright_spots`) is inside OpenCV's `cv::findContours`.** Not in our wrapper code, not in any pixel-access pattern we control. The runtime cost goes to:

1. Allocating per-contour storage (`BlockStorage` / `ContourDataStorage` push_back chain).
2. Scan-line state machine (`findFirstBoundingContour`, `findNextX`, `findNext`, `contourScan`).
3. Tree traversal for the parent/child hierarchy required by `RETR_CCOMP` (which we need; the finder pattern is a black ring with a black inner island, both contour kinds and their nesting matter).

On these noisy images the binarizer emits thousands of tiny disconnected blobs from glare/shadow speckle, and OpenCV's contour scanner walks every one.

## Decision

**Stop the perf cycle. Ship `bfbc2e2` as the perf state of v1. Do NOT port `LinearContourLabelChang2004` for perf reasons.**

## Rationale

**The big perf lever is ADR-01-rejected.** Cutting `cv::findContours` cost without a parity-breaking workaround means replacing it with a hand-rolled contour scanner — which is exactly what porting `LinearContourLabelChang2004` would do, and exactly what ADR 01 says we don't do for v1.

**Workarounds that don't require porting are parity-breaking on this dataset:**

- (a) **Pre-filter tiny blobs before `findContours`.** A morphological open with even a 2-px kernel kills hundreds of speckle blobs and would cut findContours time materially. But it changes which contours are emitted vs. BoofCV. The 5 borderline `monitor`/`glare` images that already trace to the cv::findContours boundary divergence (per ADR 01) would shift further; other categories' borderline contours could be filtered out and create new misses. Parity-breaking at exactly the points where it's already fragile.
- (b) **Switch retrieval mode.** `RETR_LIST` and `RETR_EXTERNAL` are faster, but both lose the parent/child hierarchy that finder-pattern detection relies on (the inner 3×3 black island lives inside the outer 7×7 ring's hole; CLAUDE.md "OpenCV substitution policy" specifically calls this out). `RETR_TREE` is no faster than `RETR_CCOMP` and may be slower.
- (c) **Chain approx mode.** `CHAIN_APPROX_SIMPLE` would give us fewer points per contour, but `PolylineSplitMerge` and `ContourEdgeIntensity` consume the full per-pixel sequence — parity-breaking and would invalidate the corner finder.

**Re-opening ADR 01 is a multi-cycle commitment.** A clean port of `LinearContourLabelChang2004` is ~600 LOC of verbatim algorithmic code + JUnit-equivalent tests + algorithm doc per CLAUDE.md "Algorithm documentation requirement" + full parity re-validation across the 562-image regression set + risk of breaking the 11 in-band categories that currently work. That is a cycle-N project, not a "one fix, profile-confirmed" cycle.

**Cycle 1's win is real and banked.** 1.62× aggregate, 19.58× on the worst-case category. Worst-case ratio dropped from 44.4× C++/Java to 2.27×. Every category improved or stayed flat; none regressed. Aggregate parity preserved byte-identically.

**v1 is parity-first.** CLAUDE.md goal #1 is algorithmic parity within ~2% of the Java reference; goal #3 is performance. Goal #1 is met (target: ±2pp per category; achieved: 11/17 in band, 6 documented residuals, -0.08pp aggregate). Goal #3 has been improved meaningfully but not exhaustively. Stopping perf at the first profile-confirmed quick win is consistent with the priority ordering in CLAUDE.md.

**Secondary benefit not captured by stopping here.** Porting `LinearContourLabelChang2004` would likely *also* clear the `monitor -11.76` and `glare -3.77` parity residuals tracked in ADR 01 — both trace to the same per-pixel-sequence divergence that makes `cv::findContours` hard to substitute cheaply. If a downstream consumer hits a use case where either the perf gap OR those parity residuals become dominant, ADR 01 + ADR 02 together give the empirical case for revisiting.

## Empirical perf state (final, post-`bfbc2e2`)

| category      | java_ms | cpp_ms (post) | ratio (C++/Java) |
|---------------|--------:|---------------:|-----------------:|
| blurred       |   760.1 |        2566.4  |            3.38× |
| bright_spots  |  1458.5 |       16991.6  |           11.65× |
| brightness    |  1011.4 |        7096.7  |            7.02× |
| close         |   762.1 |        2564.6  |            3.37× |
| curved        |   803.0 |        5811.2  |            7.24× |
| damaged       |   207.4 |         818.3  |            3.94× |
| **decoding**  |    69.0 |          46.6  |       **0.68×** (faster than Java) |
| glare         |   443.1 |        1745.4  |            3.94× |
| high_version  |   331.4 |         750.9  |            2.27× (was 44.4× pre-cycle-1) |
| lots          |   744.6 |        3332.2  |            4.48× |
| monitor       |   436.4 |        1234.8  |            2.83× |
| nominal       |   382.2 |        1442.5  |            3.77× |
| noncompliant  |    62.6 |         143.3  |            2.29× |
| pathological  |    10.9 |          17.5  |            1.60× |
| perspective   |    53.7 |         137.3  |            2.56× |
| rotations     |   299.6 |         747.1  |            2.49× |
| shadows       |   168.8 |         442.8  |            2.62× |
| **SUM**       |  8004.8 |       45889.3  |        **5.73×** |

End-to-end wall clock for the full 562-image regression: **52.6 s** (load + decode + JSON serialise; Java does the same end-to-end run in ~22.7 s). Decoder-only sum is **45.9 s** vs Java's **8.0 s**.

The two slowest categories (`bright_spots` 11.65×, `brightness` 7.02×) are exactly the categories the cycle-2 profile pinned to `cv::findContours` time. Cutting that source of cost is what the deferred ADR-01-revisit would unlock.

## Alternatives considered

### (W) Continue cycle 2 — apply some `cv::findContours` workaround anyway

- Cost: implementation + parity re-run + risk of breaking the 11 in-band categories.
- Benefit: speculative; profile suggests every workaround that doesn't change the contour math materially is parity-breaking.
- **Rejected.** The escalation flagged this; the user agreed not to ship parity risk for speculative perf gain.

### (X) Port `LinearContourLabelChang2004` now

- Cost: multi-cycle (~600 LOC + tests + algo doc + full parity re-validation). Same cost as the ADR-01 rejection; this is the same fork, motivated by perf instead of parity.
- Benefit: would close the cv::findContours-cost gap AND the cv::findContours-substitution parity residual from ADR 01 in one stroke. Decoder-only ratio could plausibly drop from 5.73× to ~2× (extrapolating from the categories not dominated by findContours, where ratios are already 1.6–4×).
- **Rejected for v1.** Multi-cycle work, doesn't fit the "one fix, profile-confirmed" cycle scope authorised by the user. Re-evaluate if a downstream consumer needs the gain.

### (Y) Stop after cycle 1; ship `bfbc2e2`. **CHOSEN.**

- Cost: docs only.
- Benefit: 1.62× aggregate / 19.58× worst-case win banked. Parity-first stance preserved.

## Consequences

**Positive:**

- Cycle 1's win shipped: decoder-only 5.73× C++/Java (down from ~9× pre-cycle-1 on the same machine state), worst-case category cut by 19.58×.
- Parity preserved byte-identically through both perf commits.
- Future engineer revisiting perf has a complete map: profile data, the answer ("it's all in `cv::findContours`"), and the cross-link to ADR 01 explaining why we accept this for v1.

**Negative:**

- C++ port is still **5.73× slower than Java** in aggregate decoder time. Two categories sit above 7× (`brightness` 7.02×, `bright_spots` 11.65×).
- The slow categories are exactly the ones with high contour counts — i.e. real-world noisy inputs (low-contrast, glare, motion blur, low-resolution captures) will see the same ratio.
- A downstream consumer like `pricetag-vision` doing video / multi-frame fusion will pay this cost per frame.

## When to revisit

Reopen this decision if any of the following are true:

1. **A downstream consumer's input distribution makes the absolute time matter.** E.g. real-time scanning at >10 Hz on retail-shelf video, or batch processing where 5× slower than Java is a deployment blocker. At that point, either (a) port `LinearContourLabelChang2004` (combines this ADR's perf benefit with ADR 01's parity benefit) or (b) re-evaluate workarounds (a)–(c) under the new tolerance for parity drift.
2. **CLAUDE.md goal-priority changes.** Goal #3 (perf) currently sits below goals #1 (parity) and #2 (clean API). If priorities flip, the trade space changes.
3. **OpenCV's `findContours` gets meaningfully faster.** Successor implementations or new modes that match BoofCV's pixel ordering exactly would let us pick up the speedup without porting.
4. **A profile-confirmed non-`findContours` hotspot appears.** Cycle-2 profile says the rest of the pipeline is well below 5% each — but as `findContours` cost is reduced (hypothetically), other functions become proportionally larger and may justify their own cycle.

## References

- [ADR 01 — `cv::findContours` substitution](01_cv_findcontours_substitution.md) — the parity-driven version of the same architectural decision; explains why we use OpenCV's contour scanner in the first place.
- ChangeLog entries:
  - `340d038` — cycle 1 fix: inlined single-point homography in `QrCodeBinaryGridToPixel`.
  - `bfbc2e2` — cycle 1 fix-up: match OpenCV's `FLT_EPSILON` guard + zero-fill on the degenerate branch.
- `src/sampler/qr_code_binary_grid_to_pixel.md` — algorithm doc; "Why this approach" section documents the inlining + bit-identity argument.
- `src/decoder/qr_code_decoder_image.md` — orchestrator algo doc; "Performance" section has the final per-category timing table.
- Sample profile outputs at decision time (kept locally during the cycle, not committed):
  `/tmp/qr_profile_brightness.txt`, `/tmp/qr_profile_otsu2.txt`.
- CLAUDE.md "Goals (in priority order)": parity > clean API > performance.
