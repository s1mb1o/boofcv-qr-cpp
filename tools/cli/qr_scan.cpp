// Step 9b CLI binary — runs the full C++-port detection pipeline
// (binarize -> polygon -> finder -> alignment -> orchestrator) on every
// image under <input_dir> and emits per-image detection JSON in the
// same shape as `tools/java_reference/Baseline.java`. The output can
// then be scored by `tests/regression/score.py` against the locked
// `tests/baseline.json`.
//
// CLAUDE.md says CLI may use `<filesystem>` and other host-OS bits
// (the core lib must not). We do, here.
//
// Usage:
//   qr_scan <input_dir> <output_dir>     # batch mode; mirrors dataset
//   qr_scan <single_image.png>           # single-image; emits JSON to stdout
//   qr_scan --stage-timings <input_dir> <output_dir>  # batch + sidecar timing report
//   QR_SCAN_THREADS=N qr_scan <input_dir> <output_dir>  # pin batch workers
//
// JSON is emitted with a small hand-written serialiser — pulling in
// nlohmann/json for one CLI binary isn't worth it. The shape matches
// Java line-for-line so score.py works on either side.

#include "boofcv_qr/finder/qr_code_position_pattern_detector.hpp"
#include "boofcv_qr/finder/qr_code_position_pattern_graph_generator.hpp"
#include "boofcv_qr/polygon/detect_polygon_binary_gray_refine.hpp"
#include "boofcv_qr/polygon/detect_polygon_from_contour.hpp"
#include "boofcv_qr/polygon/refine_polygon_to_gray.hpp"
#include "boofcv_qr/polyline/polyline_split_merge.hpp"
#include "boofcv_qr/qr_code_decoder_image.hpp"
#include "boofcv_qr/qr_code_mask_pattern.hpp"
#include "boofcv_qr/threshold_block_otsu.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------------
// Minimal JSON writer. The output is compact-ish and matches what
// score.py expects — the Java reference uses Jackson with
// INDENT_OUTPUT, but score.py doesn't care about formatting; we emit
// pretty JSON for human readability.
// ---------------------------------------------------------------------

class JsonWriter {
public:
    explicit JsonWriter(std::ostream& os) : os_(os) {}

    void writeRecord(const std::string& json) { os_ << json; }

    static std::string escape(const std::string& s) {
        std::string out;
        out.reserve(s.size() + 2);
        for (char c : s) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out += buf;
                    } else {
                        out += c;
                    }
            }
        }
        return out;
    }

    static std::string num(double v) {
        // Match Jackson's default doubles: drop trailing zeros for
        // integers, otherwise standard scientific. Use std::ostringstream
        // with default precision so 0.5 → "0.5" and 5.358750000000001 →
        // "5.358750000000001". We can't perfectly mimic Java's double
        // printer but score.py only reads back as float so that's fine.
        std::ostringstream oss;
        oss.precision(17);
        oss << v;
        return oss.str();
    }

    static std::string num(int32_t v) { return std::to_string(v); }
    static std::string num(int64_t v) { return std::to_string(v); }

private:
    std::ostream& os_;
};

// ---------------------------------------------------------------------
// Sidecar `.txt` ground-truth parser. Mirrors Java's `parseGroundTruth`
// in `tools/java_reference/Baseline.java` — handles both `SETS`-style
// coord blocks and plain-payload-only files (decoding subset).
// ---------------------------------------------------------------------

struct GroundTruth {
    std::vector<std::array<cv::Point2d, 4>> corners;
    std::string message;            // empty if no MESSAGE block
    bool hasMessage = false;        // true for decoding-subset payloads
};

GroundTruth parseGroundTruth(const fs::path& imagePath) {
    GroundTruth out;
    fs::path txt = imagePath;
    txt.replace_extension(".txt");
    if (!fs::is_regular_file(txt)) return out;

    std::ifstream in(txt);
    if (!in.is_open()) return out;

    std::vector<double> nums;
    bool inMessage = false;
    bool sawMessageKeyword = false;
    std::string msgBuf;
    std::string fallbackBuf;
    bool coordRegionHadNonNumeric = false;

    auto trim = [](std::string& s) {
        std::size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) { s.clear(); return; }
        std::size_t b = s.find_last_not_of(" \t\r\n");
        s = s.substr(a, b - a + 1);
    };
    auto upper = [](std::string s) {
        for (char& c : s)
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        return s;
    };

    std::string line;
    while (std::getline(in, line)) {
        trim(line);
        if (line.empty()) continue;
        if (line[0] == '#') continue;
        std::string up = upper(line);

        if (up == "SETS") { inMessage = false; continue; }
        if (up == "MESSAGE") {
            inMessage = true;
            sawMessageKeyword = true;
            msgBuf.clear();
            continue;
        }
        if (up == "EOF" || up == "END") { inMessage = false; continue; }

        if (inMessage) {
            if (!msgBuf.empty()) msgBuf += '\n';
            msgBuf += line;
            continue;
        }

        if (!fallbackBuf.empty()) fallbackBuf += '\n';
        fallbackBuf += line;

        std::istringstream iss(line);
        std::string tok;
        while (iss >> tok) {
            try {
                std::size_t pos;
                double d = std::stod(tok, &pos);
                if (pos == tok.size()) {
                    nums.push_back(d);
                    continue;
                }
            } catch (...) {}
            coordRegionHadNonNumeric = true;
        }
    }

    // Decoding-subset fallback: plain-text .txt with the expected payload.
    if (!sawMessageKeyword && nums.size() < 8 && coordRegionHadNonNumeric &&
        !fallbackBuf.empty()) {
        msgBuf = fallbackBuf;
        nums.clear();
    }

    int32_t complete = static_cast<int32_t>(nums.size() / 8);
    out.corners.reserve(static_cast<std::size_t>(complete));
    for (int32_t q = 0; q < complete; q++) {
        std::array<cv::Point2d, 4> c;
        for (int32_t i = 0; i < 4; i++) {
            c[static_cast<std::size_t>(i)] = cv::Point2d(
                nums[static_cast<std::size_t>(q * 8 + i * 2)],
                nums[static_cast<std::size_t>(q * 8 + i * 2 + 1)]);
        }
        out.corners.push_back(c);
    }
    if (!msgBuf.empty()) {
        out.message = msgBuf;
        out.hasMessage = true;
    }
    return out;
}

// ---------------------------------------------------------------------
// Detection pipeline factory. Mirrors the construction pattern used in
// `tests/unit/test_qr_code_position_pattern_detector.cpp:createAlg()`
// + the orchestrator. Default-constructed.
// ---------------------------------------------------------------------

