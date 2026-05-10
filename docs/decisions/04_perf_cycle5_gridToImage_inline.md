# ADR 04: Perf cycle 5 — inline `gridToImage` / `imageToGrid` into header (V40 hot path)

**Date:** 2026-05-10
**Status:** Accepted
**Deciders:** User decision after ADR 03 close-out; profile data captured at cycle 5 hotspot investigation.
**Related:**
- [ADR 01 — `cv::findContours` substitution](01_cv_findcontours_substitution.md) — parity residuals from the OpenCV contour scanner; the ~600-LOC port that would close them remains deferred for v1.
- [ADR 02 — Stop performance work after cycle 1](02_perf_stop_after_cycle1.md) — earlier stop point at decoder-only 5.73× C++/Java, aggregate parity -0.08pp; superseded in part by ADR 03.
- [ADR 03 — Resume the perf cycle (cycles 3 + 4)](03_perf_findhomography_and_sampler_cycles.md) — explicit DLT via `cv::SVD::solveZ` (cycle 3) + sampler hot path (cycle 4); aggregate dropped to **4.05× C++/Java** with parity tightened to **0.00pp byte-identical** to Java baseline. ADR 04 is the next-cycle continuation.

---

## Context

ADR 03 had banked cycles 3 + 4 and shipped at 4.05× decoder-only with byte-identical Java parity. ADR 03's "When to revisit" included an explicit cluster-coverage gate: *before declaring victory, profile across distinct category clusters — single-cluster profiling on noisy-binarisation images (the ADR-02 mistake) masks per-decode-loop costs in `lots` / `high_version`.*

The user authorised one more profile-driven cycle. Per ADR 03's gate, the profile pass had to cover four distinct cluster types:

