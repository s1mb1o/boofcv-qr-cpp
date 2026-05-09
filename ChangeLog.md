# ChangeLog

## 2026-05-09 (later still) — Step 2 of the port: Reed-Solomon

### Added

- [include/boofcv_qr/galois_table_ops.hpp](include/boofcv_qr/galois_table_ops.hpp) + [src/galois/galois_table_ops.cpp](src/galois/galois_table_ops.cpp) — port of `GaliosFieldTableOps_U8` and `GaliosFieldTableOps_U16` as a single `GaliosFieldTableOpsT<WordT>` template (Java has two classes only because Java generics can't take primitive types). Public aliases `GaliosFieldTableOps_U8` / `_U16` keep naming parity. Algorithm doc: [src/galois/galois_table_ops.md](src/galois/galois_table_ops.md).
- [include/boofcv_qr/reed_solomon.hpp](include/boofcv_qr/reed_solomon.hpp) + [src/reed_solomon/reed_solomon.cpp](src/reed_solomon/reed_solomon.cpp) — port of `ReedSolomonCodes_U8` / `_U16` as `ReedSolomonCodesT<WordT>`. Berlekamp-Massey error locator, brute-force Chien search, Forney magnitude evaluator, generator polynomial construction (QR `generatorBase=0`, Aztec `=1`). Algorithm doc: [src/reed_solomon/reed_solomon.md](src/reed_solomon/reed_solomon.md).
- [tests/unit/test_galois_table_ops.cpp](tests/unit/test_galois_table_ops.cpp) — typed gtests covering polyScale / polyAdd(_S) / polyAddScaleB / polyMult(_flipA, _S) / polyEval(_S, Continue) / polyDivide(_S). Each Java JUnit case from `TestGaliosFieldTableOps_U8/U16.java` runs against both `uint8_t` and `uint16_t` instantiations.
- [tests/unit/test_reed_solomon.cpp](tests/unit/test_reed_solomon.cpp) — typed gtests covering computeECC (incl. Python reference vector and ISO 18004 §7.2.3 known generators), computeSyndromes, generatorFamily0/Base1, findErrorLocatorPolynomialBM (BM vs direct), findErrorLocations_BruteForce (positive + over-budget), findErrorEvaluator (1/2/3-error reference), correctErrors_hand, and correct_random.

### Regression check

- C++ unit tests: **65/65 pass** (`ctest --output-on-failure`, 0.49 s). The biggest case is `correct_random<uint8_t>` running 60 000 random encode-corrupt-correct cycles across `(numBits, primitive, generatorBase)` configs `(4, 0b10011, 0)`, `(8, 0x11d, 0)`, `(8, 0x11d, 1)` — all green. `<uint16_t>` runs 6 000 of the same as a template-instantiation smoke test.
- Java baseline re-run on the full `boofcv-qrcodes` dataset: identical quality numbers to the locked [tests/baseline.json](tests/baseline.json) — zero drift across all 17 categories on every metric.

### Notes

- **One template instead of two classes** is a deliberate deviation from upstream. CLAUDE.md verbatim spirit (preserve loop structure, variable names, comments) is honoured — every algorithmic line maps 1:1 to either the U8 or the U16 Java source. The "duplication" Java has is purely a Java-language constraint (no `Foo<byte>`); collapsing it in C++ eliminates a long-tail bug class where a fix in U8 forgets to land in U16.
- **`generator_` member trailing underscore** disambiguates the field from the `generator(int degree)` member function (Java uses separate field/method namespaces; C++ doesn't). Watch for this when grepping across languages.
- **`std::function` strategy injection deferred.** CLAUDE.md "Public API design" calls for hooks at `findErrorLocatorPolynomialBM` / `correctErrors` etc. for known-prefix RS and clipped-QR fallback. We exposed every stage as a public method (so consumers can drive the pipeline manually) but did not yet add ctor-time `std::function` injection — too premature without seeing real call sites.

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

