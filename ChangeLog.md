# ChangeLog

## 2026-05-09 (later)

### Added — Step 1 of the port: Galois field arithmetic

- Root [CMakeLists.txt](CMakeLists.txt) — C++17 strict, OpenCV-free at this stage, GoogleTest pulled via `FetchContent` at `v1.15.2`. Sets `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion`.
- [include/boofcv_qr/galois.hpp](include/boofcv_qr/galois.hpp), [src/galois/galois.cpp](src/galois/galois.cpp) — verbatim port of `boofcv.alg.fiducial.qrcode.GaliosFieldOps` (static utilities) and `GaliosFieldTableOps` (precomputed exp/log tables for `GF(2^numBits)`). Variable names, loop structure, and inline comments preserved per CLAUDE.md "Verbatim vs idiomize". Upstream typo `Galios` retained for cross-reference fidelity.
- [tests/unit/test_galois.cpp](tests/unit/test_galois.cpp) — mirrors all 11 cases from `TestGaliosFieldOps.java` and `TestGaliosFieldTableOps.java`. RNG: `std::mt19937` seeded `234` (same constant as BoofCV's `BoofStandardJUnit`); the tests are property-based so the differing Java/C++ random sequences don't affect what's verified.
- [src/galois/galois.md](src/galois/galois.md) — companion algorithm doc per CLAUDE.md "Algorithm documentation requirement". Covers polynomial-as-int representation, Russian-Peasant-with-reduction, the doubled `exp` table, `(numBits, primitive)` tunables, failure modes (notably `power(0, k)` quirk inherited from Java).

### Regression check

- C++ unit tests: **11/11 pass** (`ctest --output-on-failure`).
- Java baseline re-run on the full `boofcv-qrcodes` dataset: identical quality numbers to the locked [tests/baseline.json](tests/baseline.json) — zero drift on `gt_count`, `det_count`, `matches_at_iou`, `detection_rate`, `precision`, `decode_rate`, `payload_exact_rate` for every category. Wall-clock perf fluctuated within ~10% as expected (background load); perf is not regression-gated.
- `GaliosFieldTableOps_U8` / `_U16` (BoofCV's polynomial-on-byte-array specialisations) deferred to step 2 — their `polyAdd`/`polyMult`/`polyDivide` routines are RS-flavoured and belong with the Reed-Solomon decoder, not pure Galois.

## 2026-05-09

### Added

- Pinned upstream BoofCV at `v1.3.0` ([UPSTREAM_VERSION](UPSTREAM_VERSION)).
- Java baseline harness at [tools/java_reference/](tools/java_reference/) — Gradle project depending on `org.boofcv:boofcv-recognition:1.3.0` and `boofcv-io:1.3.0`. Runs `FactoryFiducial.qrcode()` on every `.jpg`/`.png` under a dataset root and emits per-image JSON (ground truth + detections + decoded payload + wall-clock time) plus a top-level `summary.json`. Builds with `JAVA_HOME=…openjdk@21 ./gradlew run --args="<datasetRoot> <outDir>"`.
- Python scorer at [tests/regression/score.py](tests/regression/score.py) — reads `summary.json`, matches detections to GT polygons by IoU (Shapely), aggregates per-category detection rate, precision, decode-success rate, payload-exact-match rate, and wall-clock mean/p50/p95.
- First Java reference run committed: per-image dumps under [tests/regression/baseline_java/](tests/regression/baseline_java/) and the aggregated [tests/baseline.json](tests/baseline.json) covering the full 562-image / 1258-GT `boofcv-qrcodes` dataset.

### Notes

- Sidecar `.txt` files in the dataset use **two** layouts. `nominal/`, `noncompliant/` etc. use `SETS\n<8 floats per line>`; `close/`, `monitor/`, `perspective/`, `pathological/`, `high_version/` etc. use raw `x\ny\nx\ny\n…` (4 lines per QR). The `decoding/` subset stores plain payload text with no markers. Parser in `Baseline.java` handles all three.
- BoofCV's `getDetections()` only returns *fully decoded* codes; partial detections (polygon found, decode failed) live in `getFailures()`. The current scorer ignores failures, so detection-set "decode rate" equals "detection rate" by construction. Failure-aware scoring (locate-only vs decode-success split) is a planned follow-up.
- Build env: macOS arm64, OpenJDK 21 (Gradle 8.12.1 doesn't yet support Java 25, which is the system default); BoofCV 1.3.0 from Maven Central; Python 3.14 + Shapely 2.1.