// Build the QR-profile orchestrator config — mirrors Java's
// `ConfigQrCode` decoder-side defaults (lines 62, 75 of upstream
// ConfigQrCode.java).
boofcv_qr::QrCodeDecoderImage::Config makeQrConfig() {
    boofcv_qr::QrCodeDecoderImage::Config cfg;
    // ConfigQrCode.java:62: `defaultEncoding = EciEncoding.ISO8859_1`.
    // The general decoder default is "UTF-8"; QR profile uses
    // ISO-8859-1 so byte-mode payloads with raw 0x80–0xFF bytes
    // (legitimate per ISO 18004 §7.4.5) don't fail UTF-8 validation.
    cfg.defaultEncoding = "ISO-8859-1";
    // ConfigQrCode.java:75: `ignorePaddingBytes = true`. Encoders
    // commonly emit non-spec padding patterns; relax the check.
    cfg.ignorePaddingBytes = true;
    return cfg;
}

struct StageTiming {
    double totalMs = 0.0;
    double binarizeMs = 0.0;
    double finderTotalMs = 0.0;
    double contourPolygonMs = 0.0;
    double finderValidationMs = 0.0;
    double graphMs = 0.0;
    double orchestratorMs = 0.0;
    boofcv_qr::QrCodeDecoderImageTiming decoder;

    int32_t polygons = 0;
    int32_t positionPatterns = 0;
    int32_t detections = 0;
    int32_t failures = 0;
};

