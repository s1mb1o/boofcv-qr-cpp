// Port of boofcv.alg.fiducial.qrcode.QrCodeDecoderImage (BoofCV v1.3.0).
// Step 9: top-level orchestrator that consumes a `PositionPatternNode`
// graph from step 7c and a `cv::Mat CV_8UC1` source image, runs the
// full decode pipeline (format → version → alignment → iterative-
// transform/readRawData → RS → mode dispatch), and produces a
// `std::vector<QrCode>` of successes plus a `std::vector<QrCode>` of
// failures-with-cause.
//
// Per CLAUDE.md type mappings: ImageGray<T> -> cv::Mat CV_8UC1
// (template <T> dropped). Lens distortion not ported (deferred —
// downstream pricetag-vision pipeline doesn't use it).
//
// CLAUDE.md "Public API design" mandates that land here:
//   1. Stage-isolation public entry points (find_finders,
//      sample_bit_matrix, extract_raw_codewords, rs_correct,
//      decode_message).
//   2. Strategy injection: std::function hooks for RS decoder + the
//      alignment-pattern locator.
//   3. detect_polygons_only() polygon-only mode.
//   4. rawCodewords / rsErrorLocations / blockStatus surfaced on QrCode.
//   5. setTransformFromLinesSquare / setMarkerUnknownVersion finally
//      get used (rough-homography pre-version path).
//   6. bitsTransposed retry path.
//   7. pybind11-friendliness — no raw pointers in public signatures.
//
// Algorithm description: src/decoder/qr_code_decoder_image.md.

#ifndef BOOFCV_QR_QR_CODE_DECODER_IMAGE_HPP
#define BOOFCV_QR_QR_CODE_DECODER_IMAGE_HPP

#include "boofcv_qr/alignment/qr_code_alignment_pattern_locator.hpp"
#include "boofcv_qr/packed_bits.hpp"
#include "boofcv_qr/position_pattern_node.hpp"
#include "boofcv_qr/qr_code.hpp"
#include "boofcv_qr/qr_code_binary_grid_reader.hpp"
#include "boofcv_qr/qr_code_codeword_locations.hpp"
#include "boofcv_qr/qr_code_decoder_bits.hpp"

#include <opencv2/core.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace boofcv_qr {

// Helper struct surfaced by `find_finders(...)` — finder triplets
// without running any decoding. Each triplet is in canonical
// orientation (`setPositionPatterns` rotated the polygons so corners
// align with Java's `ppCorner[0..3]` / `ppRight[0..3]` / `ppDown[0..3]`
// ordering convention).
struct PositionPatternTriplet {
    std::array<cv::Point2d, 4> ppCorner{};
    std::array<cv::Point2d, 4> ppRight{};
    std::array<cv::Point2d, 4> ppDown{};
    double threshCorner = 0.0;
    double threshRight = 0.0;
    double threshDown = 0.0;
};

// Result of `detect_polygons_only(...)` — runs binarize → polygon →
// finder → alignment but stops before sampling/RS/mode. The
// `qrCodes` vector contains one QrCode entry per candidate triplet
// with `ppCorner`/`ppRight`/`ppDown`/`bounds`/`alignment[]` populated
// but `version`/`error`/`mask`/`message`/`rawbits`/`corrected` left
// empty.
struct PolygonOnlyResult {
    std::vector<QrCode> qrCodes;
};

// Forward-declare the test-only access shim so the friend grant in
// `QrCodeDecoderImage` compiles without test code in the public include
// path. The shim is defined alongside the test fixture in
// `tests/unit/test_qr_code_decoder_image.cpp`.
class QrCodeDecoderImagePeer;

class QrCodeDecoderImage {
public:
    // Strategy-injection hook for RS error correction. Returns true
    // on success. Default = use the built-in `QrCodeDecoderBits`. The
    // hook may inspect/mutate `qr.rawbits`, `qr.corrected`,
    // `qr.rsErrorLocations`, `qr.blockStatus`, etc.
    using RsCorrectFn = std::function<bool(QrCode& qr)>;

    // Strategy-injection hook for alignment-pattern localisation.
    // Returns true if all expected alignment patterns were found.
    // Default = use the built-in `QrCodeAlignmentPatternLocator`.
    using AlignmentLocatorFn = std::function<bool(const cv::Mat& gray, QrCode& qr)>;

