# Plan 03: DogArray recycle pools — incremental perf push, parallel-safe with the LinearContour port

**Date:** 2026-05-10
**Status:** Draft
**Author:** team-lead session
**Runs in parallel with:** the `LinearContourLabelChang2004` port effort (separate session, separate team).
**Cross-references:**
- ADR 03, ADR 04 — perf cycles 3 + 4 + 5; the surgical-fix floor we're now extending.
- ADR 01 — `cv::findContours` substitution; the LinearContour port reverses this.
- CLAUDE.md "Containers" type-mapping table — `DogArray<T>` → `std::vector<T>` was the v1 mapping with explicit `// TODO(perf): recycle` markers at every site.

---

## Context

After perf cycles 1+3+4+5 the C++ port is at decoder-only **4.01× C++/Java** with byte-identical Java parity. ADR 04 declared the surgical-fix floor for v1. The two remaining levers (per ADR-04 / ADR-01 analysis) are:

1. **Architectural — port `LinearContourLabelChang2004`** (~600 LOC, multi-cycle, addresses cv::findContours dominance + parity residuals). Running in parallel session.
2. **Incremental — implement the DogArray recycle pools** at sites we explicitly deferred during the v1 port.

This plan covers (2): a series of small profile-confirmed cycles, each one site, each one commit, each codex-reviewed for parity. The two efforts are **mostly orthogonal** — they touch different files except for one well-defined overlap zone (described below) — so they can run concurrently without merge contention.

## Goal

Implement value-typed recycle pools at every `// TODO(perf): recycle` site. Each cycle:

- One site → one commit
- 421/421 unit tests pass; aggregate `decode_rate = 0.7440381558028617` byte-identical to Java
- Codex parity review per commit (same template as perf cycles 3+4+5)
- ChangeLog entry per commit

Aggregate target: **4.01× → ~3.0–3.5× C++/Java** when fully done. (Hard to predict precisely; recycle-pool wins compound multiplicatively across hot-loop allocations and benchmarking each site individually is the way to ground-truth this.)

## Parallel-safety with the LinearContour port

The LinearContour port (separate session) touches:

- **NEW** `src/polygon/linear_contour_label_chang2004.cpp/hpp` (or similar — a new module).
- **MODIFIED** `src/polygon/detect_polygon_from_contour.cpp` — the `cv::findContours` call site becomes a `LinearContourLabelChang2004::process` call.
- **MODIFIED** `src/polygon/detect_polygon_from_contour.hpp` — minor: probably an injected dependency.
- **MODIFIED** `docs/decisions/05_<...>.md` — new ADR.
- **MODIFIED** `CLAUDE.md` — "OpenCV substitution policy" loses the `cv::findContours` bullet.

This plan touches:

- **MODIFIED** `include/boofcv_qr/polyline/polyline_split_merge.hpp` (+ `.cpp`) — Corner pool.
- **MODIFIED** `include/boofcv_qr/finder/qr_code_position_pattern_graph_generator.hpp` (+ `.cpp`) — `PositionPatternNode` storage.
- **MODIFIED** `include/boofcv_qr/qr_code_decoder_image.hpp` (+ `.cpp`) — `storageQR_`, `intensityBits_`, plus `successes_` / `failures_` recycle.
- **MODIFIED** `include/boofcv_qr/sampler/qr_code_binary_grid_reader.hpp` (+ `.cpp`) — intermediate buffers in `readBitIntensity`.
- **MODIFIED** `include/boofcv_qr/qr_code.hpp` — `alignment[]` recycle (small).
- **MODIFIED** `src/polygon/refine_polygon_to_gray.cpp` — `DogArray<Point2D_F64>` analog.

**The overlap zone**: `src/polygon/detect_polygon_from_contour.cpp` + `.hpp` are touched by the LinearContour port. This plan **must not** modify `DetectPolygonFromContour`'s internal storage (the polygon-info DogArray) until the LinearContour port lands. Document the deferred site explicitly; pick it up as a follow-up cycle after merge.