double elapsedMs(std::chrono::steady_clock::time_point start,
                 std::chrono::steady_clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

struct Pipeline {
    boofcv_qr::ThresholdBlockOtsu binarizer;
    std::unique_ptr<boofcv_qr::QrCodePositionPatternDetector> finder;
    boofcv_qr::QrCodePositionPatternGraphGenerator graphGen{40};
    boofcv_qr::QrCodeDecoderImage orchestrator{makeQrConfig()};

    cv::Mat binary;  // reused buffer

    Pipeline() {
        // Mirror Java's `ConfigQrCode` polygon-detector tunings (lines
        // 95–96 of upstream ConfigQrCode.java). The general
        // ConfigPolylineSplitMerge defaults give cornerScorePenalty=0.025
        // and maxSideError=relative(0.05,3); QR overrides to 0.4 and
        // relative(0.12,3) respectively. The 16× cornerScorePenalty
        // bump is the prime mover for the polygon-cluster residuals
        // (blurred / pathological): it makes 5+ corner candidates
        // collapse to 4-corner finder polygons, where the more lenient
        // default lets too many over-cornered candidates through and
        // they get filtered later → false negatives.
        boofcv_qr::ConfigPolylineSplitMerge polyCfg;
        polyCfg.minimumSides = 4;
        polyCfg.maximumSides = 4;
        polyCfg.cornerScorePenalty = 0.4;
        polyCfg.maxSideError = boofcv_qr::ConfigLength::relative(0.12, 3.0);
        // minimumSideLength = 2 already matches our ConfigPolylineSplitMerge
        // default (and Java's ConfigPolylineSplitMerge default), but
        // ConfigQrCode.java:97 sets it explicitly so we mirror.
        polyCfg.minimumSideLength = 2;
        auto adapter = std::make_unique<boofcv_qr::PolylineSplitMergeAdapter>(polyCfg);

        auto contour = std::make_unique<boofcv_qr::DetectPolygonFromContour>(
            std::move(adapter), /*outputClockwiseUpY=*/true,
            /*canTouchBorder=*/false,
            /*contourEdgeThreshold=*/3.0,
            /*tangentEdgeIntensity=*/1.5);
        contour->setNumberOfSides(4, 4);
        // Mirror Java's `ConfigQrCode` defaults (line 100 of upstream
        // ConfigQrCode.java): `polygon.detector.minimumContour =
        // ConfigLength.fixed(40)`. The class default is
        // `ConfigLength::relative(0.044, 4.0)` which on a 4032×3024
        // image computes ~154 px → minimumArea ≈ 1482 sq px → all
        // small finders rejected. The fixed-40 cap (Java's QR setting)
        // gives minimumArea = (40/4)² = 100 sq px, accepting tiny
        // finders. This is the load-bearing fix for the lots/image005-
        // 007 zero-detection mode.
        contour->setMinimumContour(boofcv_qr::ConfigLength::fixed(40.0));

        boofcv_qr::ConfigRefinePolygonLineToImage refineCfg;
        auto refine = std::make_shared<boofcv_qr::RefinePolygonToGrayLine>(refineCfg);

        // Mirror Java's `ConfigQrCode` line 103 of upstream:
        // `polygon.minimumRefineEdgeIntensity = 6`.
        auto wrapper = std::make_shared<boofcv_qr::DetectPolygonBinaryGrayRefine>(
            std::move(contour), std::move(refine),
            /*minimumRefineEdgeIntensity=*/6.0,
            /*adjustForThresholdBias=*/true);

        finder = std::make_unique<boofcv_qr::QrCodePositionPatternDetector>(std::move(wrapper));
    }

    // Run binarize → finder → graph → orchestrator on a CV_8UC1 image.
    // Populates `orchestrator.getSuccesses() / getFailures()`.
    void run(const cv::Mat& gray) {
        runTimed(gray, nullptr);
    }

    void runTimed(const cv::Mat& gray, StageTiming* timing) {
        auto totalStart = std::chrono::steady_clock::now();
        auto t0 = totalStart;

        binarizer.process(gray, binary);
        auto t1 = std::chrono::steady_clock::now();
        if (timing)
            timing->binarizeMs = elapsedMs(t0, t1);

        t0 = std::chrono::steady_clock::now();
        finder->process(gray, binary);
        t1 = std::chrono::steady_clock::now();
        if (timing) {
            timing->finderTotalMs = elapsedMs(t0, t1);
            timing->contourPolygonMs = finder->getLastContourPolygonMS();
            timing->finderValidationMs = finder->getLastFinderValidationMS();
            timing->polygons = static_cast<int32_t>(
                finder->getSquareDetector().getPolygonInfo().size());
            timing->positionPatterns = static_cast<int32_t>(
                finder->getPositionPatterns().size());
        }

        // The graph generator mutates the position-pattern node list in place;
        // we cast away const via the friend grant pattern used elsewhere.
        auto& positions = const_cast<std::vector<boofcv_qr::PositionPatternNode>&>(
            finder->getPositionPatterns());
        t0 = std::chrono::steady_clock::now();
        graphGen.process(positions);
        t1 = std::chrono::steady_clock::now();
        if (timing)
            timing->graphMs = elapsedMs(t0, t1);

        t0 = std::chrono::steady_clock::now();
        if (timing) {
            orchestrator.process(positions, gray, &timing->decoder);
        } else {
            orchestrator.process(positions, gray);
        }
        t1 = std::chrono::steady_clock::now();
        if (timing) {
            timing->orchestratorMs = elapsedMs(t0, t1);
            timing->detections = static_cast<int32_t>(orchestrator.getSuccesses().size());
            timing->failures = static_cast<int32_t>(orchestrator.getFailures().size());
            timing->totalMs = elapsedMs(totalStart, std::chrono::steady_clock::now());
        }
    }
};

// ---------------------------------------------------------------------
// Per-image record serialisation. Output schema mirrors Java's
// `Baseline.processOne(...)` byte-for-byte (modulo trailing whitespace).
// ---------------------------------------------------------------------

const char* errorLevelName(boofcv_qr::ErrorLevel e) {
    switch (e) {
        case boofcv_qr::ErrorLevel::L: return "L";
        case boofcv_qr::ErrorLevel::M: return "M";
        case boofcv_qr::ErrorLevel::Q: return "Q";
        case boofcv_qr::ErrorLevel::H: return "H";
    }
    return "?";
}

const char* modeName(boofcv_qr::Mode m) {
    switch (m) {
        case boofcv_qr::Mode::UNKNOWN: return "UNKNOWN";
        case boofcv_qr::Mode::MIXED: return "MIXED";
        case boofcv_qr::Mode::NUMERIC: return "NUMERIC";
        case boofcv_qr::Mode::ALPHANUMERIC: return "ALPHANUMERIC";
        case boofcv_qr::Mode::BYTE: return "BYTE";
        case boofcv_qr::Mode::KANJI: return "KANJI";
        case boofcv_qr::Mode::ECI: return "ECI";
        case boofcv_qr::Mode::STRUCTURE_APPENDED: return "STRUCTURE_APPENDED";
        case boofcv_qr::Mode::FNC1_FIRST: return "FNC1_FIRST";
        case boofcv_qr::Mode::FNC1_SECOND: return "FNC1_SECOND";
    }
    return "?";
}

const char* failureName(boofcv_qr::Failure f) {
    switch (f) {
        case boofcv_qr::Failure::NONE: return "NONE";
        case boofcv_qr::Failure::FORMAT: return "FORMAT";
        case boofcv_qr::Failure::VERSION: return "VERSION";
        case boofcv_qr::Failure::ALIGNMENT: return "ALIGNMENT";
        case boofcv_qr::Failure::READING_BITS: return "READING_BITS";
        case boofcv_qr::Failure::ERROR_CORRECTION: return "ERROR_CORRECTION";
        case boofcv_qr::Failure::UNKNOWN_MODE: return "UNKNOWN_MODE";
        case boofcv_qr::Failure::READING_PADDING: return "READING_PADDING";
        case boofcv_qr::Failure::MESSAGE_OVERFLOW: return "MESSAGE_OVERFLOW";
        case boofcv_qr::Failure::DECODING_MESSAGE: return "DECODING_MESSAGE";
        case boofcv_qr::Failure::KANJI_UNAVAILABLE: return "KANJI_UNAVAILABLE";
        case boofcv_qr::Failure::STRING_ENCODING_UNAVAILABLE:
            return "STRING_ENCODING_UNAVAILABLE";
    }
    return "?";
}

// Map QrCodeMaskPattern* → "M000".."M111" or "null".
std::string maskName(const boofcv_qr::QrCodeMaskPattern* m) {
    if (m == nullptr) return "null";
    using boofcv_qr::QrCodeMaskPattern;
    if (m == &QrCodeMaskPattern::M000()) return "M000";
    if (m == &QrCodeMaskPattern::M001()) return "M001";
    if (m == &QrCodeMaskPattern::M010()) return "M010";
    if (m == &QrCodeMaskPattern::M011()) return "M011";
    if (m == &QrCodeMaskPattern::M100()) return "M100";
    if (m == &QrCodeMaskPattern::M101()) return "M101";
    if (m == &QrCodeMaskPattern::M110()) return "M110";
    if (m == &QrCodeMaskPattern::M111()) return "M111";
    return "?";
}

std::string polygonJson(const std::array<cv::Point2d, 4>& poly) {
    std::ostringstream oss;
    oss << "[ ";
    for (std::size_t i = 0; i < 4; i++) {
        if (i > 0) oss << ", ";
        oss << "[ " << JsonWriter::num(poly[i].x) << ", "
            << JsonWriter::num(poly[i].y) << " ]";
    }
    oss << " ]";
    return oss.str();
}

std::string detectionJson(const boofcv_qr::QrCode& qr) {
    std::ostringstream oss;
    oss << "{\n";
    oss << "    \"corners\" : " << polygonJson(qr.bounds) << ",\n";
    oss << "    \"pp_corner\" : " << polygonJson(qr.ppCorner) << ",\n";
    oss << "    \"pp_right\" : " << polygonJson(qr.ppRight) << ",\n";
    oss << "    \"pp_down\" : " << polygonJson(qr.ppDown) << ",\n";
    oss << "    \"message\" : \"" << JsonWriter::escape(qr.message) << "\",\n";
    oss << "    \"byte_encoding\" : \"" << JsonWriter::escape(qr.byteEncoding) << "\",\n";
    oss << "    \"version\" : " << JsonWriter::num(qr.version) << ",\n";
    oss << "    \"total_bit_errors\" : " << JsonWriter::num(qr.totalBitErrors) << ",\n";
    oss << "    \"error_correction\" : \"" << errorLevelName(qr.error) << "\",\n";
    oss << "    \"mode\" : \"" << modeName(qr.mode) << "\",\n";
    oss << "    \"mask\" : \"" << maskName(qr.mask) << "\",\n";
    oss << "    \"failure_cause\" : \"" << failureName(qr.failureCause) << "\"\n";
    oss << "  }";
    return oss.str();
}

std::string groundTruthJson(const GroundTruth& gt) {
    std::ostringstream oss;
    oss << "[";
    bool first = true;
    bool wroteAny = false;
    auto emitObjectStart = [&]() {
        if (!first) oss << ", ";
        first = false;
        wroteAny = true;
        oss << "{\n";
    };
    if (!gt.corners.empty()) {
        for (std::size_t q = 0; q < gt.corners.size(); q++) {
            emitObjectStart();
            oss << "    \"corners\" : " << polygonJson(gt.corners[q]);
            if (q == 0 && gt.hasMessage) {
                oss << ",\n    \"message\" : \"" << JsonWriter::escape(gt.message) << "\"";
            }
            oss << "\n  }";
        }
    } else if (gt.hasMessage) {
        emitObjectStart();
        oss << "    \"message\" : \"" << JsonWriter::escape(gt.message) << "\"\n  }";
    }
    if (!wroteAny) {
        oss << "]";
        return oss.str();
    }
    oss << " ]";
    return oss.str();
}

struct Record {
    std::string imagePath;          // forward-slash relative path
    std::string subset;
    std::string category;
    GroundTruth gt;
    int32_t imageWidth = 0;
    int32_t imageHeight = 0;
    double elapsedMs = 0.0;
    bool loadFailed = false;
    bool hasTiming = false;
    StageTiming timing;
    std::vector<boofcv_qr::QrCode> detections;
    std::vector<boofcv_qr::QrCode> failures;
};

std::string recordJson(const Record& rec) {
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"image_path\" : \"" << JsonWriter::escape(rec.imagePath) << "\",\n";
    oss << "  \"subset\" : \"" << JsonWriter::escape(rec.subset) << "\",\n";
    oss << "  \"category\" : \"" << JsonWriter::escape(rec.category) << "\",\n";
    oss << "  \"ground_truth\" : " << groundTruthJson(rec.gt);
    if (rec.loadFailed) {
        oss << ",\n  \"error\" : \"load_failed\",\n";
        oss << "  \"detections\" : [],\n";
        oss << "  \"failures\" : []\n";
        oss << "}";
        return oss.str();
    }
    oss << ",\n  \"image_width\" : " << JsonWriter::num(rec.imageWidth) << ",\n";
    oss << "  \"image_height\" : " << JsonWriter::num(rec.imageHeight) << ",\n";
    oss << "  \"elapsed_ms\" : " << JsonWriter::num(rec.elapsedMs) << ",\n";
    oss << "  \"detections\" : [";
    if (!rec.detections.empty()) {
        oss << " ";
        for (std::size_t i = 0; i < rec.detections.size(); i++) {
            if (i > 0) oss << ", ";
            oss << detectionJson(rec.detections[i]);
        }
        oss << " ";
    }
    oss << "],\n";
    oss << "  \"failures\" : [";
    if (!rec.failures.empty()) {
        oss << " ";
        for (std::size_t i = 0; i < rec.failures.size(); i++) {
            if (i > 0) oss << ", ";
            oss << detectionJson(rec.failures[i]);
        }
        oss << " ";
    }
    oss << "]\n";
    oss << "}";
    return oss.str();
}

// ---------------------------------------------------------------------
// Greyscale image loader. Mirrors BoofCV's `UtilImageIO.loadImage(path,
// GrayU8.class)` which uses Java's ImageIO directly without applying
// EXIF orientation metadata — i.e. the loaded buffer matches the
// raw-pixel orientation of the file, NOT the EXIF "display" rotation.
//
// `cv::imread` defaults to applying the EXIF Orientation tag, which on
// camera JPEGs auto-rotates the buffer 90°/180°/270° relative to what
// BoofCV sees. The QR detector works on either orientation, but the
// reported corner coordinates then disagree with the ground truth (and
// with Java's detection coords) — IoU matching fails on every camera-
// shot image in the qrcodes_v3 dataset (24 images including all 7
// `lots/` images, ~33pp aggregate impact).
//
// `cv::IMREAD_IGNORE_ORIENTATION` disables the auto-rotation so we see
// the same pixel buffer Java does. Per CLAUDE.md "Replace with OpenCV"
// policy, image I/O remains `cv::imread`; only the flag changes.
cv::Mat loadGray(const fs::path& imagePath) {
    return cv::imread(imagePath.string(),
                       cv::IMREAD_GRAYSCALE | cv::IMREAD_IGNORE_ORIENTATION);
}

// ---------------------------------------------------------------------
// Filesystem walk: collect all .jpg/.jpeg/.png images under root,
// sorted lexicographically (matches Java's Files.walk + sorted()).
// ---------------------------------------------------------------------

bool isImage(const fs::path& p) {
    std::string ext = p.extension().string();
    for (char& c : ext) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return ext == ".jpg" || ext == ".jpeg" || ext == ".png";
}

std::vector<fs::path> collectImages(const fs::path& root) {
    std::vector<fs::path> out;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && isImage(entry.path())) {
            out.push_back(entry.path());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

int32_t chooseBatchThreads(std::size_t imageCount) {
    if (imageCount == 0)
        return 1;

    int32_t requested = 0;
    if (const char* env = std::getenv("QR_SCAN_THREADS")) {
        requested = std::atoi(env);
    }
    if (requested <= 0) {
        unsigned int hw = std::thread::hardware_concurrency();
        requested = hw == 0 ? 1 : static_cast<int32_t>(hw);
    }
    int32_t maxThreads = static_cast<int32_t>(
        std::min<std::size_t>(imageCount, static_cast<std::size_t>(requested)));
    return std::max(1, maxThreads);
}

// ---------------------------------------------------------------------
// Run the pipeline on one image and populate `rec`. Failures (load
// failure, decode exhaustion) are recorded in the JSON, never thrown.
// ---------------------------------------------------------------------

void processOne(Pipeline& pipe, const fs::path& imagePath,
                const fs::path& datasetRoot, Record& rec,
                bool collectTimings) {
    fs::path rel = fs::relative(imagePath, datasetRoot);
    std::string relStr = rel.generic_string();
    rec.imagePath = relStr;

    // Match Java's category/subset extraction from path segments.
    std::vector<std::string> segs;
    {
        std::istringstream iss(relStr);
        std::string s;
        while (std::getline(iss, s, '/')) segs.push_back(s);
    }
    rec.category = segs.size() >= 2 ? segs[segs.size() - 2] : "";
    rec.subset = segs.size() >= 3 ? segs[segs.size() - 3] : "";

    rec.gt = parseGroundTruth(imagePath);

    cv::Mat gray = loadGray(imagePath);
    if (gray.empty()) {
        rec.loadFailed = true;
        return;
    }
    rec.imageWidth = gray.cols;
    rec.imageHeight = gray.rows;

    auto t0 = std::chrono::steady_clock::now();
    try {
        if (collectTimings) {
            rec.hasTiming = true;
            pipe.runTimed(gray, &rec.timing);
        } else {
            pipe.run(gray);
        }
        rec.detections = pipe.orchestrator.getSuccesses();
        rec.failures = pipe.orchestrator.getFailures();
    } catch (const std::exception& e) {
        // Bubble decode-stage exceptions up as failures for parity with
        // Java's stack trace; we surface as empty detections + a
        // failure entry would require a synthetic QrCode, so just
        // log to stderr for now.
        std::fprintf(stderr, "Error processing %s: %s\n", relStr.c_str(), e.what());
        rec.detections.clear();
        rec.failures.clear();
    }
    auto t1 = std::chrono::steady_clock::now();
    rec.elapsedMs =
        std::chrono::duration<double, std::milli>(t1 - t0).count();
}

bool envFlag(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr)
        return false;
    std::string s(value);
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return !(s.empty() || s == "0" || s == "false" ||
             s == "off" || s == "no");
}

double nonNegative(double value) {
    return value < 0.0 ? 0.0 : value;
}

std::string sizeBucket(const Record& rec) {
    int64_t pixels = static_cast<int64_t>(rec.imageWidth) *
                     static_cast<int64_t>(rec.imageHeight);
    if (pixels < 500000)
        return "lt_0_5mp";
    if (pixels < 2000000)
        return "0_5_to_2mp";
    if (pixels < 8000000)
        return "2_to_8mp";
    return "ge_8mp";
}

struct TimingAggregate {
    int64_t images = 0;
    int64_t pixels = 0;
    int64_t polygons = 0;
    int64_t positionPatterns = 0;
    int64_t detections = 0;
    int64_t failures = 0;
    int64_t candidates = 0;
    int64_t decodeAttempts = 0;
    int64_t transposedAttempts = 0;
    int64_t samplingAttempts = 0;
    int64_t rsAttempts = 0;
    int64_t messageAttempts = 0;

    double totalMs = 0.0;
    double binarizeMs = 0.0;
    double finderTotalMs = 0.0;
    double contourPolygonMs = 0.0;
    double finderValidationMs = 0.0;
    double graphMs = 0.0;
    double orchestratorMs = 0.0;
    double formatMs = 0.0;
    double versionMs = 0.0;
    double alignmentMs = 0.0;
    double transformMs = 0.0;
    double samplingMs = 0.0;
    double rsMs = 0.0;
    double messageMs = 0.0;

    void add(const Record& rec) {
        if (!rec.hasTiming || rec.loadFailed)
            return;
        const StageTiming& t = rec.timing;
        images++;
        pixels += static_cast<int64_t>(rec.imageWidth) *
                  static_cast<int64_t>(rec.imageHeight);
        polygons += t.polygons;
        positionPatterns += t.positionPatterns;
        detections += t.detections;
        failures += t.failures;
        candidates += t.decoder.candidates;
        decodeAttempts += t.decoder.decodeAttempts;
        transposedAttempts += t.decoder.transposedAttempts;
        samplingAttempts += t.decoder.samplingAttempts;
        rsAttempts += t.decoder.rsAttempts;
        messageAttempts += t.decoder.messageAttempts;

        totalMs += t.totalMs;
        binarizeMs += t.binarizeMs;
        finderTotalMs += t.finderTotalMs;
        contourPolygonMs += t.contourPolygonMs;
        finderValidationMs += t.finderValidationMs;
        graphMs += t.graphMs;
        orchestratorMs += t.orchestratorMs;
        formatMs += t.decoder.formatMs;
        versionMs += t.decoder.versionMs;
        alignmentMs += t.decoder.alignmentMs;
        transformMs += t.decoder.transformMs;
        samplingMs += t.decoder.samplingMs;
        rsMs += t.decoder.rsMs;
        messageMs += t.decoder.messageMs;
    }

    double finderOverheadMs() const {
        return nonNegative(finderTotalMs - contourPolygonMs - finderValidationMs);
    }

    double decoderKnownMs() const {
        return formatMs + versionMs + alignmentMs + transformMs +
               samplingMs + rsMs + messageMs;
    }

    double decoderOtherMs() const {
        return nonNegative(orchestratorMs - decoderKnownMs());
    }

    double pipelineOverheadMs() const {
        return nonNegative(totalMs - binarizeMs - finderTotalMs -
                           graphMs - orchestratorMs);
    }
};

std::vector<std::pair<std::string, double>> additiveStageSums(
    const TimingAggregate& agg) {
    return {
        {"binarization", agg.binarizeMs},
        {"contour_polygon", agg.contourPolygonMs},
        {"finder_validation", agg.finderValidationMs},
        {"finder_overhead", agg.finderOverheadMs()},
        {"graph", agg.graphMs},
        {"decoder_format", agg.formatMs},
        {"decoder_version", agg.versionMs},
        {"decoder_alignment", agg.alignmentMs},
        {"decoder_transform", agg.transformMs},
        {"decoder_sampling", agg.samplingMs},
        {"decoder_rs", agg.rsMs},
        {"decoder_message", agg.messageMs},
        {"decoder_other", agg.decoderOtherMs()},
        {"pipeline_overhead", agg.pipelineOverheadMs()},
    };
}

std::vector<std::pair<std::string, double>> topBottlenecks(
    const TimingAggregate& agg, int32_t count) {
    auto stages = additiveStageSums(agg);
    std::sort(stages.begin(), stages.end(),
              [](const auto& a, const auto& b) {
                  return a.second > b.second;
              });
    if (static_cast<int32_t>(stages.size()) > count)
        stages.resize(static_cast<std::size_t>(count));
    return stages;
}

void writeStageMap(std::ostream& out, const TimingAggregate& agg,
                   const std::string& indent, bool mean) {
    auto emit = [&](const char* name, double value, bool last) {
        double v = mean && agg.images > 0
            ? value / static_cast<double>(agg.images)
            : value;
        out << indent << "  \"" << name << "\" : " << JsonWriter::num(v);
        out << (last ? "\n" : ",\n");
    };
    out << indent << "{\n";
    emit("pipeline_total", agg.totalMs, false);
    emit("binarization", agg.binarizeMs, false);
    emit("finder_total", agg.finderTotalMs, false);
    emit("contour_polygon", agg.contourPolygonMs, false);
    emit("finder_validation", agg.finderValidationMs, false);
    emit("finder_overhead", agg.finderOverheadMs(), false);
    emit("graph", agg.graphMs, false);
    emit("orchestrator_total", agg.orchestratorMs, false);
    emit("decoder_format", agg.formatMs, false);
    emit("decoder_version", agg.versionMs, false);
    emit("decoder_alignment", agg.alignmentMs, false);
    emit("decoder_transform", agg.transformMs, false);
    emit("decoder_sampling", agg.samplingMs, false);
    emit("decoder_rs", agg.rsMs, false);
    emit("decoder_message", agg.messageMs, false);
    emit("decoder_other", agg.decoderOtherMs(), false);
    emit("pipeline_overhead", agg.pipelineOverheadMs(), true);
    out << indent << "}";
}

void writeAggregate(std::ostream& out, const TimingAggregate& agg,
                    const std::string& indent) {
    double meanMegapixels = agg.images > 0
        ? static_cast<double>(agg.pixels) / static_cast<double>(agg.images) / 1000000.0
        : 0.0;
    out << indent << "{\n";
    out << indent << "  \"images\" : " << JsonWriter::num(agg.images) << ",\n";
    out << indent << "  \"mean_megapixels\" : "
        << JsonWriter::num(meanMegapixels) << ",\n";
    out << indent << "  \"counters\" : {\n";
    out << indent << "    \"polygons\" : " << JsonWriter::num(agg.polygons) << ",\n";
    out << indent << "    \"position_patterns\" : "
        << JsonWriter::num(agg.positionPatterns) << ",\n";
    out << indent << "    \"decoder_candidates\" : "
        << JsonWriter::num(agg.candidates) << ",\n";
    out << indent << "    \"decode_attempts\" : "
        << JsonWriter::num(agg.decodeAttempts) << ",\n";
    out << indent << "    \"transposed_attempts\" : "
        << JsonWriter::num(agg.transposedAttempts) << ",\n";
    out << indent << "    \"sampling_attempts\" : "
        << JsonWriter::num(agg.samplingAttempts) << ",\n";
    out << indent << "    \"rs_attempts\" : "
        << JsonWriter::num(agg.rsAttempts) << ",\n";
    out << indent << "    \"message_attempts\" : "
        << JsonWriter::num(agg.messageAttempts) << ",\n";
    out << indent << "    \"detections\" : " << JsonWriter::num(agg.detections) << ",\n";
    out << indent << "    \"failures\" : " << JsonWriter::num(agg.failures) << "\n";
    out << indent << "  },\n";
    out << indent << "  \"stages_ms\" : ";
    writeStageMap(out, agg, indent + "  ", false);
    out << ",\n";
    out << indent << "  \"mean_ms\" : ";
    writeStageMap(out, agg, indent + "  ", true);
    out << "\n";
    out << indent << "}";
}

bool writeStageTimingReport(const std::vector<Record>& records,
                            const fs::path& outputDir) {
    TimingAggregate overall;
    std::map<std::string, TimingAggregate> byCategory;
    std::map<std::string, TimingAggregate> bySize;

    for (const Record& rec : records) {
        if (!rec.hasTiming || rec.loadFailed)
            continue;
        overall.add(rec);
        byCategory[rec.category.empty() ? "uncategorized" : rec.category].add(rec);
        bySize[sizeBucket(rec)].add(rec);
    }

    fs::path reportFile = outputDir / "stage_timings.json";
    std::ofstream out(reportFile);
    if (!out.is_open()) {
        std::fprintf(stderr, "Cannot open stage timing report %s\n",
                     reportFile.string().c_str());
        return false;
    }

    out << "{\n";
    out << "  \"image_count\" : " << JsonWriter::num(overall.images) << ",\n";
    out << "  \"top_bottlenecks\" : [";
    auto top = topBottlenecks(overall, 2);
    for (std::size_t i = 0; i < top.size(); i++) {
        if (i > 0) out << ", ";
        double pct = overall.totalMs > 0.0 ? 100.0 * top[i].second / overall.totalMs : 0.0;
        double mean = overall.images > 0
            ? top[i].second / static_cast<double>(overall.images)
            : 0.0;
        out << "{ \"stage\" : \"" << JsonWriter::escape(top[i].first)
            << "\", \"total_ms\" : " << JsonWriter::num(top[i].second)
            << ", \"mean_ms\" : " << JsonWriter::num(mean)
            << ", \"pct_pipeline\" : " << JsonWriter::num(pct) << " }";
    }
    out << " ],\n";
    out << "  \"overall\" : ";
    writeAggregate(out, overall, "  ");
    out << ",\n";

    out << "  \"by_category\" : {\n";
    bool first = true;
    for (const auto& [name, agg] : byCategory) {
        if (!first) out << ",\n";
        first = false;
        out << "    \"" << JsonWriter::escape(name) << "\" : ";
        writeAggregate(out, agg, "    ");
    }
    out << "\n  },\n";

    out << "  \"by_size_bucket\" : {\n";
    first = true;
    for (const auto& [name, agg] : bySize) {
        if (!first) out << ",\n";
        first = false;
        out << "    \"" << JsonWriter::escape(name) << "\" : ";
        writeAggregate(out, agg, "    ");
    }
    out << "\n  }\n";
    out << "}\n";

    if (!top.empty()) {
        std::printf("Stage timing report: %s\n", reportFile.string().c_str());
        std::printf("Top bottlenecks:");
        for (const auto& [name, total] : top) {
            double pct = overall.totalMs > 0.0 ? 100.0 * total / overall.totalMs : 0.0;
            double mean = overall.images > 0
                ? total / static_cast<double>(overall.images)
                : 0.0;
            std::printf(" %s %.3f ms/image (%.1f%%)",
                        name.c_str(), mean, pct);
        }
        std::printf("\n");
    }
    return true;
}

void printStageProfileSummary(const TimingAggregate& agg) {
    if (agg.images == 0)
        return;
    std::printf("Stage timings (mean ms/iter, pct of pipeline):\n");
    auto stages = additiveStageSums(agg);
    for (const auto& [name, total] : stages) {
        if (total <= 0.0)
            continue;
        double mean = total / static_cast<double>(agg.images);
        double pct = agg.totalMs > 0.0 ? 100.0 * total / agg.totalMs : 0.0;
        std::printf("  %-18s %9.4f  %5.1f%%\n",
                    name.c_str(), mean, pct);
    }
    auto top = topBottlenecks(agg, 2);
    if (!top.empty()) {
        std::printf("Top bottlenecks:");
        for (const auto& [name, total] : top) {
            double mean = total / static_cast<double>(agg.images);
            double pct = agg.totalMs > 0.0 ? 100.0 * total / agg.totalMs : 0.0;
            std::printf(" %s %.4f ms/iter (%.1f%%)",
                        name.c_str(), mean, pct);
        }
        std::printf("\n");
    }
}

// ---------------------------------------------------------------------
// Main.
// ---------------------------------------------------------------------

int runBatch(const fs::path& inputDir, const fs::path& outputDir,
             bool collectTimings) {
    fs::create_directories(outputDir);

    std::vector<fs::path> images = collectImages(inputDir);
    std::printf("Found %zu images under %s\n", images.size(),
                inputDir.string().c_str());

    // Warm up on up to 5 images (matches Java's Baseline.java warmup).
    int32_t warmupN = std::min(static_cast<int32_t>(images.size()), 5);
    {
        Pipeline warmupPipe;
        for (int32_t i = 0; i < warmupN; i++) {
            cv::Mat gray = loadGray(images[static_cast<std::size_t>(i)]);
            if (!gray.empty()) {
                try { warmupPipe.run(gray); } catch (...) {}
            }
        }
    }
    std::printf("Warmed up on %d images\n", warmupN);

    int32_t numThreads = chooseBatchThreads(images.size());
    std::printf("Processing with %d worker thread%s",
                numThreads, numThreads == 1 ? "" : "s");
    if (const char* env = std::getenv("QR_SCAN_THREADS")) {
        std::printf(" (QR_SCAN_THREADS=%s)", env);
    }
    if (collectTimings) {
        std::printf(" + stage timings");
    }
    std::printf("\n");

    auto globalStart = std::chrono::steady_clock::now();

    std::vector<Record> records(images.size());
    std::atomic<std::size_t> nextIndex{0};
    std::atomic<int32_t> completed{0};
    std::mutex printMutex;
    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(numThreads));

    for (int32_t workerIdx = 0; workerIdx < numThreads; workerIdx++) {
        workers.emplace_back([&]() {
            Pipeline pipe;
            for (;;) {
                std::size_t index = nextIndex.fetch_add(1);
                if (index >= images.size())
                    break;

                processOne(pipe, images[index], inputDir, records[index],
                           collectTimings);

                int32_t done = completed.fetch_add(1) + 1;
                if (done % 50 == 0 ||
                    done == static_cast<int32_t>(images.size())) {
                    std::lock_guard<std::mutex> lock(printMutex);
                    std::printf("[%d/%zu] processed\n", done, images.size());
                }
            }
        });
    }

    for (std::thread& worker : workers)
        worker.join();

    // Write files after processing so the records array remains in sorted
    // image order even when processing finishes out-of-order.
    fs::path summaryFile = outputDir / "summary.json";
    std::ofstream summary(summaryFile);
    if (!summary.is_open()) {
        std::fprintf(stderr, "Cannot open summary file %s\n",
                     summaryFile.string().c_str());
        return 2;
    }
    summary << "{\n";
    summary << "  \"dataset_root\" : \"" << JsonWriter::escape(inputDir.string()) << "\",\n";
    summary << "  \"output_root\" : \"" << JsonWriter::escape(outputDir.string()) << "\",\n";
    summary << "  \"port_version\" : \"qr-boofcv-cpp-9b\",\n";
    summary << "  \"image_count\" : " << JsonWriter::num(static_cast<int32_t>(images.size())) << ",\n";
    summary << "  \"warmup_images\" : " << JsonWriter::num(warmupN) << ",\n";
    summary << "  \"records\" : [";

    bool firstRec = true;
    for (std::size_t i = 0; i < images.size(); i++) {
        // Write per-image JSON file mirroring dataset structure.
        fs::path rel = fs::relative(images[i], inputDir);
        fs::path outFile = outputDir / rel;
        outFile.replace_extension(".json");
        fs::create_directories(outFile.parent_path());
        std::ofstream f(outFile);
        if (f.is_open()) {
            f << recordJson(records[i]);
        }

        // Append to summary.json's records array.
        if (firstRec) {
            firstRec = false;
            summary << " ";
        } else {
            summary << ", ";
        }
        summary << recordJson(records[i]);
    }
    summary << " ],\n";
    auto globalEnd = std::chrono::steady_clock::now();
    int64_t totalMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            globalEnd - globalStart)
            .count();
    summary << "  \"total_elapsed_ms\" : " << JsonWriter::num(totalMs) << "\n";
    summary << "}\n";
    summary.close();

    std::printf("Wrote %s (%zu records, %lld ms total)\n",
                summaryFile.string().c_str(), images.size(),
                static_cast<long long>(totalMs));
    if (collectTimings && !writeStageTimingReport(records, outputDir))
        return 2;
    return 0;
}