    // Construction-time configuration — value-typed, reentrant per
    // CLAUDE.md "Public API design" line 31. Default-constructed Config
    // = use built-in RS + alignment locator. Once the detector is
    // constructed, configuration is immutable; tests that need to
    // swap strategies construct a new detector.
    struct Config {
        // Java's `forceEncoding` — when set, overrides byte-mode
        // encoding auto-detection.
        std::optional<std::string> forceEncoding;
        // Java's `defaultEncoding` — used when auto-detection finds no
        // ECI and the byte mode produces invalid UTF-8 / Latin-1.
        std::string defaultEncoding = "UTF-8";
        // Mirrors Java's public field — when true, decode() retries
        // with a transposed bit pattern if the first pass fails.
        bool considerTransposed = true;
        // Mirrors `ConfigQrCode.ignorePaddingBytes`. When true,
        // decode-time padding-byte verification is skipped (bug-
        // tolerant against encoders that emit non-spec padding). Java
        // QR profile defaults this to `true`.
        bool ignorePaddingBytes = false;
        // Default-constructed std::function = use the built-in
        // ReedSolomonCodes_U8 path.
        RsCorrectFn rs_decoder;
        // Default-constructed std::function = use the built-in
        // QrCodeAlignmentPatternLocator path.
        AlignmentLocatorFn alignment_locator;
    };

    // Default Config: built-in RS + alignment locator + UTF-8 default
    // encoding + considerTransposed=true.
    QrCodeDecoderImage();

    // Construct with a value-typed Config. Strategy injection sites
    // are honoured once at construction; the orchestrator stores the
    // hooks and the runtime no longer re-reads Config.
    explicit QrCodeDecoderImage(Config cfg);

    // ---- End-to-end entry — Java's `process()`. ----
    //
    // Runs the full pipeline on every PositionPatternNode triplet
    // walked from the `pps` graph. After this returns, `getSuccesses()`
    // and `getFailures()` reflect what was decoded.
    void process(const std::vector<PositionPatternNode>& pps,
                 const cv::Mat& gray);

    // ---- Stage-isolation public entry points (CLAUDE.md mandate). ----
    //
    // `find_finders` — given a PositionPatternNode graph, walk it and
    // emit the canonical-orientation finder triplets. No decoding.
    static std::vector<PositionPatternTriplet> find_finders(
        const std::vector<PositionPatternNode>& pps);

    // `detect_polygons_only` — runs binarize → polygon → finder →
    // version-estimation → alignment, then stops. `qr.version` IS
    // populated by `estimateVersionBySize` so the alignment locator
    // can run with the right number of expected patterns; format/RS/
    // mode-decode are NOT run. Alignment honours the injected
    // `alignment_locator` hook (so a clipped-QR fallback substitutes
    // here just as it does in `process()`). Returns a vector of QrCode
    // shells with `ppCorner`/`ppRight`/`ppDown`/`bounds`/`version`/
    // `alignment[]` populated and the message/rawbits/corrected fields
    // empty.
    PolygonOnlyResult detect_polygons_only(
        const std::vector<PositionPatternNode>& pps, const cv::Mat& gray);

    // `sample_bit_matrix` — sample the QR's bit matrix at a known
    // version + finder geometry. Pre-fills `qr.rawbits` with N bytes
    // where N = VERSION_INFO[version].codewords. Caller must have
    // already populated `qr.ppCorner` / `ppRight` / `ppDown`,
    // `qr.version`, `qr.error`, `qr.mask`, and the threshold fields.
    bool sample_bit_matrix(const cv::Mat& gray, QrCode& qr);

    // `extract_raw_codewords` — alias of `sample_bit_matrix` that also
    // mirrors the orchestrator's `qr.rawCodewords = qr.rawbits` step
    // so the public mandate field is populated.
    bool extract_raw_codewords(const cv::Mat& gray, QrCode& qr);

    // `rs_correct` — runs Reed-Solomon on `qr.rawbits` (or honours the
    // injected `rs_decoder` strategy). Populates `qr.corrected`,
    // `qr.totalBitErrors`, `qr.rsErrorLocations`, `qr.blockStatus`.
    bool rs_correct(QrCode& qr);