**Coordination**:

1. Branch hygiene: this plan executes on `main`. The LinearContour port should run on a feature branch (e.g. `perf/linear-contour-port`). When the LinearContour port merges, it rebases on top of however many DogArray cycles have landed.
2. **Avoid edits to `DetectPolygonFromContour` and any new `linear_contour_label_chang2004.*` files in this plan.** If profiling identifies a recycle site there, defer it.
3. ADR numbering: this plan ships ADR 06 at the end (a single retrospective ADR documenting all DogArray cycles, similar to ADR 03 covering cycles 3+4). The LinearContour port likely ships ADR 05 mid-flight. If they collide on numbering, the second-to-merge re-numbers their ADR.
4. ChangeLog entries: plain top-of-file entries per commit; both efforts have non-overlapping headers per commit so merges should be conflict-free if `git merge` semantics are clean (they are for top-of-file insertions).

## Pattern — `RecyclePool<T>`

We introduce **one** lightweight pool primitive that mirrors BoofCV's `DogArray<T>` semantics:

```cpp
// include/boofcv_qr/util/recycle_pool.hpp (new file in cycle 1)
namespace boofcv_qr {

// DogArray-equivalent: reuses storage across reset() calls so per-decode
// short-lived objects don't repeatedly hit the allocator.
template <typename T>
class RecyclePool {
public:
    using reset_fn = void (*)(T&);

    explicit RecyclePool(reset_fn reset = nullptr) : reset_(reset) {}

    // Grab a fresh-or-recycled element. Storage stays valid until reset().
    T& grab() {
        if (size_ == storage_.size()) {
            storage_.emplace_back();
        }
        T& obj = storage_[size_++];
        if (reset_) reset_(obj);
        return obj;
    }

    // Mark all elements available for reuse on the next call. Does NOT
    // free storage — that's the whole point.
    void reset() noexcept { size_ = 0; }

    // Visible elements — only [0, size_) are valid.
    std::size_t size() const noexcept { return size_; }
    T& operator[](std::size_t i) { return storage_[i]; }
    const T& operator[](std::size_t i) const { return storage_[i]; }

    // For range-based for over visible elements only.
    auto begin() { return storage_.begin(); }
    auto end()   { return storage_.begin() + static_cast<std::ptrdiff_t>(size_); }

    // Test/diagnostic; not for hot-path use.
    std::size_t capacity() const noexcept { return storage_.size(); }

private:
    std::vector<T> storage_;
    std::size_t size_ = 0;
    reset_fn reset_ = nullptr;
};

}  // namespace boofcv_qr
```

**Properties this preserves vs `std::vector<T>`**:
- Stable references across `reset()` (storage_ never shrinks below high-water mark; `T*` pointers obtained between resets stay valid).
- Same iteration semantics for the visible window.
- Owners can call `clear() / shrink_to_fit()` if memory pressure matters; default is hold-and-reuse.

**Properties this differs from `std::vector<T>`**:
- `reset()` does NOT call destructors. If `T` holds resources (file handles, mutexes, etc.) the pool is wrong; for plain data + container-of-trivial-types it's correct. **All recycle-target types are plain data**, audited per-site.
- `operator[]` doesn't bounds-check beyond `[0, size_)`. Callers respect the visible window; `at()` would defeat the cycle-4-style bounds-check elimination.

**Why not just `std::vector<T>` with `clear() + reserve(N)`?**
- `clear()` calls destructors on every element. For non-trivially-destructible `T` (e.g. `cv::Mat`-holders, `std::vector<>`-holders), that's the cost we're trying to eliminate.
- `RecyclePool` can run `reset_fn` instead of destruct+construct — exactly mirroring BoofCV's `DogArray<T>(T::new, T::reset)` constructor pattern.

The `reset_fn` is a function pointer (not `std::function`) to avoid type-erasure overhead. Members can be lambdas-cast-to-function-pointers when stateless.