int runSingle(const fs::path& imagePath) {
    Pipeline pipe;
    cv::Mat gray = loadGray(imagePath);
    if (gray.empty()) {
        std::fprintf(stderr, "Cannot load %s\n", imagePath.string().c_str());
        return 1;
    }
    Record rec;
    rec.imageWidth = gray.cols;
    rec.imageHeight = gray.rows;
    rec.imagePath = imagePath.filename().string();
    auto t0 = std::chrono::steady_clock::now();
    try {
        pipe.run(gray);
        rec.detections = pipe.orchestrator.getSuccesses();
        rec.failures = pipe.orchestrator.getFailures();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Error: %s\n", e.what());
    }
    auto t1 = std::chrono::steady_clock::now();
    rec.elapsedMs =
        std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::cout << recordJson(rec) << "\n";
    return 0;
}

// ---------------------------------------------------------------------
// Stage-dump mode — emits the same intermediate-state files as
// `tools/java_reference/DumpStages.java` for stage-by-stage parity diff
// per CLAUDE.md "When you get stuck": "the bug is wherever the dumps
// first disagree."
// ---------------------------------------------------------------------

int runDumpStages(const fs::path& imagePath, const fs::path& outDir) {
    fs::create_directories(outDir);
    cv::Mat gray = loadGray(imagePath);
    if (gray.empty()) {
        std::fprintf(stderr, "Cannot load %s\n", imagePath.string().c_str());
        return 1;
    }
    std::printf("Loaded %s (%dx%d)\n", imagePath.string().c_str(),
                gray.cols, gray.rows);

    Pipeline pipe;
    pipe.run(gray);

    // Stage 1: binary (0/1 in memory; *255 for PNG per CLAUDE.md
    // "Binary image convention").
    cv::Mat bin255 = pipe.binary * 255;
    cv::imwrite((outDir / "binary.png").string(), bin255);
    std::printf("Wrote binary.png (%dx%d)\n", pipe.binary.cols, pipe.binary.rows);

    // Stage 2: detected polygons (pre-finder filter).
    const auto& polyInfo =
        pipe.finder->getSquareDetector().getPolygonInfo();
    {
        std::ostringstream poly;
        poly << "{\n  \"count\" : " << JsonWriter::num(static_cast<int32_t>(polyInfo.size())) << ",\n";
        poly << "  \"polygons\" : [";
        for (std::size_t i = 0; i < polyInfo.size(); i++) {
            if (i > 0) poly << ", ";
            poly << "\n    {\n";
            poly << "      \"corners\" : [";
            const auto& corners = polyInfo[i].polygon;
            for (std::size_t k = 0; k < corners.size(); k++) {
                if (k > 0) poly << ", ";
                poly << "[ " << JsonWriter::num(corners[k].x) << ", "
                     << JsonWriter::num(corners[k].y) << " ]";
            }
            poly << "],\n";
            poly << "      \"edge_inside\" : " << JsonWriter::num(polyInfo[i].edgeInside) << ",\n";
            poly << "      \"edge_outside\" : " << JsonWriter::num(polyInfo[i].edgeOutside) << ",\n";
            poly << "      \"contour_touches_border\" : "
                 << (polyInfo[i].contourTouchesBorder ? "true" : "false") << "\n    }";
        }
        if (!polyInfo.empty()) poly << "\n  ";
        poly << "]\n}\n";
        std::ofstream f(outDir / "polygons.json");
        f << poly.str();
    }
    std::printf("Wrote polygons.json (%zu polygons)\n", polyInfo.size());

    // Stage 3: finder-pattern position-pattern nodes (after 1:1:3:1:1
    // appearance check). These are the polygons that survived the
    // finder check and were eligible for graph wiring.
    const auto& positionPatterns = pipe.finder->getPositionPatterns();
    {
        std::ostringstream pp;
        pp << "{\n  \"count\" : " << JsonWriter::num(static_cast<int32_t>(positionPatterns.size())) << ",\n";
        pp << "  \"position_patterns\" : [";
        for (std::size_t i = 0; i < positionPatterns.size(); i++) {
            if (i > 0) pp << ", ";
            pp << "\n    {\n";
            pp << "      \"corners\" : [";
            const auto& sq = positionPatterns[i].square;
            for (std::size_t k = 0; k < sq.size(); k++) {
                if (k > 0) pp << ", ";
                pp << "[ " << JsonWriter::num(sq[k].x) << ", "
                   << JsonWriter::num(sq[k].y) << " ]";
            }
            pp << "],\n";
            pp << "      \"gray_threshold\" : "
               << JsonWriter::num(positionPatterns[i].grayThreshold) << ",\n";
            pp << "      \"center\" : [ "
               << JsonWriter::num(positionPatterns[i].center.x) << ", "
               << JsonWriter::num(positionPatterns[i].center.y) << " ]\n    }";
        }
        if (!positionPatterns.empty()) pp << "\n  ";
        pp << "]\n}\n";
        std::ofstream f(outDir / "position_patterns.json");
        f << pp.str();
    }
    std::printf("Wrote position_patterns.json (%zu patterns)\n",
                positionPatterns.size());

    // Stage 4: final detections + failures.
    {
        const auto& dets = pipe.orchestrator.getSuccesses();
        const auto& fails = pipe.orchestrator.getFailures();
        std::ostringstream det;
        det << "{\n  \"detection_count\" : "
            << JsonWriter::num(static_cast<int32_t>(dets.size())) << ",\n";
        det << "  \"failure_count\" : "
            << JsonWriter::num(static_cast<int32_t>(fails.size())) << ",\n";
        det << "  \"detections\" : [";
        for (std::size_t i = 0; i < dets.size(); i++) {
            if (i > 0) det << ", ";
            det << "\n    " << detectionJson(dets[i]);
        }
        if (!dets.empty()) det << "\n  ";
        det << "],\n";
        det << "  \"failures\" : [";
        for (std::size_t i = 0; i < fails.size(); i++) {
            if (i > 0) det << ", ";
            det << "\n    " << detectionJson(fails[i]);
        }
        if (!fails.empty()) det << "\n  ";
        det << "]\n}\n";
        std::ofstream f(outDir / "detections.json");
        f << det.str();
    }
    std::printf("Wrote detections.json (%zu successes / %zu failures)\n",
                pipe.orchestrator.getSuccesses().size(),
                pipe.orchestrator.getFailures().size());

    std::printf("All stage dumps written to %s\n", outDir.string().c_str());
    return 0;
}