    // `decode_message` — runs the mode-dispatch payload extractor on
    // `qr.corrected`. Populates `qr.message`, `qr.byteEncoding`,
    // `qr.mode`. Returns false on hard mode-decode failure (with
    // `qr.failureCause` set).
    bool decode_message(QrCode& qr);

    // ---- Result accessors (const-only — CLAUDE.md "Public API design"
    // line 31: detector instances are reentrant; results are read-only
    // by external consumers). Callers that want to mutate the result
    // vector should copy.
    const std::vector<QrCode>& getSuccesses() const { return successes_; }
    const std::vector<QrCode>& getFailures() const { return failures_; }

    // Read-only flag mirroring the Config setting that's currently
    // active. (Set at ctor time via Config; runtime mutation is not
    // supported per the reentrant-Config contract.)
    bool getConsiderTransposed() const { return considerTransposed_; }

    // ---- TEST-VISIBLE static helpers (Java JUnit pokes these directly). ----
    static void setPositionPatterns(const PositionPatternNode& ppn,
                                    int32_t cornerToRight,
                                    int32_t cornerToDown,
                                    QrCode& qr);
    static void rotateUntilAt(std::array<cv::Point2d, 4>& square,
                              int32_t current, int32_t desired);
    static void computeBoundingBox(QrCode& qr);
    static void transposeCorners(std::array<cv::Point2d, 4>& c);

    // ---- TEST-VISIBLE non-static (Java JUnit pokes these directly). ----
    void transposePositionPatterns(QrCode& qr);
    bool extractFormatInfo(QrCode& qr);
    bool extractVersionInfo(QrCode& qr);
    int32_t estimateVersionBySize(QrCode& qr);
    int32_t decodeVersion();
    bool readFormatRegion0(QrCode& qr);
    bool readFormatRegion1(QrCode& qr);

    // Version-info read regions — Java has these as private; we expose
    // them so `extractVersionInfo`'s parity tests can poke at them.
    bool readVersionRegion0(QrCode& qr);
    bool readVersionRegion1(QrCode& qr);

    // Internal sub-module access is granted to the test peer only
    // (CLAUDE.md "Public API design" — owned types in public surface,
    // friend grant for test-only mutable access). The orchestrator
    // does not expose `getGridReader` / `getAlignmentLocator` /
    // `getDecoder` to general consumers.
    friend class QrCodeDecoderImagePeer;

private:
    bool decode(const cv::Mat& gray, QrCode& qr);
    bool readRawData(QrCode& qr);
    float readBitIntensityAndThresholdDownRight(
        QrCode& qr, const std::vector<Point2I>& locationBits);
    void bitIntensityToBitValue(QrCode& qr,
                                const std::vector<Point2I>& locationBits);
    void read(int32_t bit, int32_t row, int32_t col);

    // Run the alignment-locator step honouring the optional injected
    // hook (CLAUDE.md mandate — strategy injection at ctor time).
    bool runAlignmentLocator(const cv::Mat& gray, QrCode& qr);

    // Run the RS step honouring the optional injected hook.
    bool runRsCorrect(QrCode& qr);

    // ---- Sub-modules. ----
    QrCodeDecoderBits decoder_;
    QrCodeAlignmentPatternLocator alignmentLocator_;
    QrCodeBinaryGridReader gridReader_;

    // ---- Result storage. ----
    std::vector<QrCode> successes_;
    std::vector<QrCode> failures_;
    std::vector<QrCode> storageQR_;  // Java DogArray<QrCode> recycle pool

    // ---- Internal workspace. ----
    PackedBits8 bits_;
    cv::Point2d grid_{0.0, 0.0};
    std::array<cv::Point2d, 4> tempTranspose_{};
    std::vector<float> intensityBits_;

    // ---- Configuration captured at construction time. ----
    bool considerTransposed_ = true;
    RsCorrectFn rsHook_;
    AlignmentLocatorFn alignmentHook_;
};

}  // namespace boofcv_qr

#endif  // BOOFCV_QR_QR_CODE_DECODER_IMAGE_HPP
