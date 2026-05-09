// Partial port of boofcv.alg.fiducial.qrcode.QrCode (BoofCV v1.3.0).
//
// Step 4 contributes: ErrorLevel, BlockInfo, VersionInfo, the populated
// VERSION_INFO[] table, and the data-side runtime fields the
// QrCodeDecoderBits orchestrator needs (version, error, mask, rawbits,
// corrected, message, byteEncoding, failureCause, mode, totalBitErrors,
// bitsTransposed).
//
// Step 8 adds the `Alignment` inner struct and `alignment[]` field —
// populated by the alignment-pattern locator. The remaining geometry
// fields (`ppCorner`, `ppDown`, `ppRight`, `bounds`, `Hinv`) are
// deferred to step 9's orchestrator.
//
// Algorithm description: src/decoder/qr_code.md.

#ifndef BOOFCV_QR_QR_CODE_HPP
#define BOOFCV_QR_QR_CODE_HPP

#include "boofcv_qr/qr_mode.hpp"

#include <opencv2/core.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace boofcv_qr {

class QrCodeMaskPattern;  // fwd

// Error correction level. Bit values come from ISO 18004 Table 12.
// Note: the bit assignment (M=00, L=01, H=10, Q=11) is NOT in numeric
// order, hence the explicit values.
enum class ErrorLevel : int32_t {
    L = 0b01,
    M = 0b00,
    Q = 0b11,
    H = 0b10,
};

}  // namespace boofcv_qr

// Hash specialisation must precede the std::unordered_map<ErrorLevel,...>
// member definition below.
namespace std {
template <>
struct hash<boofcv_qr::ErrorLevel> {
    std::size_t operator()(boofcv_qr::ErrorLevel e) const noexcept {
        return std::hash<int32_t>{}(static_cast<int32_t>(e));
    }
};
}  // namespace std

namespace boofcv_qr {

inline int32_t ErrorLevel_value(ErrorLevel e) { return static_cast<int32_t>(e); }

// Returns the ErrorLevel for the given 2-bit value. Throws on unknown.
inline ErrorLevel ErrorLevel_lookup(int32_t value) {
    switch (value) {
        case 0b01: return ErrorLevel::L;
        case 0b00: return ErrorLevel::M;
        case 0b11: return ErrorLevel::Q;
        case 0b10: return ErrorLevel::H;
        default: throw std::invalid_argument("Unknown ErrorLevel value");
    }
}

// Number of codewords per RS block at this error correction level.
struct BlockInfo {
    int32_t codewords;
    int32_t dataCodewords;
    int32_t blocks;
};

// Per-version metadata. Populated once at static-init time from the
// table in ISO 18004 Table 9 + Table E.1.
class VersionInfo {
public:
    int32_t codewords = 0;
    std::vector<int32_t> alignment;
    std::unordered_map<ErrorLevel, BlockInfo> levels;

    VersionInfo() = default;
    VersionInfo(int32_t codewords_, std::vector<int32_t> alignment_)
        : codewords(codewords_), alignment(std::move(alignment_)) {}

    void add(ErrorLevel level, int32_t codeWords, int32_t dataCodewords,
             int32_t eccBlocks) {
        levels[level] = BlockInfo{codeWords, dataCodewords, eccBlocks};
    }

    // Total bytes in the data area at this error correction level.
    // Mirrors Java's totalDataBytes computation.
    int32_t totalDataBytes(ErrorLevel error) const;
};

// QR code result struct. Public mutable surface mirrors the Java original.
class QrCode {
public:
    // Alignment-pattern record. Populated by the alignment-pattern
    // locator (step 8) — one entry per alignment pattern that the
    // locator was able to find for this QR. `moduleX`/`moduleY` are
    // the expected grid coordinates from `VERSION_INFO[v].alignment`;
    // `moduleFound` is the refined sub-module grid coord; `pixel` is
    // the corresponding image-pixel position; `threshold` is the local
    // gray threshold the locator picked.
    struct Alignment {
        cv::Point2d pixel{0.0, 0.0};
        int32_t moduleX = 0;
        int32_t moduleY = 0;
        cv::Point2d moduleFound{0.0, 0.0};
        double threshold = 0.0;

        void reset() {
            pixel = cv::Point2d(0.0, 0.0);
            moduleX = 0;
            moduleY = 0;
            moduleFound = cv::Point2d(0.0, 0.0);
            threshold = 0.0;
        }
    };

    // Mask applied to format information when encoding (ISO 18004 §7.9).
    static constexpr int32_t FORMAT_MASK = 0b101010000010010;

    // Maximum supported version.
    static constexpr int32_t MAX_VERSION = 40;

    // Versions 7+ have explicit version-info modules; 1..6 don't.
    static constexpr int32_t VERSION_ENCODED_AT = 7;

    // Per-version table — index 0 unused, 1..40 populated. Returns a
    // const reference to the static-initialised array.
    static const std::array<VersionInfo, MAX_VERSION + 1>& VERSION_INFO();

    // ---- Runtime-populated fields ----
    int32_t version = -1;
    ErrorLevel error = ErrorLevel::L;
    // Non-owning pointer to one of the QrCodeMaskPattern singletons.
    const QrCodeMaskPattern* mask = nullptr;
    Mode mode = Mode::UNKNOWN;
    std::string byteEncoding;

    std::vector<std::uint8_t> rawbits;
    std::vector<std::uint8_t> corrected;
    std::string message;

    Failure failureCause = Failure::NONE;
    int32_t totalBitErrors = 0;
    bool bitsTransposed = false;

    // Local thresholds used by the binarization layer; populated by
    // the position-pattern detector in step 7. Carried here so the
    // public result struct keeps Java's surface.
    double threshCorner = 0.0;
    double threshDown = 0.0;
    double threshRight = 0.0;
    double threshDownRight = 0.0;

    // Alignment patterns located by step 8 (`QrCodeAlignmentPatternLocator`).
    // Empty for v1 (no alignment patterns); for v2+ this is populated
    // with one entry per non-corner-overlapping cell in
    // `VERSION_INFO[v].alignment` × `alignment` after the locator
    // succeeds. Java's `DogArray<Alignment>` → `std::vector<Alignment>`
    // (value-typed; `// TODO(perf): recycle`).
    std::vector<Alignment> alignment;

    QrCode() { reset(); }

    int32_t getNumberOfModules() const { return totalModules(version); }
    int32_t getNumberOfDataBytes() const {
        return VERSION_INFO()[static_cast<std::size_t>(version)]
            .totalDataBytes(error);
    }
    static int32_t totalModules(int32_t v) { return v * 4 + 17; }

    // Reset to the initial post-construction state.
    void reset();
};

}  // namespace boofcv_qr

#endif  // BOOFCV_QR_QR_CODE_HPP
