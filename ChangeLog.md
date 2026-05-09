# ChangeLog

## 2026-05-09 (later⁵) — Step 5: perspective grid sampler

### Added (OpenCV becomes a CMake dep at this step)

- `find_package(OpenCV)` in [CMakeLists.txt](CMakeLists.txt). Required components: `core` (Mat, Matx33d, perspectiveTransform), `calib3d` (findHomography), `imgproc` (getPerspectiveTransform). Hint Homebrew's keg-only `OpenCV_DIR` automatically.
- [include/boofcv_qr/qr_code_codeword_locations.hpp](include/boofcv_qr/qr_code_codeword_locations.hpp) + [src/sampler/qr_code_codeword_locations.cpp](src/sampler/qr_code_codeword_locations.cpp) — feature mask + zigzag bit-traversal sequence per QR version. Uses our own `Point2I`; no OpenCV dependency. (MicroQR variant intentionally not ported.)
- [include/boofcv_qr/qr_code_binary_grid_to_pixel.hpp](include/boofcv_qr/qr_code_binary_grid_to_pixel.hpp) + [src/sampler/qr_code_binary_grid_to_pixel.cpp](src/sampler/qr_code_binary_grid_to_pixel.cpp) — grid↔pixel homography. 4-pt setup via `cv::getPerspectiveTransform`; N-pt via `cv::findHomography(..., 0)`; point transforms via `cv::perspectiveTransform` (no `cv::warpPerspective` per CLAUDE.md). Includes outlier rejection (`removeFeatureWithLargestError`) and adjust-with-features residual lookup.
- [include/boofcv_qr/qr_code_binary_grid_reader.hpp](include/boofcv_qr/qr_code_binary_grid_reader.hpp) + [src/sampler/qr_code_binary_grid_reader.cpp](src/sampler/qr_code_binary_grid_reader.cpp) — pixel-level sampler. CV_8UC1 only (Java template specialised). Nearest-neighbour read with EXTENDED-border clamping (matches BoofCV's `nearestNeighborPixelS`); 5-sample majority-vote `readBit`.
- [src/sampler/qr_code_codeword_locations.md](src/sampler/qr_code_codeword_locations.md), [qr_code_binary_grid_to_pixel.md](src/sampler/qr_code_binary_grid_to_pixel.md), [qr_code_binary_grid_reader.md](src/sampler/qr_code_binary_grid_reader.md) — algorithm docs.

### Tests

- [tests/unit/test_qr_code_codeword_locations.cpp](tests/unit/test_qr_code_codeword_locations.cpp) — JUnit-mirrored bit-order check for v2, full-fill for v2..40, data-bit capacity per ISO §6.5.1 for v1/2/3/.../40.
- [tests/unit/test_qr_code_binary_grid_to_pixel.cpp](tests/unit/test_qr_code_binary_grid_to_pixel.cpp) — synthetic 4-corner setup (upstream tests use `QrCodeEncoder`+`QrCodeGeneratorImage` which we don't ship); round-trip imageToGrid∘gridToImage.

### Regression

- C++ unit tests: **119/119 pass** in 1.6 s.
- Java baseline re-run: zero quality drift.

### Deferred from upstream

- `setTransformFromLinesSquare` (line-correspondence DLT used when only 3 finder polygons are reliable) — depends on inputs that step 7's finder-pattern detector produces. Lands then.
- `setLensDistortion` on the grid reader — not needed for QR; consumers can compose `cv::undistortPoints` if they need wide-FOV support.
- `QrCodeBinaryGridReader<T>` template parameter — committed to `CV_8UC1` per CLAUDE.md.

## 2026-05-09 (later⁴) — Step 4: format/version BCH, mask, decoder-bits orchestrator

### Added

- [include/boofcv_qr/qr_code.hpp](include/boofcv_qr/qr_code.hpp) + [src/decoder/qr_code.cpp](src/decoder/qr_code.cpp) — partial port of `QrCode` struct: data fields (`version`, `error`, `mask`, `rawbits`, `corrected`, `message`, `byteEncoding`, `failureCause`, `mode`, `totalBitErrors`, `bitsTransposed`, `thresh*`), `ErrorLevel` enum, `BlockInfo`, `VersionInfo`, plus the populated `VERSION_INFO[1..40]` table verbatim from ISO 18004 Tables 9 + E.1. Geometry fields (`ppCorner` etc.) deferred to step 7+ because they need OpenCV.
- [include/boofcv_qr/qr_code_polynomial_math.hpp](include/boofcv_qr/qr_code_polynomial_math.hpp) + [src/decoder/qr_code_polynomial_math.cpp](src/decoder/qr_code_polynomial_math.cpp) — BCH(15,5) format codec, BCH(18,6) version codec, brute-force minimum-Hamming-distance corrector. Hamming pop-count inlined to avoid pulling in BoofCV's descriptor module.
- [include/boofcv_qr/qr_code_mask_pattern.hpp](include/boofcv_qr/qr_code_mask_pattern.hpp) + [src/decoder/qr_code_mask_pattern.cpp](src/decoder/qr_code_mask_pattern.cpp) — abstract `QrCodeMaskPattern` + 8 Meyer's-singleton subclasses (M000…M111) with the ISO 18004 §7.8.2 / Table 10 XOR formulas verbatim.
- [include/boofcv_qr/qr_code_decoder_bits.hpp](include/boofcv_qr/qr_code_decoder_bits.hpp) + [src/decoder/qr_code_decoder_bits.cpp](src/decoder/qr_code_decoder_bits.cpp) — orchestrator. `applyErrorCorrection` (per-block RS), `decodeMessage` (mode-aware payload extraction), `decodeEci`, `checkPaddingBytes`, `alignToBytes`, `getLengthBits{Numeric,Alphanumeric,Bytes,Kanji}` (mirrors `QrCodeEncoder` length-field-bits-per-version table — duplicated locally so the encoder isn't pulled in).

### Tests

- [tests/unit/test_qr_code.cpp](tests/unit/test_qr_code.cpp) — VERSION_INFO block-arithmetic identity, alignment-coord monotonicity, totalDataBytes against ISO spec, totalModules, reset.
- [tests/unit/test_qr_code_mask_pattern.cpp](tests/unit/test_qr_code_mask_pattern.cpp) — all 9 JUnit cases mirrored.
- [tests/unit/test_qr_code_polynomial_math.cpp](tests/unit/test_qr_code_polynomial_math.cpp) — encode + check + DCH-correct for both BCH codes; mirrors all 7 JUnit cases.
- [tests/unit/test_qr_code_decoder_bits.cpp](tests/unit/test_qr_code_decoder_bits.cpp) — `alignToBytes`, `checkPaddingBytes`, `decodeEci_IsoExample`, `getLengthBits` table. The encoder-dependent tests from upstream (`applyErrorCorrection` round-trip, `invalidEncoding`) are deferred until step 9 since `QrCodeEncoder` isn't part of the decoder-only deliverable.

### Algorithm docs

- [src/decoder/qr_code.md](src/decoder/qr_code.md), [qr_code_polynomial_math.md](src/decoder/qr_code_polynomial_math.md), [qr_code_mask_pattern.md](src/decoder/qr_code_mask_pattern.md), [qr_code_decoder_bits.md](src/decoder/qr_code_decoder_bits.md).

### Regression

- C++ unit tests: **111/111 pass** in 0.56 s.
- Java baseline re-run: zero drift.

### Deferred

- `QrCode` geometry fields (`ppCorner`, `ppDown`, `ppRight`, `bounds`, `Hinv`, `alignment[]`) — need OpenCV's `cv::Point2d`/`Matx33d`. Land in step 6 when OpenCV is added.
- `QrCodeEncoder` — not part of the decoder-only deliverable. Length-field-bits table replicated locally.
- `QrCodeCodeWordLocations` — used by `LOCATION_BITS[]` in BoofCV; only needed by the sampler. Step 5.
- Encoder-dependent tests (`applyErrorCorrection` round-trip, `invalidEncoding`) — pick up at step 9 with end-to-end harness.

## 2026-05-09 (later still still) — Step 3 of the port: codec bits

### Added

- [include/boofcv_qr/qr_mode.hpp](include/boofcv_qr/qr_mode.hpp) — `Mode` enum (with bit codes per ISO 18004 §6.4.1) + `Mode_lookup(int)` helper, and `Failure` enum. Subset of `QrCode.java`; the full struct is deferred to step 4.
- [include/boofcv_qr/packed_bits.hpp](include/boofcv_qr/packed_bits.hpp) — header-only port of `PackedBits8` with `get`/`set`/`read`/`append` (4 overloads)/`growArray`/`zero`/`isIdentical`/`wrap`. The Java `PackedBits` interface and `PackedBits32` variant are intentionally not ported (unused by QR). Doc: [src/decoder/packed_bits.md](src/decoder/packed_bits.md).
- [include/boofcv_qr/eci_encoding.hpp](include/boofcv_qr/eci_encoding.hpp) + [src/decoder/eci_encoding.cpp](src/decoder/eci_encoding.cpp) — UTF-8 validator and ECI designator → charset-name table (ZXing's table mirrored verbatim). Doc: [src/decoder/eci_encoding.md](src/decoder/eci_encoding.md).
- [include/boofcv_qr/qr_codec_bits_utils.hpp](include/boofcv_qr/qr_codec_bits_utils.hpp) + [src/decoder/qr_codec_bits_utils.cpp](src/decoder/qr_codec_bits_utils.cpp) — full port of `QrCodeCodecBitsUtils` (numeric / alphanumeric / byte / kanji decoders, matching encoders, `flipBits8`, `containsKanji`/`containsByte`/`containsAlphaNumeric`). Doc: [src/decoder/qr_codec_bits_utils.md](src/decoder/qr_codec_bits_utils.md).

### Tests

- [tests/unit/test_packed_bits.cpp](tests/unit/test_packed_bits.cpp) — mirrors all 6 cases from `TestPackedBits8.java`.
- [tests/unit/test_eci_encoding.cpp](tests/unit/test_eci_encoding.cpp) — mirrors `TestEciEncoding.java` plus extra coverage of the ECI designator table.
- [tests/unit/test_qr_codec_bits_utils.cpp](tests/unit/test_qr_codec_bits_utils.cpp) — mirrors `TestQrCodeCodecBitsUtils.java`, plus round-trip tests for numeric / alphanumeric / kanji modes (only the byte mode was explicitly tested upstream; the others are additional coverage of our port).

### Charset-handling deviation from upstream — documented

Java's `decodeByte` / `decodeKanji` invoke `new String(bytes, "Shift_JIS")` (and similar) which goes through `java.nio.charset.Charset`. C++ has no built-in equivalent. We port the byte-level algorithm verbatim and store raw bytes in `workString` as a byte sequence, with `selectedByteEncoding` reporting the announced label. Result is byte-equivalent for ASCII / ISO-8859-1 / UTF-8; for `Shift_JIS` and other multi-byte encodings, the consumer must re-decode. See `src/decoder/qr_codec_bits_utils.md` for the rationale.

### Regression check

- C++ unit tests: **85/85 pass** (`ctest --output-on-failure`, 1.6 s).
- Java baseline re-run on the full `boofcv-qrcodes` dataset: zero drift vs the locked [tests/baseline.json](tests/baseline.json) on every quality metric across all 17 categories.

### Harness reliability fix

- [tools/java_reference/src/main/java/qrboofcv/Baseline.java](tools/java_reference/src/main/java/qrboofcv/Baseline.java): parse the ground-truth sidecar **before** loading the image, and retry image-load once after a brief delay if it returns null. Found while investigating a one-off run where one image (`damaged/image018.jpg`) transiently failed to load under heavy host load — that turned a "no image" event into "no GT either", which falsely reported drift. With the fix, an isolated load failure becomes "GT preserved, 0 detections," which surfaces correctly as a precision/recall delta rather than a phantom GT-count change. The locked baseline numbers are unchanged.

### Deferred

- `QrCodeDecoderBits` (orchestrator that splits codewords into RS blocks, applies correction, then runs the codec utils per segment). Depends on `QrCode.VERSION_INFO[]` table — natural fit for step 4 alongside format/version BCH decoders.
- `QrCode` struct full body, `ErrorLevel` enum, `VersionInfo` / `BlockInfo`, mask-pattern XOR — step 4.
- `PackedBits32` — not used by QR.

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

