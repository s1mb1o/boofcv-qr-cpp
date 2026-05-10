# ADR 03: Resume the perf cycle after ADR 02 — `cv::findHomography` LM refinement (cycle 3) + sampler hot path (cycle 4)

**Date:** 2026-05-10
**Status:** Accepted
**Deciders:** User decision after the ADR-02 stop point; profile data captured at cycles 3 and 4 hotspot investigations.
**Related:**
- [ADR 01 — `cv::findContours` substitution](01_cv_findcontours_substitution.md) — parity residuals from the OpenCV contour scanner; the ~600-LOC port that would close them is deferred for v1.
- [ADR 02 — Stop performance work after cycle 1](02_perf_stop_after_cycle1.md) — earlier stop point at decoder-only **5.73× C++/Java**, aggregate parity **-0.08pp** from Java; this ADR is the resumption.

---

## Context

ADR 02 declared "stop the perf cycle, ship `bfbc2e2`" on the basis that the dominant remaining hotspot was inside `cv::findContours` (~95% of CPU time on `brightness` / `bright_spots` test images), which is ADR-01-locked for v1.

A follow-up profile pass on **different** workloads — `lots/image00X` (60-QR-per-image dataset) and `detection/high_version/image029` (Version-40 worst-case from cycle 1) — surfaced two profile-confirmed bottlenecks that were *not* inside `cv::findContours` and *not* covered by the ADR-02 escalation:

1. **`cv::findHomography(method=0)` does undocumented Levenberg–Marquardt refinement** when `npoints > 4`, contributing ~14% of `lots` decode time (`cv::LMSolverImpl::run` → `cv::solve` → `cv::JacobiImpl_<double>` chain in the profile). CLAUDE.md and the sampler algorithm doc both claimed `method=0` was "pure DLT, no iterative refinement" — incorrect for OpenCV. The LM-refined homography also drifted from BoofCV's pure-DLT solution, contributing to the -0.08pp aggregate parity residual.
2. **`QrCodeBinaryGridReader::sampleNearest` + `readBitIntensity` hot path** — the inner-most pixel-access loop, called ~156k times per Version-40 QR scan (177×177 modules × 5 samples per bit). Per-call overhead from `cv::Mat::at<>` (debug bounds check + row-stride multiplication), `std::vector::push_back` (capacity check + size increment per element), and `std::floor` (slower than truncation cast) added up to a measurable fraction of decoder CPU on every category that decodes any QR.

Neither hotspot was reachable by porting `LinearContourLabelChang2004` (the ADR-01 / ADR-02 deferred lever). The user authorised resumption: same per-cycle rules as ADR 02 (one fix, profile-confirmed, parity preserved, codex review per commit).

## Cycles 3 and 4 — committed

### Cycle 3 — `23c1327`: explicit DLT via `cv::SVD::solveZ` in `computeTransform`

**Hotspot.** `cv::findHomography(src, dst, 0)` does *not* skip refinement when `method=0` and `npoints > 4`. OpenCV runs DLT to seed, then iterates Levenberg–Marquardt to minimise reprojection error in a `cv::LMSolverImpl::run` → `cv::solve` → `cv::JacobiImpl_<double>` cascade. BoofCV's `GenerateHomographyLinear` → `HomographyDirectLinearTransform` → `SolveNullSpaceSvd_DDRM` runs **only** the DLT (and the constructor-time `normalize` flag is dead-coded — `shouldNormalize = false;//normalize && points2D != null;` in `process()`).

