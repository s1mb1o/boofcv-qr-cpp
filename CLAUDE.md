# BoofCV QR Code Port — C++ / OpenCV

## Project

Port BoofCV's QR code detection and decoding module (Java) to C++ on top of OpenCV. Source upstream: https://github.com/lessthanoptimal/BoofCV — primary package `boofcv.alg.fiducial.qrcode` plus its supporting code in `boofcv.alg.shapes`, `boofcv.alg.filter.binary`, and the `georegression` math library.

Deliverable: a standalone C++17 library `qr-boofcv-cpp` exposing a `QrCodeDetector` class with `cv::Mat` input, plus a CLI tool and a regression test harness. License: Apache-2.0 (matches upstream; preserve attribution in `NOTICE`).

## Goals (in priority order)

1. **Algorithmic parity** with BoofCV-Java on the `qrcodes_v3` regression set: per-category read rate within ~2% of the Java reference.
2. **Clean API**: modern C++17 surface, OpenCV-native types at the boundary.
3. **Performance**: not slower than BoofCV-Java on equivalent inputs. SIMD/threading speedups are a follow-on, not v1.

Do NOT prioritize code beauty over correctness. Do NOT invent algorithms. Do NOT "fix" things that look weird in the Java source — they are usually load-bearing.

## Public API design — for downstream recovery pipelines

This library is consumed by **pricetag-vision** (the Lenta-hackathon price-tag CV pipeline), which builds custom QR recovery on top of standard decode. That use case is non-negotiable and shapes the public surface from day 1.

The recovery techniques pricetag-vision plans to layer on top — known-prefix Reed-Solomon decoding, clipped-QR fallback, multi-frame codeword fusion, alignment-pattern-missing fallback, retailer-specific format dialects — **all require owning the post-binarization chain**: calling individual stages in isolation, swapping specific algorithms, and reading raw intermediate output. The API must support that without forking the library.

Concrete requirements:

- **Stages must be callable in isolation.** Runtime order: `binary → polygon → finder → alignment → sampler → format/version + mask XOR → raw-codeword extraction (de-interleave) → RS error correction (uses Galois) → mode decode`. Each stage in this chain is part of the public API, not private implementation. Expose entry points like `find_finders(const cv::Mat& binary)`, `sample_bit_matrix(corners, version)`, `extract_raw_codewords(const BitMatrix& m, format_info)`, `rs_correct(std::vector<uint8_t>& codewords, version, ec_level)`, `decode_message(const std::vector<uint8_t>& corrected_codewords)`. The top-level `QrCodeDetector::detect()` is just one composition; consumers compose differently. Note: this runtime order is **the inverse** of the porting order under "Porting order (bottom-up, mandatory)" — port leaves first, compose them last.
- **Strategy injection for RS decoder and alignment-pattern detector.** Concrete near-term consumer needs: known-prefix RS (treat a fixed payload prefix as known erasures to recover otherwise-unrecoverable codes) and clipped-QR fallback (one finder missing or right edge cut). Provide `std::function`-based or pure-virtual hooks at construction time. Do NOT require inheritance from the concrete detector class.
- **Polygon-only / detection-only mode.** Best-frame selection over video calls detection many times per full decode. Expose `detect_polygons_only()` that stops after the alignment-pattern stage and returns candidate quadrilaterals + finder triplets, without paying RS / mode-decode cost.
- **Raw codewords and erasure positions exposed in the result.** After bit sampling but before mode decoding, the public result must include raw codeword bytes, RS error/erasure positions used, and per-block decode status. Downstream may re-decode with custom RS parameters, apply known-prefix recovery, or fuse codewords across frames before mode-decoding.
- **Stay single-frame.** No multi-frame state, no temporal voting, no panorama logic in this library. That is the consumer's responsibility. The library exposes enough per-frame intermediate state that the consumer can build it on top.
- **No tag-style or domain knowledge.** The port understands ISO/IEC 18004 QR codes only. Retailer-specific dialects (e.g. Lenta's 24-bit prefix) live in the consumer, built on top of the codeword stream this library exposes.
- **`cv::Mat` at boundaries, no global state, value-semantic config.** Detector instances are reentrant. Config passed by value at construction. No singletons, no thread-locals, no `init()` calls.
- **pybind11-friendly from day 1.** Public APIs return owned types (`std::vector`, `std::optional`); no raw pointers in public signatures. `cv::Mat` round-trips through cv2's buffer protocol cleanly. Python bindings are out of v1 scope but the surface must not require an API rewrite to add them.

The intermediate-state debug-dump hooks under "Intermediate-state dumps for debugging parity failures" are diagnostic, not a substitute for these production-API requirements. The two are independent: dumps may be `#ifdef`'d out in release builds; the stage-level API never is.

## Stack

- **C++17** strictly. Do not use C++20 features.
  - Allowed: `std::optional`, `std::variant`, `std::string_view`, `std::filesystem` (test harness/CLI only — NOT in the core lib), structured bindings, `if constexpr`, `[[nodiscard]]`, `std::byte`, `std::array`.
  - Forbidden: concepts, ranges, `std::span`, `std::format`, modules, coroutines.
  - If span semantics are needed, pass `(T* data, size_t size)` or define a small in-house `Span<T>`. Do NOT pull in gsl-lite, abseil, range-v3, or boost.
- **OpenCV 4.5+** for image types, image I/O, color conversion, contour finding, perspective transforms.
- **CMake 3.16+**: `set(CMAKE_CXX_STANDARD 17)`, `STANDARD_REQUIRED ON`, `EXTENSIONS OFF`.
- **GoogleTest** for unit tests.
- **nlohmann/json** (single-header) for fixtures and ground-truth files.
- No other deps in the core library. Test scorer may be Python 3.10+.

The core detector library must be `<filesystem>`-free and OS-agnostic.

## Type mappings (authoritative)

### Image types

| BoofCV    | C++ / OpenCV              |
|-----------|---------------------------|
| `GrayU8`  | `cv::Mat` type `CV_8UC1`  |
| `GrayU16` | `cv::Mat` type `CV_16UC1` |
| `GrayS16` | `cv::Mat` type `CV_16SC1` |
| `GrayS32` | `cv::Mat` type `CV_32SC1` |
| `GrayF32` | `cv::Mat` type `CV_32FC1` |
| `GrayF64` | `cv::Mat` type `CV_64FC1` |

For the QR pipeline, input is `GrayU8` end-to-end. Do NOT template the top-level detector on image type; commit to `cv::Mat CV_8UC1`. Internal scratch buffers may use other types.

### Containers

| BoofCV / Java                                                | C++                                                                 |
|--------------------------------------------------------------|---------------------------------------------------------------------|
| `DogArray<T>`, `FastQueue<T>`, `FastArray<T>`, `GrowArray<T>`| `std::vector<T>`                                                    |
| `DogArray_I32` / `_F32` / `_F64` / `_B`                      | `std::vector<int32_t/float/double/uint8_t>`                         |
| `java.util.List<T>`, `ArrayList<T>`                          | `std::vector<T>`                                                    |
| `java.util.Map<K,V>`                                         | `std::unordered_map<K,V>` (use `std::map` only if order matters)    |
| `java.util.Set<T>`                                           | `std::unordered_set<T>`                                             |

Object pooling / recycling that BoofCV does via `DogArray.reset()` is an optimization. v1 just does fresh `std::vector` allocations. Mark such sites with `// TODO(perf): recycle` so we can revisit later.

### Geometry types

| BoofCV (georegression) | C++                              |
|------------------------|----------------------------------|
| `Point2D_F32`          | `cv::Point2f`                    |
| `Point2D_F64`          | `cv::Point2d`                    |
| `Point2D_I32`          | `cv::Point2i` (a.k.a. `cv::Point`)|
| `Point3D_F64`          | `cv::Point3d`                    |
| `Polygon2D_F64`        | `std::vector<cv::Point2d>`       |
| `Quadrilateral_F64`    | `std::array<cv::Point2d, 4>`     |
| `Homography2D_F64`     | `cv::Matx33d`                    |
| `Affine2D_F64`         | `cv::Matx23d`                    |
| `Vector2D_F64`         | `cv::Vec2d`                      |

### Primitives

- Java `int` → `int32_t`. BoofCV assumes 32-bit ints — do not let it become `long` on 64-bit platforms.
- Java `long` → `int64_t`.
- Java `byte` → `uint8_t` for image data, `int8_t` only when BoofCV explicitly treats it as signed.
- Java `String` → `std::string` (UTF-8). `std::string_view` at API boundaries when the value is non-owning.
- Java `boolean` → `bool`.

## Coordinate conventions — READ THIS

**This is the #1 source of bugs in Java→C++ image ports.**

- BoofCV uses `(x, y)` everywhere: `image.get(x, y)`, `image.set(x, y, v)`. `Point2D` is `(x, y)`.
- OpenCV `cv::Mat::at<T>(i, j)` is `(row, col)` = `(y, x)` — **transposed**.
- OpenCV `cv::Point` IS `(x, y)`. `cv::Mat::at<T>(cv::Point(x, y))` reads the right pixel.

**Rule:** when porting BoofCV `image.get(x, y)`, the C++ equivalent is `mat.at<uchar>(y, x)` OR `mat.at<uchar>(cv::Point(x, y))`. Pick one style per file and stick to it. Recommended: `at(y, x)` with a comment `// (y, x) = (row, col)` near the top of any function doing pixel access.

```java
// Java (BoofCV)
for (int y = 0; y < image.height; y++) {
    for (int x = 0; x < image.width; x++) {
        int v = image.unsafe_get(x, y);
```

```cpp
// C++ (OpenCV) — loop order matches, indexing is (y, x)
for (int y = 0; y < image.rows; y++) {
    for (int x = 0; x < image.cols; x++) {
        uint8_t v = image.at<uint8_t>(y, x);
```

Use row pointers (`image.ptr<uint8_t>(y)`) inside hot loops for speed once parity is verified. Not before.

## Binary image convention

After binarization (step 6), binary images follow BoofCV's convention so that ported code can mirror Java pixel-for-pixel:

- **Format**: `cv::Mat` of type `CV_8UC1`.
- **Values**: `0` = background (light pixel in source), `1` = foreground (dark pixel = a QR module). This matches BoofCV's `GrayU8` binary output.
- **Polarity**: foreground = the **dark** modules. When porting Java conditions like `if (image.unsafe_get(x,y) == 1)`, keep the literal `1`. Do NOT rewrite to `255`.
- **`cv::findContours`**: accepts any non-zero pixel as foreground, so `0/1` works directly — no scale-up needed before contour extraction.
- **PNG dumps**: multiply by `255` only at the dump boundary (`cv::Mat dump = binary * 255;`) so the file is human-readable and byte-comparable across Java / C++ pipelines. Never alter the in-memory `0/1` buffer.
- **Parity diffs**: Java `boofcv.alg.filter.binary` writes `0/1` `GrayU8` too, so byte-for-byte parity diffs against a Java reference dump are direct after both sides apply the `*255` PNG-dump rule.

If a stage genuinely needs `0/255` semantics (e.g. an OpenCV op that interprets the value, not just zero/non-zero), make the conversion local and explicit (`bin * 255`), and convert back if the result re-enters the pipeline.

## OpenCV substitution policy

**Replace with OpenCV** (do NOT port from Java):

- Image I/O (`cv::imread`, `cv::imwrite`).
- Color conversion (`cv::cvtColor`).
- Resize / pyramid (`cv::resize`, `cv::pyrDown`).
- Contour extraction from a binary image (`cv::findContours` with `CHAIN_APPROX_NONE`). Pick the retrieval mode to **match BoofCV's `LinearContourLabelChang2004` configuration** — most stages need both external and internal contours plus the parent/child hierarchy because finder patterns are nested (the inner 3×3 black island lives inside the hole of the outer 7×7 ring). Use `RETR_CCOMP` or `RETR_TREE` and consume the hierarchy as BoofCV does. Do **not** default to `RETR_EXTERNAL` — it silently discards the nested blobs that finder/alignment detection rely on.
- Homography math only: `cv::getPerspectiveTransform` (4-point matrix), `cv::findHomography(..., 0)` (N-point DLT, **`method=0` only** — that's pure DLT, the same algorithm as BoofCV's `GenerateHomographyLinear`; do NOT pass `RANSAC`/`LMEDS`/`RHO` here, those add iterative refinement that diverges from Java), `cv::perspectiveTransform` (transform individual points), `cv::invert` (matrix inverse). Do **not** use `cv::warpPerspective` or `cv::remap` to rectify a QR for bit sampling — BoofCV samples the source binary directly at homography-mapped grid coordinates, and OpenCV's interpolation / rounding / border-replication choices would change sampled bits. `cv::warpPerspective` and `cv::remap` are permitted only outside the QR sampling path (e.g. visualization or debug renders).
- Generic Gaussian blur and Sobel (when not customized by BoofCV).

**Port verbatim** (do NOT substitute OpenCV equivalents):

- BoofCV's local adaptive threshold (their algorithm; `cv::adaptiveThreshold` is NOT equivalent and measurably degrades QR detection rate).
- `BinaryPolygonDetector` and corner refinement.
- Finder pattern (position pattern) detection: `PositionPatternNode`, `SquareGraph`, etc.
- Alignment pattern detection.
- Reed-Solomon and Galois field math.
- Format/version info decoders, mask XOR, mode decoders.
- Bit sampling from the perspective-rectified grid.

## Porting order (bottom-up, mandatory)

Each step ships with green unit tests before the next begins.

1. **Galois field arithmetic** (`GaliosFieldOps`, `GaliosFieldTableOps`). Test against ISO/IEC 18004 Annex worked examples.
2. **Reed-Solomon decoder** (`ReedSolomonCodes_U16` / `_U8`, Berlekamp-Massey, error correction). Test against known codeword vectors.
3. **Mode decoders** (numeric, alphanumeric, byte/UTF-8, kanji, ECI handling) and bit-stream reader. Hand-crafted bitstreams → expected payload.
4. **Format / version info decoders**, mask pattern XOR. Spec-derived test vectors.
5. **Perspective grid sampler**: given 4 corners + version, extract the bit matrix. Test on synthetic clean QR codes.
6. **Binary image preparation**: BoofCV's local threshold. Compare binarized output pixel-for-pixel against the Java reference on a small image set.
7. **Polygon detector** and **finder pattern (position pattern) detector**. Compare detected polygons against Java.
8. **Alignment pattern detector**.
9. **Top-level `QrCodeDetector` orchestrator.** Runtime wiring is `6 → 7 → 8 → 5 → 4 → 2 → 3` — i.e. `binary → polygon/finder → alignment → sampler → format/version + mask XOR → RS (using Galois from step 1) → mode decode`. Note this is the inverse of the porting order: porting goes bottom-up by dependency (Galois first), runtime goes top-down through the pipeline. **Mode decoding consumes RS-corrected codewords, not raw bits — never invert step 2 and step 3 in the runtime path.**

Do NOT skip ahead. If step N is failing, do not start N+1.

## Verbatim vs idiomize (per-file directive)

- **Algorithmic core (steps 1–8 above)**: port line-by-line from Java. Preserve loop structure, variable names, and comments. Do NOT introduce range-based `for`, structured bindings, `std::transform`, `std::accumulate`, or any "modernization." Do NOT collapse multi-step assignments. The Java code has been debugged for years; mechanical fidelity is the goal.
- **Plumbing** (config classes, result structs, factory equivalents, public API surface, CLI, test harness, debug-dump utilities): idiomatic modern C++17 is fine and encouraged. Use `std::optional`, RAII, `[[nodiscard]]`, const-correctness.

When unsure, default to verbatim.

## Forbidden moves

- Do NOT use `cv::QRCodeDetector`, `cv::QRCodeDetectorAruco`, `cv::wechat_qrcode::*`, or any `cv::aruco::*` QR helpers as a substitute for porting BoofCV's logic. The whole point of this project is BoofCV's algorithm.
- Do NOT use `cv::adaptiveThreshold` in place of BoofCV's local threshold.
- Do NOT introduce new dependencies beyond OpenCV, GoogleTest, nlohmann/json without explicit approval in chat.
- Do NOT use exceptions for control flow. Algorithmic failure paths return `std::optional<T>` or push to a `failures` vector, mirroring BoofCV's `getDetections()` / `getFailures()` split. Exceptions are reserved for programmer errors and unrecoverable I/O.
- Do NOT use `auto` in algorithmic-core files when the Java declares an explicit type. Mirror the type. `auto` is fine in plumbing/test code.
- Do NOT use raw `new` / `delete`. RAII, `std::unique_ptr`, value semantics.
- Do NOT use `std::shared_ptr` unless ownership is genuinely shared (it almost never is here).
- Do NOT silently change algorithm behavior to "fix" warnings or "improve robustness." If something looks wrong, leave the behavior intact, add `// FIXME(parity): looks suspicious, verify against Java`, and keep going.
- Do NOT skip the parity check after each ported file.
- Do NOT use `using namespace std;` or `using namespace cv;`. Always qualify.
- Do NOT include `<bits/stdc++.h>` ever.
- Do NOT commit an algorithmic source file without its companion `.md` algorithm doc — the doc is part of the port, not a follow-up.
- Do NOT make a stage callable only through the top-level `QrCodeDetector`. Each stage in steps 1–8 must be reachable as a public entry point so downstream recovery pipelines can compose differently — see "Public API design".
- Do NOT bake retailer-specific or domain-specific knowledge (Lenta prefix, any specific tag style) into this library. Domain dialects live in the consumer.

## Test harness contract

### Layer 1 — Component unit tests (GoogleTest)

For each algorithmic module, port the corresponding JUnit test from `boofcv/main/boofcv-fiducials/src/test/java/boofcv/alg/fiducial/qrcode/`. Same test cases, same expected values. Convert any Java test fixtures to JSON in `tests/fixtures/`.

### Layer 2 — End-to-end regression on `qrcodes_v3`

- Dataset: https://boofcv.org/notwiki/regression/fiducial/qrcodes_v3.zip — 536 images, 1232 QR codes, 16 categories.
- Ground truth: per-image JSON `{path, qr_codes: [{corners: [[x,y]×4], payload: "..."}]}`.
- A one-shot Java tool in `tools/convert_groundtruth/` reads BoofCV's native ground-truth format and emits the JSON. Run once, commit the JSON, never depend on Java again from CI.
- Scorer: per-category precision/recall/F1, IoU ≥ 0.5 for polygon matching, exact-string match for payload.
- CI gate: per-category read rate within 2% of the Java baseline (committed as `tests/baseline.json`).

### Intermediate-state dumps for debugging parity failures

When Java vs C++ output diverges, the ability to dump intermediate state at every pipeline stage is what makes the bug findable. Each stage exposes an optional debug-dump hook:

- Binarized image → PNG.
- Detected polygon corners → JSON.
- Finder pattern triplets → JSON.
- Sampled bit matrix → text grid.
- Decoded codewords (pre-RS) → hex.
- Decoded payload → text.

Same hook signatures on the Java side via a small reference tool in `tools/java_reference/`. Diff stage-by-stage to localize bugs to the earliest point of divergence.

## Repository layout

```
qr-boofcv-cpp/
├── CMakeLists.txt
├── CLAUDE.md                  # this file
├── README.md
├── LICENSE                    # Apache-2.0
├── NOTICE                     # credits BoofCV / Peter Abeles
├── UPSTREAM_VERSION           # BoofCV git tag we ported from
├── include/boofcv_qr/
│   ├── qr_code.hpp            # public result struct
│   ├── qr_code_detector.hpp   # public detector API
│   └── config.hpp
├── src/
│   ├── galois/                # step 1
│   ├── reed_solomon/          # step 2
│   ├── decoder/               # steps 3–4
│   ├── sampler/               # step 5
│   ├── binary/                # step 6
│   ├── polygon/               # step 7a
│   ├── finder/                # step 7b
│   ├── alignment/             # step 8
│   └── detector/              # step 9
├── tests/
│   ├── unit/                  # GoogleTest mirroring JUnit
│   ├── fixtures/              # JSON test vectors
│   ├── regression/            # qrcodes_v3 harness
│   └── baseline.json          # Java reference numbers per category
├── tools/
│   ├── convert_groundtruth/   # one-shot Java → JSON converter
│   ├── java_reference/        # Java tool dumping intermediate state
│   └── cli/                   # command-line scanner
└── third_party/
    └── nlohmann_json/
```

Each algorithmic source file under `src/galois/`, `src/reed_solomon/`, `src/decoder/`, `src/sampler/`, `src/binary/`, `src/polygon/`, `src/finder/`, `src/alignment/`, and `src/detector/` ships with a companion `<file>.md` algorithm doc — see "Algorithm documentation requirement" below.

## Algorithm documentation requirement

For every ported file in steps 1–9 of the porting order — **including the top-level orchestrator (step 9)** — produce a companion `<file>.md` alongside the source documenting **what the algorithm does and why it works** — not what the code is. The audience is a future engineer (likely us) considering writing a new QR decoder from scratch and wanting to borrow BoofCV's good ideas without re-reading 800 lines of Java. The orchestrator's doc covers different ground (stage wiring, runtime order, failure-mode propagation, the public-API contract for stage-isolation use), but it is required for the same reason: the wiring decisions are exactly what a downstream consumer needs to understand before composing stages differently.

Each algorithm doc must cover:

- **One-paragraph summary.** What this stage takes in, what it produces, in plain English.
- **Algorithm description.** The actual approach. E.g., "local threshold computes a per-pixel mean over a 21×21 window; pixels below `mean + bias` become black." Math where it matters. Cite the BoofCV class and method names being described.
- **Why this approach over alternatives.** E.g., why BoofCV uses its own local threshold rather than `cv::adaptiveThreshold`; why finder-pattern detection uses a graph search over square contours rather than the classical 1:1:3:1:1 raster scan; why Berlekamp–Massey vs. Peterson–Gorenstein–Zierler. If BoofCV explicitly chose a non-obvious approach, capture *why* — that is the load-bearing part.
- **Failure modes and known limits.** What inputs make this stage fail. How downstream stages cope (or don't) when this stage's output is wrong or absent.
- **Tunable parameters.** Which constants in the code are knobs, what range is reasonable, what happens at the extremes. Useful when downstream consumers want to tune for their specific inputs (low-res shelf video, motion blur, partial occlusion, etc.).
- **Integration points for downstream recovery.** If this stage is one of the strategy-injection points or has output consumers will want to subclass / replace, name those hooks and what invariants alternative implementations must preserve.
- **Cross-references.** Upstream BoofCV file path, the git tag pinned in `UPSTREAM_VERSION`, any papers or specs cited in BoofCV comments (e.g. ISO/IEC 18004 section numbers), and related stages this one depends on or feeds.

Write the doc **while reading the Java source**, not after — if you can't describe the algorithm in prose, you do not understand it well enough to port it correctly. The doc is part of the port; an algorithmic file without its `.md` is incomplete and the commit is not ready.

These docs are the durable artifact of this project. The C++ source is replaceable; "BoofCV does X for reason Y, with these failure modes and these knobs" is what future-us consults before deciding to fork the algorithm vs. accept a parity bug, or before lifting one of BoofCV's stages into a from-scratch decoder.

## Workflow per file

1. Identify the Java file in upstream BoofCV. Note the package path.
2. Read it end-to-end before writing any C++.
3. Draft the companion algorithm doc (`<file>.md`) while the Java source is fresh — see "Algorithm documentation requirement". If you cannot draft the doc, you do not understand the code well enough to port it. Iterate on the doc as you port; ship it in the same commit as the source.
4. Read the corresponding JUnit test file. That defines correctness.
5. Port the JUnit test first, into `tests/unit/`, with assertions intact even if the implementation isn't there yet.
6. Port the implementation: verbatim where algorithmic, idiomatic where plumbing.
7. Make the unit test pass. Do not move on with failing or skipped tests.
8. Run the regression harness. Per-category numbers should not regress.
9. Finalize the algorithm doc — fill in failure modes / tunables that only became clear during porting.
10. Commit (source + test + `.md` together) with message: `port: <java.package.ClassName> → <cpp/path/file.hpp>`.

## When you get stuck

- If a Java idiom doesn't have an obvious C++ equivalent: prefer the dumb literal translation, leave a `// REVIEW:` comment, keep moving.
- If a test fails and you can't tell whether the bug is in the port or in your test: run the Java reference dump on the same input. Diff the intermediate state. The bug is wherever the dumps first disagree.
- If `qrcodes_v3` regression drops in a category that has nothing to do with the file you just ported: stop and investigate. Cross-stage interference is real and means you broke an invariant the next stage relied on.
- Do NOT ask the user to "verify the algorithm is correct." The algorithm is correct in Java; if it's wrong in C++, the port is wrong.

## Upstream tracking

Pin to a specific BoofCV git tag at project start. Record it in `UPSTREAM_VERSION`. When that tag changes upstream, re-pull and re-diff before applying any updates — Peter Abeles makes algorithmic improvements regularly.