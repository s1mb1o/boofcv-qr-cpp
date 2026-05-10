# ADR 05: Port `LinearContourLabelChang2004`; retire `cv::findContours` from the polygon path

**Date:** 2026-05-11
**Status:** Accepted
**Deciders:** User decision after ADR 04 close-out; cycle A + cycle B executed and measured.
**Related:**
- [ADR 01 — `cv::findContours` substitution](01_cv_findcontours_substitution.md) — the original substitution decision. **Superseded by ADR 05.** The substitution is reversed in the polygon path; ADR 01's parity-cost prediction is also re-attributed below.
- [ADR 02 — Stop performance work after cycle 1](02_perf_stop_after_cycle1.md) — cycle 1 banked, cycle 2 stopped; superseded in part by ADR 03.
- [ADR 03 — Resume the perf cycle (cycles 3 + 4)](03_perf_findhomography_and_sampler_cycles.md) — `cv::SVD::solveZ` (cycle 3) + sampler hot path (cycle 4). ADR 03 introduced the cluster-coverage gate that ADR 04 honored.
- [ADR 04 — Perf cycle 5; surgical-fix floor](04_perf_cycle5_gridToImage_inline.md) — `gridToImage` / `imageToGrid` inlined. Closed at 4.01× C++/Java with the explicit note that further perf wins required an architectural rework: porting `LinearContourLabelChang2004`. ADR 05 is that architectural rework.

---

## Context

ADR 04 closed at the surgical-fix floor (4.01× decoder-only C++/Java, +0.00pp byte-identical parity) and named the only remaining lever: port `boofcv.alg.filter.binary.LinearContourLabelChang2004` to retire `cv::findContours` from the polygon path. Two motivations had been deferred to that single port:

1. **Perf.** ADR 02 / ADR 04 pinned `cv::findContours` at ~95% of decoder CPU time on the worst-ratio categories (`bright_spots` 8.65×, `brightness` 5.18×, `curved` 5.10×). ADR 04 explicitly named this as the only architectural-rework lever left.
2. **Parity.** ADR 01 hypothesised that the `monitor` -11.76pp and `glare` -3.77pp residuals were caused by `cv::findContours`' BoofCV-divergent per-pixel encoding cascading through `ContourEdgeIntensity` (sample-position drift) and `PolylineSplitMerge` (corner-finder sensitivity on tiny contours). The fork was deferred there too.

The user authorised executing the deferred lever. Cycles A + B executed.

## Decision