**Fix.** Build the 2N×9 design matrix in place (Java's `addPoints2D` row layout: cols 3..5 = (-f.x, -f.y, -1), cols 6..8 = (s.y·f.x, s.y·f.y, s.y); cols 0..2 = (f.x, f.y, 1), cols 6..8 = (-s.x·f.x, -s.x·f.y, -s.x)) and solve with `cv::SVD::solveZ(A, h)`. Reshape h row-major into the 3×3 H. Same algorithm BoofCV's `SolveNullSpaceSvd_DDRM` runs.

No Hartley normalisation (mirrors BoofCV's dead-coded flag). No post-DLT scale/sign canonicalisation (BoofCV's `AdjustHomographyMatrix.adjust`): the only consumers of H are `applyHomography` (perspective divide cancels any non-zero scalar multiple) and `cv::invert` (sign/scale invariant), so the canonicalisation is a no-op for this pipeline.

**Result.** Aggregate decode rate **74.32% → 74.40%** = byte-matches Java. The -0.08pp residual from ADR 02 was the LM refinement diverging from BoofCV's pure DLT, **not "the close-out state of v1" as ADR 02 implied**. Cycle 3 was a parity fix as much as a perf fix.

Per-category timing (best of 3 runs, decoder-only sums vs cycle-1 close-out):

| category      | java_ms | bfbc2e2 (ADR-02) | 23c1327 (cycle 3) | C++/Java |
|---------------|--------:|------------------:|-------------------:|---------:|
| **lots**      |   744.6 |            3332.2 |             1459.7 |    1.96× |
| pathological  |    10.9 |              17.5 |               15.9 |    1.46× |
| perspective   |    53.7 |             137.3 |              125.6 |    2.34× |
| nominal       |   382.2 |            1442.5 |             1413.9 |    3.70× |
| (others)      |    —    |               —   |                —   |  (~flat) |
| **TOTAL**     |  8004.8 |           45889.3 |            43539.4 |    5.44× |

`lots` was the headline (60-QR images call `computeTransform` per detected QR; LM cost there was substantial). Aggregate ratio **5.73× → 5.44×** C++/Java.

### Cycle 4 — `ea93854`: array-buffer + ptr-access in `QrCodeBinaryGridReader` hot path

**Hotspot.** `sampleNearest` is the inner-most pixel read on every decode path. V40 codes hit ~156k calls per scan; per-call overhead compounds.

**Fix.** Five surgical changes:

1. `sampleNearest` moved from .cpp to header `inline` so the 5 calls per bit collapse to inline arithmetic + a single pixel read each.
2. `std::floor(x)` → `static_cast<int32_t>(x)`. Java's `(int)x` is truncation toward zero; the cast matches Java's behaviour exactly. `std::floor` differs from `(int)` only for `x ∈ (-1, 0)`, but the subsequent `if (ix < 0) ix = 0` clamp folds both to 0 — output byte-identical, cast is faster.
3. `image_.at<std::uint8_t>(iy, ix)` → `image_.ptr<std::uint8_t>(iy)[ix]`. After clamping, indices are guaranteed in-bounds; `at()` adds a debug bounds check + a `step.p[0] * iy` multiplication that `ptr<>` doesn't.
4. `readBitIntensity`: replace 5× `push_back(...)` with one `intensity.resize(base + 5)` + 5 indexed writes via `intensity.data() + base`. Caller (`readBitIntensityAndThresholdDownRight`) `reserve()`s the full capacity upfront so the resize is non-allocating; collapses 5 capacity-checks + 5 size-increments into 1 each. API contract unchanged — still appends 5 floats per call.
5. Removed unused `<cmath>` include.

**Result.** **Universal speedup**: every category 1.32–1.36× faster — the hot path is in *every* decode, not just decode-heavy ones. Aggregate decoder time **43.5 s → 32.5 s**. Aggregate ratio **5.44× → 4.05×** C++/Java. Parity byte-identical to cycle-3 (74.40%, == Java baseline).

## Decision

**Resume the perf cycle for two more targeted commits, then close.** Cycle 3 banked. Cycle 4 banked. Final state: decoder-only **4.05× C++/Java**, aggregate parity **0.00pp** from Java baseline, 421/421 unit tests pass.

ADR 03 is the resumption; ADR 02's "stop here" decision is superseded by this ADR's "stop here, but two more cycles later." Cross-link both directions.

## Rationale

**Each cycle satisfies the user's per-cycle scope** — one fix, profile-confirmed before the change, parity preserved, codex parity review per commit.

**Cycle 3 was both a perf and a parity fix.** The -0.08pp residual ADR 02 logged as the close-out state was caused by `cv::findHomography`'s LM refinement diverging from BoofCV's pure DLT. Closing that residual was unintended evidence that LM refinement was wrong, not just slow.

**Cycle 4's gain is universal**, not tied to a single category. `sampleNearest` runs in every decode path; the 1.32–1.36× per-category speedup is the inner-loop overhead of `at()` + `push_back` + `floor` showing up everywhere. This is exactly the case ADR 02 missed by profiling only `bright_spots/image010` and `brightness/image010` — those images spend ~95% of decode time inside `cv::findContours`, which masks the ubiquitous-but-smaller `sampleNearest` cost. Other test images have very different profiles.

**Why ADR 02 didn't catch this.** ADR 02's investigation profiled only the noisy-binarisation cluster (`bright_spots` / `brightness`), where `cv::findContours` legitimately dominates. The `lots` and `high_version` workloads have very different profiles — `lots` is `computeTransform`-bound (60 QRs × `computeTransform` per QR), `high_version` is `sampleNearest`-bound (V40 codes have 7×–8× as many bit reads as average). Lesson: **profile across distinct category clusters before concluding "the dominant lever is X."** ADR 03 commits the project to one more profile pass before any future ADR-rewrite.

**Cycle-3 corrected a CLAUDE.md error.** The "OpenCV substitution policy" used to claim `cv::findHomography(method=0)` was "pure DLT, the same algorithm as BoofCV's `GenerateHomographyLinear`; do NOT pass `RANSAC`/`LMEDS`/`RHO` here, those add iterative refinement." The bullet about LM-refinement was correct, but the part claiming `method=0` doesn't add LM was wrong: OpenCV adds LM after DLT for `npoints > 4` regardless of the flag. Cycle 3 updated the policy to call this out and recommend `cv::SVD::solveZ` for the N-point case.

## Empirical perf state — final, post-cycle-4 (`ea93854`)

End-to-end wall clock for the full 562-image regression: **37.7 s** (load + decode + JSON serialise; Java does the same end-to-end in ~22.7 s). Decoder-only sum: **32.5 s** vs Java's **8.0 s** = **4.05× C++/Java**.

| category      | java_ms | cpp_ms (final) | ratio (C++/Java) |
|---------------|--------:|---------------:|-----------------:|
| blurred       |   760.1 |         1891.0 |            2.49× |
| bright_spots  |  1458.5 |        12768.1 |            8.75× |
| brightness    |  1011.4 |         5256.8 |            5.20× |
| close         |   762.1 |         1875.9 |            2.46× |
| curved        |   803.0 |         4166.9 |            5.19× |
| damaged       |   207.4 |          599.5 |            2.89× |
| **decoding**  |    69.0 |           33.0 |   **0.48×** (faster than Java) |
| glare         |   443.1 |         1209.0 |            2.73× |
| high_version  |   331.4 |          535.7 |            1.62× (was 44.4× pre-cycle-1) |
| lots          |   744.6 |         1081.7 |            1.45× |
| monitor       |   436.4 |          901.3 |            2.07× |
| nominal       |   382.2 |         1073.0 |            2.81× |
| noncompliant  |    62.6 |          106.4 |            1.70× |
| pathological  |    10.9 |           12.0 |            1.10× |
| perspective   |    53.7 |           95.0 |            1.77× |
| rotations     |   299.6 |          525.9 |            1.76× |
| shadows       |   168.8 |          318.1 |            1.88× |
| **SUM**       |  8004.8 |        32449.2 |        **4.05×** |

Best-of-4 measurements over the regression set, machine state matched (release build, `-O3 -DNDEBUG`).

### Four-state perf progression

| commit    | label                                        | decoder-only sum | C++/Java | Δ vs prior |
|-----------|----------------------------------------------|-----------------:|---------:|-----------:|
| `bda1650` | pre-perf (parity ship)                       |          ~74.2 s |    9.27× |          — |
| `bfbc2e2` | cycle 1 — `perspectiveTransform` inline      |           45.9 s |    5.73× |      1.62× |
| `23c1327` | cycle 3 — explicit DLT via `cv::SVD::solveZ` |           43.5 s |    5.44× |      1.05× + parity 0.00pp |
| `ea93854` | cycle 4 — sampler hot path                   |           32.5 s |**4.05×** |      1.34× |

Two parity states banked alongside:
- pre-cycle-3: 74.32% (-0.08pp from Java).
- post-cycle-3: **74.40%** (== Java baseline byte-identically). Held through cycle 4.

## Alternatives considered

### (Z) Stop at ADR 02's `bfbc2e2` (the "do nothing" baseline)

- Cost: documentation only. The state ADR 02 shipped.
- Benefit: zero perf risk, but bakes in two known issues — the LM-refinement parity gap (-0.08pp) and the ubiquitous-sampler-overhead cost.
- **Rejected.** Both follow-up cycles were profile-confirmed quick wins fitting ADR 02's per-cycle scope. The brief said "stop the cycle" not "never resume"; resumption with the same gates is consistent with the original framing.

### (Y) Port `LinearContourLabelChang2004` now (the ADR-01 / ADR-02 deferred fork)

- Same as ADR 02: multi-cycle (~600 LOC + tests + algo doc + full parity re-validation), doesn't fit "one fix, profile-confirmed" scope.
- **Rejected for v1**, same reasoning as ADR 02. Cycle 3 + cycle 4 picked the lower-hanging non-`findContours` fruit instead.

### (X) Ship cycle 3 only; defer cycle 4

- Cost: cycle 4 is single-commit, profile-confirmed, byte-parity preserved. Deferring it would leave the universal 1.34× speedup on the floor.
- **Rejected.** The brief authorised both cycles together; cycle 4 was independently approved by the user before the work started.

### (W) **Cycle 3 + cycle 4, then stop. CHOSEN.**

- Cost: two surgical commits, two doc updates, this ADR.
- Benefit: aggregate parity tightened **-0.08pp → 0.00pp** + decoder ratio **5.73× → 4.05× C++/Java**. `decoding` category now **2.1× faster than Java** end-to-end.

## Consequences

**Positive:**

- **Parity is now byte-identical to Java baseline** (74.4038% aggregate, both sides). The -0.08pp residual is closed.
- **Decoder-only ratio dropped from 5.73× to 4.05× C++/Java.** Worst-case category `bright_spots` 8.75× (down from 11.65×); best-case `decoding` 0.48× (faster than Java end-to-end). 11/17 categories now within 3× of Java; 4/17 within 2×.
- **CLAUDE.md substitution policy corrected.** Future engineers no longer encouraged to use `cv::findHomography(method=0)` under the false premise that `method=0` skips LM; the policy now explicitly warns and recommends `cv::SVD::solveZ`.
- **`accepted_residuals.json` reasons unchanged** — the per-category residuals tracked there (the cv::findContours-substitution residuals + small-N "+" outliers) are independent of cycles 3 + 4. The file's `_comment` provenance is updated to reference this ADR.

**Negative:**

- C++ port still **4.05× slower than Java** in aggregate decoder time. `bright_spots` and `brightness` (the cv::findContours-bound categories) account for most of the remaining gap; nothing in cycles 3 + 4 affected `findContours` cost.
- The lesson "profile across category clusters before declaring victory" should have been applied in ADR 02. ADR 03 commits to one more profile pass before any future close-out.

## When to revisit

Reopen if any of the following:

1. **A downstream consumer's input distribution makes the absolute time matter.** Same trigger as ADR 02 — but with a tighter starting point (4.05× instead of 5.73×). At that point, the dominant lever shifts back to porting `LinearContourLabelChang2004` (ADR-01 / ADR-02 fork), which would close the cv::findContours-bound categories and the cv::findContours-substitution parity residuals in one stroke.
2. **A new profile-confirmed bottleneck appears that isn't `cv::findContours`.** ADR 03 commits to one more profile pass before any close-out. The pass MUST cover at least one image from each of: `lots` (multi-QR), `high_version` (V40), `bright_spots` (noisy binarisation), and a "typical" image (`nominal` or `close`). Single-image profiles like ADR 02's are insufficient.
3. **OpenCV's `findContours` gets meaningfully faster.** Same as ADR 02 — successor implementations or new modes that match BoofCV's pixel ordering would let us pick up the speedup without porting.
4. **CLAUDE.md goal-priority changes.** Same as ADR 02.

## References

- [ADR 01 — `cv::findContours` substitution](01_cv_findcontours_substitution.md) — parity residuals; the ~600-LOC port that would close them.
- [ADR 02 — Stop after cycle 1](02_perf_stop_after_cycle1.md) — the earlier stop-point this ADR resumes from.
- ChangeLog entries:
  - `23c1327` — cycle 3: explicit DLT via `cv::SVD::solveZ`; parity tightening 74.32% → 74.40%.
  - `ea93854` — cycle 4: sampler hot-path tightening; universal 1.32–1.36× speedup.
- [src/sampler/qr_code_binary_grid_to_pixel.md](../../src/sampler/qr_code_binary_grid_to_pixel.md) — algorithm doc; "Why this approach" section documents the LM-refinement finding + the explicit-DLT row layout.
- [src/sampler/qr_code_binary_grid_reader.md](../../src/sampler/qr_code_binary_grid_reader.md) — algorithm doc; "Sample-and-clamp" + `readBitIntensity` sections document the cycle-4 changes and the cast/floor-equivalence proof.
- [src/decoder/qr_code_decoder_image.md](../../src/decoder/qr_code_decoder_image.md) — orchestrator algo doc; "Performance" section captures the four-state perf progression and final per-category timing.
- [CLAUDE.md](../../CLAUDE.md) "OpenCV substitution policy" — corrected by cycle 3.
- Sample profile outputs at decision time (kept locally during cycles 3 and 4, not committed):
  `/tmp/profile_lots.txt`, `/tmp/profile_hv.txt`, `/tmp/profile_blurred.txt`,
  `/tmp/qr_profile_brightness.txt`.