// ---------------------------------------------------------------------
// Profile mode — loops the pipeline N times on the same image so a
// sampling profiler (e.g. macOS `sample`) can collect enough samples
// for a meaningful function-level breakdown. Prints per-iter timings.
// ---------------------------------------------------------------------

int runProfile(const fs::path& imagePath, int iters) {
    if (iters <= 0) {
        std::fprintf(stderr, "Profile iterations must be > 0\n");
        return 2;
    }
    Pipeline pipe;
    cv::Mat gray = loadGray(imagePath);
    if (gray.empty()) {
        std::fprintf(stderr, "Cannot load %s\n", imagePath.string().c_str());
        return 1;
    }
    std::printf("Loaded %s (%dx%d), %d iterations\n",
                imagePath.string().c_str(), gray.cols, gray.rows, iters);
    // Warm up once.
    pipe.run(gray);
    auto t0 = std::chrono::steady_clock::now();
    int totalDet = 0;
    TimingAggregate stageAgg;
    for (int i = 0; i < iters; ++i) {
        StageTiming timing;
        pipe.runTimed(gray, &timing);
        totalDet += static_cast<int>(pipe.orchestrator.getSuccesses().size());
        Record timingRec;
        timingRec.hasTiming = true;
        timingRec.imageWidth = gray.cols;
        timingRec.imageHeight = gray.rows;
        timingRec.timing = timing;
        stageAgg.add(timingRec);
    }
    auto t1 = std::chrono::steady_clock::now();
    double totalMs =
        std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::printf("Total: %.1f ms, mean per-iter: %.2f ms (det sum %d)\n",
                totalMs, totalMs / iters, totalDet);
    printStageProfileSummary(stageAgg);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 4 && std::string(argv[1]) == "--dump-stages") {
        return runDumpStages(argv[2], argv[3]);
    } else if (argc == 4 && std::string(argv[1]) == "--profile") {
        return runProfile(argv[2], std::atoi(argv[3]));
    } else if (argc == 4 && std::string(argv[1]) == "--stage-timings") {
        return runBatch(argv[2], argv[3], true);
    } else if (argc == 2) {
        return runSingle(argv[1]);
    } else if (argc == 3) {
        return runBatch(argv[1], argv[2], envFlag("QR_SCAN_STAGE_TIMINGS"));
    } else {
        std::fprintf(stderr, "Usage:\n");
        std::fprintf(stderr, "  qr_scan <input_dir> <output_dir>          batch\n");
        std::fprintf(stderr, "      env: QR_SCAN_THREADS=N pins batch worker count\n");
        std::fprintf(stderr, "      env: QR_SCAN_STAGE_TIMINGS=1 writes stage_timings.json\n");
        std::fprintf(stderr, "  qr_scan <single_image.png>                single image\n");
        std::fprintf(stderr, "  qr_scan --dump-stages <image> <outDir>    stage dumps\n");
        std::fprintf(stderr, "  qr_scan --profile <image> <iters>         loop image for profiling\n");
        std::fprintf(stderr, "  qr_scan --stage-timings <input_dir> <output_dir>  batch + timing report\n");
        return 2;
    }
}