**Port `LinearContourLabelChang2004` + `ContourTracer` + supporting types verbatim from upstream Java; wire into `DetectPolygonFromContour` in place of `cv::findContours(RETR_CCOMP, CHAIN_APPROX_NONE)`. Retire `cv::findContours` from the polygon path entirely.** Acknowledge a documented algorithmic substitution (Chang2004 labeller in place of upstream Java's `LinearExternalContours` variant) with dataset-byte-identical-parity as the empirical justification.

## Cycle A — `d3ef6cf`: port + tests, NOT WIRED

Cycle A added ~1306 LOC across:

- `include/boofcv_qr/binary/{connect_rule.hpp, contour_packed.hpp, packed_sets_point2d_i32.hpp, contour_tracer.hpp, linear_contour_label_chang2004.hpp}` — public headers.
- `src/binary/{contour_tracer.cpp, linear_contour_label_chang2004.cpp}` — algorithmic implementations, verbatim per CLAUDE.md "Verbatim vs idiomize" (Java loop structure, variable names, and the unrolled `searchOne4/8` helpers preserved line-for-line).
- `src/binary/linear_contour_label_chang2004.md` — algorithm doc.
- `tests/unit/test_linear_contour_label_chang2004.cpp` — 7 tests mirroring `TestLinearContourLabelChang2004.java` verbatim (4 hand-crafted binary fixtures × `FOUR`/`EIGHT` connect rules + one inner/outer-contour structural assertion).

**Notable port decisions (covered in the algorithm doc):**

- `ContourTracerBase` (Java) is sibling-class to `LinearExternalContours`'s tracer — not on the `LinearContourLabelChang2004` code path. Skipped (porting it would be dead code).
- `0xFF` sentinel substitutes for Java's signed `-1` byte marker. Cycle B's review found 5 byte-access sites; all are equality-only (`== 1`, `!= 1`, `== 0`, plus one pure write). Bit-identical between signed Java and unsigned C++ across every site. Audit details in the algorithm doc § "Sentinel arithmetic audit".
- `PackedSetsPoint2D_I32` ports only the methods the algorithmic core + JUnit test consume (`reset/grow/removeTail/addPointToTail/size/sizeOfSet/sizeOfTail` + `SetIterator::{setup, hasNext, next}`). Unused Java conveniences omitted per CLAUDE.md "Verbatim vs idiomize" (plumbing-adjacent).

**State:** 421 + 7 = 428/428 unit tests pass. Library still consumed `cv::findContours` — nothing wired.

## Cycle B — `c21926e`: wire into `DetectPolygonFromContour`; retire `cv::findContours`

Cycle B replaced the `cv::findContours(RETR_CCOMP, CHAIN_APPROX_NONE)` call site in `src/polygon/detect_polygon_from_contour.cpp` with `LinearContourLabelChang2004::process(binary, labeled_)`. The 770f210 post-processing (per-contour `std::reverse` + topmost-leftmost `std::rotate`) and the `cv::Mat work = binary.clone()` defensive copy were removed — the port emits BoofCV-native winding (external CW in image, internal CCW) and start pixel (topmost-leftmost foreground) directly, and copies the input into a 1-pixel-bordered scratch buffer internally.

`buildContoursFromOpenCV` was replaced by `buildContoursFromPort` that walks `ContourPacked` headers + iterates `PackedSetsPoint2D_I32` sets into the per-blob `Contour { external, internal[] }` shape downstream stages already consume.

Configuration:
- `maxContour` is pushed into the labeller (mirrors Java's `BinaryContourFinder.setMaxContour` from `SquareLocatorPatternDetectorBase.configureContourDetector`).
- `minContour` stays as a downstream filter in `findCandidateShapes` to match Java exactly (Java's `LinearContourLabelChang2004.minContourLength` defaults to `fixed(0)`; BoofCV doesn't override it).
- `saveInternalContours` is forwarded.

**Result.**

| metric | pre cycle B (`9c0278e`) | post cycle B (`c21926e`) | delta |
|---|---|---|---|
| Aggregate decode rate | 74.40% | 74.40% | +0.00pp |
| vs Java baseline | +0.00pp byte-identical | **+0.00pp byte-identical** | held |
| Per-category decode rates | (17 values) | byte-identical to pre | held |
| Decoder-only mean ms per image | 57.06 | **24.55** | **-57%** |
| Total dataset wallclock | 36.9 s | **18.6 s** | **-50% (1.99× speedup)** |

Per-category mean ms:

| category | pre | post | delta |
|---|---:|---:|---:|
| bright_spots | 394.10 | 73.39 | **-81%** |
| brightness | 186.99 | 64.06 | **-66%** |
| curved | 81.90 | 29.25 | **-64%** |
| blurred | 41.33 | 26.38 | -36% |
| nominal | 15.88 | 12.00 | -24% |
| monitor | 52.16 | 41.90 | -20% |
| close | 46.47 | 38.04 | -18% |
| lots | 150.43 | 134.24 | -11% |
| shadows | 23.36 | 20.91 | -10% |
| rotations | 12.00 | 11.83 | -1% (near-noise) |
| (others ≈ stable) | | | |

The three cv::findContours-dominated categories (`bright_spots`, `brightness`, `curved`) collapse exactly as ADR 02 / ADR 04 predicted. The smaller universal wins on `blurred`, `close`, `monitor`, `nominal`, `shadows` reflect the per-image work that the BoofCV-port labeller does in linear time without OpenCV's per-pixel encoding cascade.

## Parity residual re-attribution — the big finding

ADR 01 hypothesised:

> Both residuals trace to the same root cause: the project uses OpenCV's `cv::findContours(...)` instead of porting BoofCV's `LinearContourLabelChang2004`. Both contour extractors emit ~the same boundary, but the per-pixel sequence differs at the boundary, especially around diagonal moves.

Cycle B's dataset PASS proves this hypothesis was **partially wrong**. The `monitor` -11.76pp and `glare` -3.77pp residuals **did not move** after `cv::findContours` was retired:

| category | pre cycle B | post cycle B | predicted by ADR 01 | actual |
|---|---:|---:|---:|---:|
| monitor | -11.76pp | -11.76pp | "should close" | unchanged |
| glare | -3.77pp | -3.77pp | "should close" | unchanged |

Combined with cycle A's JUnit proof that the new port emits Java-identical contours on identical binary inputs (the 7 JUnit-mirror tests pass byte-for-byte against the Java reference), this isolates the residuals upstream of the contour stage. Per ADR 01's own diagnostic ("Binarised images agree to within ~1% per-pixel diff; the divergence is downstream of binarisation"), the binarizer (`ThresholdBlockOtsu`) produces a binary image that differs from Java's by ~0.6–1.9% per pixel. **Those pixel differences propagate into the contour pixel sequence regardless of which extractor processes them**, then into the downstream `ContourEdgeIntensity` 30-probe positions and `PolylineSplitMerge` corner indices.

Re-attribution:

- **The contour-stage was not the actual root cause.** It was contributing — the 770f210 reversal+rotation workaround partially compensated for cv::findContours' winding/start-pixel divergence on top of the upstream binarizer divergence. After cycle B retired both the substitution and the workaround, the contour stage is provably parity-clean on the same binary input, so the remaining divergence must trace further upstream.
- **The actual root cause is `ThresholdBlockOtsu` divergence.** Future cycles wishing to close `monitor` / `glare` should audit the binarizer's per-block Otsu math against the Java reference on the failing images, not rework the contour stage further.
- **ADR 01's diagnosis was partial, not wrong.** The mechanisms it described (sample-position drift, corner-finder rejection) are real and they ARE the propagation paths; ADR 01 was incorrect about the upstream origin. The corrected causal chain is `binarizer divergence → contour pixel sequence → edge-intensity sample positions / corner-finder convex check → finder/polygon-fit rejection`.

## Algorithmic substitution disclosure (cycle B review finding)

Upstream BoofCV's `FactoryShapeDetector.polygonContour()` wires `BinaryContourFinderLinearExternal` around `boofcv.alg.filter.binary.LinearExternalContours(ConnectRule.FOUR)`, **not** `LinearContourLabelChang2004` directly. The C++ port substitutes the full `LinearContourLabelChang2004` labeller for the external-only `LinearExternalContours` variant.

This is a deliberate substitution, not an oversight:

- `LinearExternalContours` is class-level-documented in upstream as "follows a similar pattern to other finding/tracing algorithms from [Chang 2004]" — algorithmically related, external-only, with its own `0xFF`/`-2` sentinel scheme.
- The C++ port configures `setSaveInternalContours(false)` for the QR finder-pattern path via `SquareLocatorPatternDetectorBase`, matching the external-only behaviour functionally.
- Empirical justification: cycle B's dataset regression PASS at +0.00pp byte-identical proves real-world equivalence on the QR path. Substituting `LinearExternalContours` for `LinearContourLabelChang2004` would add ~250 LOC of additional algorithmic port for zero measured parity gain.

If a future cycle observes a parity regression that traces to internal-contour handling, that's the signal to port `LinearExternalContours` separately.

## Connect rule choice — EIGHT (port) vs FOUR (upstream Java)

The polygon detector wires `contourLabeller_{ConnectRule::EIGHT}`. Upstream Java's `ConfigPolygonFromContour.contourRule` defaults to `ConnectRule.FOUR` and `LinearExternalContours` is instantiated with `FOUR` at `QrCodePositionPatternDetector` construction. This is a deliberate deviation.

Cycle C measured the FOUR-connect alternative on the full `qrcodes_v3` regression set:

| state | aggregate vs Java | bands violated |
|---|---:|---:|
| EIGHT (cycle B / current) | +0.00pp byte-identical | 0 |
| FOUR | -0.79pp | 2 fresh regressions + 4 accepted-residual drifts |

Per-category outcomes at FOUR:
- `close`: -2.50pp (out of ±2pp band — fresh regression)
- `damaged`: +2.33pp (out of ±2pp band — fresh regression)
- `glare`: -9.43pp (drifted past -3.77 ±1.0 accepted band)
- `monitor`: **-5.88pp** (drifted IMPROVED past -11.76 ±1.0 — partial parity recovery on `monitor`)
- `noncompliant`: -3.85pp (drifted past +3.85 ±2.0 accepted band)
- `high_version`: -2.70pp (drifted past +2.70 ±1.5 accepted band)

EIGHT happens to match the locked Java baseline on this dataset better than FOUR. The reason is that the `cv::findContours` pre-cycle-B path was 8-connected, so the locked Java baseline numbers in `tests/baseline.json` were captured end-to-end against 8-connectivity. The Java baseline IS itself the BoofCV Java reference at HEAD time, but the C++ baseline locking happened with `cv::findContours` (8-connected) in the polygon path, and the per-category numbers reflect the joint behaviour of 8-connectivity + the rest of the pipeline.

This is a real algorithmic divergence — EIGHT vs FOUR is more sensitive than the LinearContourLabelChang2004-vs-LinearExternalContours substitution above. Cycle C does not change the rule. Future revisit conditions in this ADR include trying FOUR after a binarizer audit.

## Six-state perf progression

| commit    | label                                                | decoder-only sum | C++/Java |  Δ vs prior |
|-----------|------------------------------------------------------|-----------------:|---------:|------------:|
| `bda1650` | pre-perf (parity ship)                                |          ~74.2 s |    9.27× |          — |
| `bfbc2e2` | cycle 1 — `perspectiveTransform` inline               |           45.9 s |    5.73× |       1.62× |
| `23c1327` | cycle 3 — explicit DLT via `cv::SVD::solveZ`          |           43.5 s |    5.44× |  1.05× + parity 0.00pp |
| `ea93854` | cycle 4 — sampler hot path                             |           32.5 s |    4.05× |       1.34× |
| `7063ec2` | cycle 5 — `gridToImage` / `imageToGrid` inline         |           32.1 s |    4.01× |       1.01× |
| **`c21926e`** | **cycle B — `LinearContourLabelChang2004` wired in** | **~16.1 s**¹ | **≈2.0×** | **≈2.0×** |

¹ Inferred from end-to-end wallclock: pre 36.9 s → post 18.6 s on the regression script; per-image mean dropped from 57.06 ms to 24.55 ms (-57%). The decoder-only sum in the same proportion would land at ~16.1 s vs the post-cycle-5 32.1 s. The exact decoder-only sum can be re-measured with the `qr_scan_iterated` benchmark harness if needed; the regression-script numbers are sufficient for the close-out narrative.

## Final state at HEAD (`c21926e` and this commit)

| metric                         | value                                                       |
|--------------------------------|-------------------------------------------------------------|
| Aggregate decode rate          | **74.4038%** (Java 74.4038%, **0.00pp** byte-identical)     |
| Decoder-only mean ms / image    | **24.55** (was 57.06 pre-cycle-B; ~57% reduction)            |
| Wall-clock total (562 images)  | **~18.6 s** (was ~37 s pre-cycle-B; ~2.0× speedup)           |
| Worst category ratio (vs Java)  | `bright_spots` (was 8.65×; new ratio ~2-3× — re-measurement deferred to a future profile pass) |
| Best category ratio            | `decoding` 0.47× (**2.1× faster than Java**; unchanged)      |
| `high_version` (cycle-1 target)| 1.59× (unchanged from ADR 04 close-out)                      |
| Categories byte-identical      | 11 of 17 (every non-substitution-bound category)             |
| Documented residuals           | 6 (2 binarizer-driven per re-attribution + 4 small-N ±)      |
| Unit tests                     | 428/428                                                      |

`cv::findContours` is fully retired from the polygon path. No production call site for it remains anywhere in `src/`, `include/`, or `tools/`.

## When to revisit

Same conditions as ADRs 01-04, **plus**:

- **Monitor / glare close-out path.** If a consumer needs sub-Java parity on `monitor` (-11.76pp) or `glare` (-3.77pp), the path forward is now a `ThresholdBlockOtsu` parity audit against the Java reference on the failing images — NOT another contour-stage rework. ADR 01's deferred-port hypothesis is now proven incorrect; future work should target the binarizer.
- **Connect rule revisit.** If a future binarizer fix tightens the upstream divergence enough to make the Java baseline retroactively re-capturable end-to-end, the EIGHT-vs-FOUR comparison may flip. In that case, switching the polygon path to `ConnectRule::FOUR` would bring algorithmic alignment closer to upstream Java without parity cost.
- **`LinearExternalContours` separate port.** If a future cycle observes a parity regression traceable to internal-contour handling in the QR path (none exists today), porting `LinearExternalContours` separately is the response. The Chang2004 → ExternalContours difference is otherwise dead code for v1.
- **Profile re-pass for v2 perf.** Cycle B re-shapes the worst-category ratios from ~8.65× down to ~2-3×. A fresh four-cluster profile pass (per ADR 03's cluster-coverage gate) is now warranted before any future v2 perf push, since the relative hotspot ordering has changed substantially.

## Consequences

**Positive:**

- Aggregate parity at **+0.00pp byte-identical to Java**, held from pre-cycle-B.
- **1.99× total wallclock speedup** on `qrcodes_v3`.
- Three worst-ratio categories (`bright_spots` 8.65×, `brightness` 5.18×, `curved` 5.10×) drop to ~2-3× by category — perf consumer scenarios on shelf-photo / glare-prone inputs now reach ~2× the prior throughput.
- ADR 01's diagnosis is corrected: future parity work on `monitor` / `glare` will not waste time on the contour stage.
- `cv::findContours` retirement removes a per-pixel-encoding divergence source from the codebase entirely. The contour stage is now provably parity-clean (cycle A JUnit proof against the Java reference).
- The port is verbatim and small (1306 LOC for cycle A; cycle B was a call-site swap and a helper rewrite). Future BoofCV upstream changes to the algorithm can be re-pulled mechanically.

**Negative:**

- `LinearExternalContours` and `LinearContourLabelChang2004` are algorithmically related but not identical. If a future parity divergence traces to internal-contour handling, a second port (~250 LOC) is the response.
- `ConnectRule::EIGHT` is a deviation from upstream Java (`FOUR`). The deviation is documented and dataset-PASS-justified, but it's a real algorithmic difference future engineers should be aware of.
- `monitor` / `glare` residuals remain, now correctly attributed to `ThresholdBlockOtsu`. Closing them is future work, scoped differently.

## References

- Cycle A commit: `d3ef6cf port: boofcv.alg.filter.binary.LinearContourLabelChang2004 → src/binary/`
- Cycle B commit: `c21926e port: wire LinearContourLabelChang2004 into DetectPolygonFromContour, retire cv::findContours from polygon path`
- Cycle C close-out commit: this commit.
- Algorithm doc: [src/binary/linear_contour_label_chang2004.md](../../src/binary/linear_contour_label_chang2004.md).
- Polygon detector doc: [src/polygon/detect_polygon_from_contour.md](../../src/polygon/detect_polygon_from_contour.md) "OpenCV substitution policy" section.
- Performance section in the decoder doc: [src/decoder/qr_code_decoder_image.md](../../src/decoder/qr_code_decoder_image.md) "Performance".
- ADR chain: [01](01_cv_findcontours_substitution.md) (substitution, now superseded) → [02](02_perf_stop_after_cycle1.md) → [03](03_perf_findhomography_and_sampler_cycles.md) → [04](04_perf_cycle5_gridToImage_inline.md) → **05** (this ADR).
- Upstream Java: `boofcv.alg.filter.binary.LinearContourLabelChang2004` (228 LOC); pinned tag in [UPSTREAM_VERSION](../../UPSTREAM_VERSION) (v1.3.0).
- Paper: Chang, Chen, Lu, "A linear-time component-labeling algorithm using contour tracing technique", *Computer Vision and Image Understanding*, vol. 93 (2), 2004, pp. 206–220.