1. **Noisy-binarisation** — `bright_spots/image010` (worst-ratio category, ADR-01-locked).
2. **Multi-QR / decode-heavy** — `lots/image001` (60 QRs per image, dominated by per-QR `computeTransform` / DLT in cycle 3).
3. **High-version / sampler-heavy** — `high_version/image029` + `image012` (V40 candidates, dominated by per-bit-sampling in cycle 4).
4. **Clean baseline** — `nominal/image020` (clean-fast case, exposes what's left after the heavyweights).

The four profiles surfaced one new profile-stable hot path *not* in the ADR-01-locked region and *not* re-targeting cycles 1/3/4: **`QrCodeBinaryGridToPixel::gridToImage` + `imageToGrid`** at **33-35% self time on V40** (stable across `image029` and `image012`). Pre-cycle-5 the methods were defined out-of-line in the .cpp and forwarded to a file-local `applyHomography` helper; both layers were `inline`-tagged but not visible across the .cpp/.hpp boundary, so every call from `QrCodeBinaryGridReader::readBitIntensity` / `readBit` and `QrCodeAlignmentPatternLocator::process` was a real function call. A V40 speculative read invokes `gridToImage` ~156k times (177×177 modules × 5 samples per bit).

The shape was symmetric to cycle-4's `sampleNearest` move into the `qr_code_binary_grid_reader.hpp` header — different file, same surgical pattern. Profile-confirmed, parity-preserving by construction (no algorithm change, no operand reordering), one fix one commit.

## Cycle 5 — committed

### `7063ec2`: inline `gridToImage` / `imageToGrid` into the sampler header

**Hotspot.** `QrCodeBinaryGridToPixel::gridToImage` and `imageToGrid` lived in `src/sampler/qr_code_binary_grid_to_pixel.cpp` and called a file-local `applyHomography(H, x, y, out)` helper that performed the 3×3 mat-vec + perspective divide. Both layers were marked `inline` but only the .cpp's translation unit could see the bodies — every cross-TU call from the bit sampler was a real function call. Profile across `high_version/image029` and `image012` showed `gridToImage` self time at 33-35% of decoder samples on V40, profile-stable across the cluster.

**Fix.** Move `gridToImage` and `imageToGrid` to header `inline` definitions in `include/boofcv_qr/qr_code_binary_grid_to_pixel.hpp`. Body spells out the 3×3 mat-vec + perspective divide arithmetic visibly *inside* each function (no nested `applyHomography` call — the compiler doesn't always inline through both layers, defeating the purpose). The slow `adjustWithFeatures && !adjustments.empty()` branch of `gridToImage` (per-call nearest-pair lookup) is kept *out-of-line* as `applyAdjustment(row, col, pixel)`: that branch is dead on every decode call after cycle 3, and inlining the loop would bloat every call site with code that never runs.

Same `(M·x + b) / (m20·x + m21·y + m22)` arithmetic. Same `FLT_EPSILON` gate. Same `(0, 0)` zero-fill on the degenerate branch. No operand reordering. Bit-identical to the prior out-of-line bodies for any input.

**Triaged-and-rejected alternatives.**
- **`cv::findContours` cascade** on `bright_spots/image010` (still ~95% of CPU there) — ADR-01-locked.
- **`cv::SVD::solveZ` cascade** on `lots/image001` (`_SVDcompute` 40.7%, `JacobiSVDImpl` 35.5%) — banked in cycle 3 as a *parity* fix; the per-QR DLT setup is by design. Reverting to `cv::findHomography` would re-open the -0.08pp parity residual cycle 3 closed.
- **`ThresholdBlockOtsu`** on `nominal/image020` (31.2% + 18.8%) — verbatim BoofCV port; the algorithmic-fidelity rule under "Verbatim vs idiomize" forbids reshaping, and the function is already row-pointer-clean post-step-6.
- **`bitIntensityToBitValue`** (18.3% on V40) — smaller share than `gridToImage`, single-cluster signal only.

**Parity preservation.** Verified by:
- Codex inspection: bit-identical inline body, no ODR violation, no SIMD, verbatim port surface preserved (5/5 verifications clean).
- 421/421 unit tests pass.
- `tools/cli/run_regression.sh` PASS, aggregate `decode_rate = 0.7440381558028617` byte-identical to Java baseline (+0.00pp). Every per-category number byte-identical to `577fb22`.

**Perf delta.**
- Same-state shootout, best of 3 (single machine, single state, fresh build per side):
  - `lots/image001` (4032×3024, 60 QRs): **174.1 → 166.3 ms/iter (-4.5%)**
  - `high_version/image029` (3525×1317, V40): **61.1 → 56.7 ms/iter (-7.2%)**
- Best-of-3 regression-set decoder-only sums on the same machine: pre **41937.9 ms**, post **41759.5 ms** (-0.43% aggregate). Smaller than per-image deltas because nominal / decoding / perspective / pathological are tiny single-QR images where `gridToImage` cost is fractional; the dominant V40-bit-dense and many-QR images get the win.

## Outcomes

### Five-state perf progression

| commit    | label                                            | decoder-only sum | C++/Java | Δ vs prior |
|-----------|--------------------------------------------------|-----------------:|---------:|-----------:|
| `bda1650` | pre-perf (parity ship)                           |          ~74.2 s |    9.27× |          — |
| `bfbc2e2` | cycle 1 — `perspectiveTransform` inline          |           45.9 s |    5.73× |      1.62× |
| `23c1327` | cycle 3 — explicit DLT via `cv::SVD::solveZ`     |           43.5 s |    5.44× |  1.05× + parity 0.00pp |
| `ea93854` | cycle 4 — sampler hot path                       |           32.5 s |    4.05× |      1.34× |
| `7063ec2` | cycle 5 — `gridToImage` / `imageToGrid` inline   |        **32.1 s**|**4.01×** |      1.01× |

### Final state

| metric                         | value                                            |
|--------------------------------|--------------------------------------------------|
| Aggregate decode rate          | **74.4038%** (Java 74.4038%, **0.00pp** delta)   |
| Decoder-only sum (562 images)  | 32.1 s (Java 8.0 s, **4.01× C++/Java**)          |
| Wall-clock total (562 images)  | ~37.7 s (Java 22.7 s)                            |
| Worst category ratio           | `bright_spots` 8.65× (ADR-01-locked)             |
| Best category ratio            | `decoding` 0.47× (**2.1× faster than Java**)     |
| `high_version` (cycle-1 target)| 1.59× (was 44.4× pre-cycle-1)                    |
| Categories within ±2pp parity  | 12 of 17 (every non-substitution-bound category at 0.00pp byte-identical) |
| Documented residuals           | 5 (2 cv::findContours per ADR 01 + 3 small-N ±)  |
| Unit tests                     | 421/421                                          |

### Cycle 5's diminishing-returns reading

Cycle 5's per-image deltas (-4.5% on `lots`, -7.2% on V40) are real and profile-confirmed. The aggregate impact (-0.43% on the regression set) is small because:
- The per-bit-loop savings cycle 5 unlocks compete with the cv::findContours-bound cost in the noisy-binarisation categories that dominate the regression aggregate.
- Cycle 4 already moved `sampleNearest` into the inline-visible position; `gridToImage` was the symmetric remaining target on the upstream side of the per-bit transform → sample chain.
- The four-cluster profile shows the new top hot spots are now either ADR-01-locked (`cv::findContours`), parity-load-bearing (`cv::SVD::solveZ`), or verbatim BoofCV ports (`ThresholdBlockOtsu`) — none surgical-fixable.

This is the surgical-fix floor for v1. Further perf wins require architectural reworks (porting `LinearContourLabelChang2004`) or out-of-scope changes (SIMD, threading) that the project explicitly defers.

## When to revisit

Same conditions as ADRs 01 + 02 + 03, **plus**:

- A fresh profile across the four cluster types (cluster-coverage gate from ADR 03) surfaces a non-`cv::findContours` hot path > 10% across **2+ clusters** that is **not** a verbatim BoofCV port (which is forbidden to reshape) and **not** a parity-load-bearing path (which would re-open closed parity residuals if changed).
- A consumer's input distribution is dominated by V40 / multi-QR scenarios where `cycles 1+3+4+5`'s wins are insufficient and the remaining cv::findContours cost matters end-to-end.
- A consumer needs sub-Java parity on `monitor` / `glare` (the ADR-01-substitution residuals); the contour-extractor port would address both perf and parity for those categories.

If those conditions hold, the path forward is the same one ADRs 01/02/03 named: fork-and-port `boofcv.alg.filter.binary.impl.LinearContourLabelChang2004`. ADR 04 does not name a new path; it confirms the v1 floor.

## Consequences

- The `applyHomography` function-local helper is gone; the arithmetic is inlined directly into `gridToImage` / `imageToGrid` bodies in the header. Any future caller wanting the same arithmetic should either inline it again or re-introduce a header-private helper.
- `applyAdjustment(row, col, pixel)` is now an out-of-line method on `QrCodeBinaryGridToPixel`. It carries the `adjustWithFeatures` slow-path body verbatim — if the adjustments-non-empty path ever becomes hot, this is the place to revisit (currently dead on every decode call).
- The header now pulls in transitive includes (cv::Matx33d, cv::Point2d) that the .cpp didn't expose at file scope before. No downstream consumer depends on the prior narrower interface (the only callers are within `boofcv_qr`).
- ADR 04 commits us to **one more profile pass per ADR 03's cluster-coverage gate** before any future close-out reversal. ADRs 01–04 together form the durable record of "what we decided not to do, and what would justify reversing."

## Cross-references

- Cycle 5 commit: `7063ec2 perf(sampler): inline gridToImage/imageToGrid into header for V40 hot path`.
- Algorithm doc covering the inlined sampler: [src/sampler/qr_code_binary_grid_to_pixel.md](../../src/sampler/qr_code_binary_grid_to_pixel.md) "Why this approach" cycle-5 paragraph.
- Cluster profile rationale: [docs/decisions/03_perf_findhomography_and_sampler_cycles.md](03_perf_findhomography_and_sampler_cycles.md) "When to revisit" — ADR 03 introduced the gate; ADR 04 honors it.
- Final per-category timings: [src/decoder/qr_code_decoder_image.md](../../src/decoder/qr_code_decoder_image.md) "Performance" section.
- Parity gate: `tests/baseline.json` (Java reference) vs `tests/regression/baseline_cpp/score.json` (C++ HEAD); both `decode_rate = 0.7440381558028617`.