**Testing**: `tests/unit/test_recycle_pool.cpp` covers `grab()` allocation behavior, `reset()` invariants, stable references across reset, reset_fn invocation. Lands in cycle 1 with the primitive.

## Sites — prioritized

Each site is a separate cycle. Order chosen by **expected impact × low conflict risk × minimal cross-stage dependencies**:

### Cycle B1 — `RecyclePool<T>` primitive + tests

**Files:** `include/boofcv_qr/util/recycle_pool.hpp` (new), `tests/unit/test_recycle_pool.cpp` (new), `CMakeLists.txt` (add test source).

**No code-path change yet** — just the primitive + tests. Trivial codex review (no parity surface). 421 → 421+N tests.

**Why first**: every subsequent cycle uses this primitive; landing it standalone makes per-cycle reviews focused on the *application* of the pool, not its mechanics.

### Cycle B2 — `QrCodeDecoderImage::storageQR_` + `intensityBits_`

**Files:** `include/boofcv_qr/qr_code_decoder_image.hpp` (`successes_` storage, `failures_` storage, `intensityBits_` mirror Java's `DogArray_F32 intensityBits`), `src/decoder/qr_code_decoder_image.cpp` (call sites).

**Java reference:** `QrCodeDecoderImage.java`:
- `DogArray<QrCode> storageQR = new DogArray<>(QrCode::new, QrCode::reset);` + `storageQR.reset();` per `process()` call
- `DogArray_F32 intensityBits = new DogArray_F32();` + `intensityBits.reset();` per `process()` call

**Current C++:** `std::vector<QrCode> storageQR_` + `std::vector<float> intensityBits_` + `clear()` per process. `QrCode::reset()` exists already (we use it). Convert to `RecyclePool<QrCode>` with `&QrCode::reset` as the reset_fn; convert `intensityBits_` to `RecyclePool<float>` (or just `std::vector<float>` with `clear()` to size=0 retained capacity — float doesn't need destructor calls, the pool is overkill).

**Expected payoff:** every `process()` call currently destruct+constructs every `QrCode` in `storageQR_`. With pool, only the first call allocates. `lots/image001` (60 QRs) is the canonical test — likely 5-15% improvement on that category.

**Critical surface preservation:** `getSuccesses()` / `getFailures()` returns. These currently return `const std::vector<QrCode>&`. Either:
- (a) Add a wrapper that returns a `Span`-like view of the visible window. Cleanest but touches public API.
- (b) Keep returning `const std::vector<QrCode>&` from a *separate* "view buffer" that copies from the pool at end-of-`process()`. Simple but partly defeats the cycle.
- (c) Have `getSuccesses()` return the pool's visible window directly via an iterator-pair API.

**Recommendation**: (a) with a small `PoolView<T>` adapter. Discuss in the codex review of B2.

### Cycle B3 — `PolylineSplitMerge::corners` (the `CornerPool`)

**File:** `include/boofcv_qr/polyline/polyline_split_merge.hpp` (the file's existing `// TODO(perf): recycle` at line 167), `src/polygon/polyline_split_merge.cpp`.

**Java reference:** `PolylineSplitMerge.java` uses a custom `Pool<Corner>` already (BoofCV's polyline-split-merge corner pool predates DogArray). We documented this in `polyline_split_merge.md:86`.

**Current C++:** `std::unique_ptr<Corner>` storage with manual reuse. Convert to `RecyclePool<Corner>` with a stateless `Corner::reset` static helper.

**Expected payoff:** the polyline corner finder is called once per polygon candidate; `lots/image001` may have hundreds of polygon candidates per call. Likely 5-10% on polygon-heavy categories (`lots`, `blurred`, `pathological`).

### Cycle B4 — `QrCodePositionPatternGraphGenerator` `PositionPatternNode` storage

**Files:** `include/boofcv_qr/finder/qr_code_position_pattern_graph_generator.hpp` (+ `.cpp`).

**Java reference:** `QrCodePositionPatternGraphGenerator.java` uses `DogArray<PositionPatternNode>`.

**Current C++:** `std::vector<PositionPatternNode>` cleared per call. Convert to `RecyclePool<PositionPatternNode>` with appropriate reset.

**Expected payoff:** every QR scan pays one PositionPatternNode allocation set; small but uniform.

### Cycle B5 — `QrCodeAlignmentPatternLocator` intermediate buffers

**Files:** `include/boofcv_qr/alignment/qr_code_alignment_pattern_locator.hpp` (+ `.cpp`).

**Java reference:** `QrCodeAlignmentPatternLocator.java` uses several `DogArray_*` for bilinear interpolation buffers + alignment-pattern candidates.

**Current C++:** scratch buffers allocated per `process()` call. Convert to pooled members.

**Expected payoff:** alignment locator runs once per QR; `lots/image001` is the canonical case.

### Cycle B6 — `QrCode::alignment[]` recycle (small)

**File:** `include/boofcv_qr/qr_code.hpp:178` (the existing TODO marker).

**Decision needed:** `QrCode::alignment` is part of the **public result struct**. Pool semantics affect users (the visible `vector<Alignment>` becomes a `RecyclePool<Alignment>` view). Two options:
- (a) Public surface keeps `std::vector<Alignment>`; the orchestrator copies from a pool to the result vector at end-of-`decode()`. Public API unchanged.
- (b) Public surface becomes a span/view. Breaks API.

**Recommendation**: (a) — small site, simple fix, no API churn.

### Cycle B7 — `RefinePolygonToGray` `DogArray<Point2D_F64>`

**File:** `src/polygon/refine_polygon_to_gray.cpp` + corresponding `.hpp`.

**Java reference:** `RefinePolygonToGray.java`.

**Current C++:** `std::vector<cv::Point2d>` scratch.

**⚠️ Conflict-zone risk:** `RefinePolygonToGray` is downstream of `DetectPolygonFromContour`, which the LinearContour port modifies. `RefinePolygonToGray` itself is **not** touched by the port (the port replaces the contour producer, not the polygon refiner). Should be safe to do in parallel; verify by reading the LinearContour port plan once it's drafted.

### Cycle B8 — Final retrospective ADR + benchmark

**Files:** `docs/decisions/06_dograrray_recycle_pools.md` (or whatever number is free), `src/decoder/qr_code_decoder_image.md` Performance section, `ChangeLog.md`.

Records: per-cycle deltas, final aggregate, what was deferred (e.g. `DetectPolygonFromContour` storage if LinearContour port hasn't merged yet).

## Per-cycle deliverable shape (mirrors perf cycles 3+4+5)

1. **Profile the target site BEFORE the change.** If allocator/destructor overhead at this site is < 1% across all four ADR-03 clusters (`bright_spots` / `lots` / `high_version` / `nominal`), skip the cycle and move on. **Do not implement based on the TODO marker alone.** The marker says "consider this," not "do this."
2. **Apply the change.** Use `RecyclePool<T>` from cycle B1. Single commit.
3. **Verify parity:**
   - 421/421 unit tests pass.
   - `tools/cli/run_regression.sh` PASS, aggregate `decode_rate = 0.7440381558028617` byte-identical to Java.
   - All per-category numbers byte-identical to commit-before-this-cycle (`git diff` of `tests/regression/baseline_cpp/score.json` should be empty for `decode_rate` fields).
4. **Re-measure perf:** profile-mode timing on the canonical image for this cycle's category (e.g. `lots/image001` for B2/B5, `high_version/image029` for B4, etc.). Record in commit body.
5. **Codex parity review** per the perf-cycles-3+4+5 template. Specific scrutiny: pool's `reset()` semantics vs `clear()` (no destructor calls), stable references, no aliasing surprises.
6. **ChangeLog top-of-file entry.**
7. **Algorithm doc update if applicable** (e.g. `polyline_split_merge.md` gets a "recycled storage" note in B3).

## Risk / parity preservation

**Low risk** — recycle pools change *storage*, not algorithmic *output*. As long as:

- `reset()` is called between distinct call instances.
- The visible window `[0, size_)` is respected.
- The reset_fn correctly reinitializes pooled objects (the canonical hazard: forgetting to reset a vector member that the previous use grew to a non-trivial size).

The third bullet is the parity hazard. The codex review per cycle should explicitly verify this.

**Specific parity hazards to flag in codex prompts:**

1. **Stale state in pooled `T`.** A `QrCode` from the pool's last use has its `rawbits`/`corrected`/`message` etc. populated. If `decode()` reads a field before writing it (e.g. an `if (qr.message.empty())` check that wasn't `qr.message = "..."` zeroed), the pool returns stale data. Codex should grep for early reads.

2. **`std::vector` member sizes after reset.** If `Corner` has a `std::vector<int> sample_offsets`, `reset_fn` must `sample_offsets.clear()`, not leave it at last-use's size.

3. **`std::vector` reallocation breaks pool's stable-reference invariant.** If pool's storage_ grows and any code holds `T*` from before the grow, that pointer is now dangling. Audit per-site for retained pointers across `grab()` calls within the same iteration.

4. **`reset()` order.** The pool resets the *next* time you `grab()`, not when you call `pool.reset()`. So `pool.reset(); for (...) pool.grab()` is correct; `for (...) pool.grab(); /* no reset */` between calls is a leak (the pool grows monotonically).

## Expected total payoff (rough, ground-truth via cycle measurements)

- B2 (orchestrator pool) likely gives the largest single contribution because `lots` does 60 process()-per-image work.
- B3 (polyline corner pool) compounds across every polygon candidate.
- B5 (alignment locator) compounds across every QR scan.
- B4, B6, B7 are smaller.

Aggregate target: **4.01× → ~3.0–3.5×**. The variance is wide because compound effects don't sum linearly; each cycle's measurement is the truth.

## Cycle ordering principle

**B1 → B2 → B3 → B5 → B4 → B6 → B7 → B8.**

- B1 first (primitive).
- B2 next (largest expected payoff; if it under-delivers, recalibrate before doing the smaller sites).
- B3, B5 mid-cycle (medium payoff).
- B4, B6, B7 last (smaller but mechanical).
- **B7 only if LinearContour port hasn't claimed `RefinePolygonToGray` (extremely unlikely but check the upstream branch first).**
- B8 retrospective ADR; only after all banked.

## When to abort a cycle

- If profile says target site is < 1% of decode time → skip, move to next.
- If parity drifts → revert, investigate. Do NOT ship a parity-affecting recycle pool. (CLAUDE.md "do NOT silently change algorithm behavior" applies — if pooling reveals a bug we had elsewhere that was masked by clear()-induced re-init, that's a separate fix in its own commit.)
- If LinearContour port lands and conflicts surface → rebase, redo the affected cycle.

## Done definition for the whole plan

- All 7 site cycles (B2–B7) attempted; aborted ones documented.
- B1 primitive + tests committed standalone.
- B8 retrospective ADR records the per-cycle deltas + final aggregate.
- 421/421 unit tests pass throughout.
- Aggregate parity remains 0.00pp byte-identical to Java throughout.
- Final aggregate ratio recorded in ADR + Performance section.

## Coordination handoff to LinearContour session

The LinearContour port session should know:

1. This plan exists; runs on `main`.
2. The deferred site `DetectPolygonFromContour` polygon-info storage is **theirs to recycle** as part of the port (since they're rewriting the producer). Or they leave it as-is and we pick it up in a B9 follow-up after their merge.
3. ADR numbering: they take 05, this plan takes 06. If they slip behind, they take 06 and we take 05.
4. CLAUDE.md "Containers" type-mapping table is updated by both efforts (LinearContour potentially adds a note about the new contour scanner; this plan adds a note about `RecyclePool<T>`). Coordinate via the merge order.
5. If they want to use `RecyclePool<T>` themselves once B1 lands, the primitive is theirs to use freely.

End.
