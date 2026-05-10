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
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
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
        binarizer.process(gray, binary);
        finder->process(gray, binary);
        // The graph generator mutates the position-pattern node list in place;
        // we cast away const via the friend grant pattern used elsewhere.
        auto& positions = const_cast<std::vector<boofcv_qr::PositionPatternNode>&>(
            finder->getPositionPatterns());
        graphGen.process(positions);
        orchestrator.process(positions, gray);
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

// ---------------------------------------------------------------------
// Run the pipeline on one image and populate `rec`. Failures (load
// failure, decode exhaustion) are recorded in the JSON, never thrown.
// ---------------------------------------------------------------------

void processOne(Pipeline& pipe, const fs::path& imagePath,
                const fs::path& datasetRoot, Record& rec) {
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
        pipe.run(gray);
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

// ---------------------------------------------------------------------
// Main.
// ---------------------------------------------------------------------

int runBatch(const fs::path& inputDir, const fs::path& outputDir) {
    fs::create_directories(outputDir);

    std::vector<fs::path> images = collectImages(inputDir);
    std::printf("Found %zu images under %s\n", images.size(),
                inputDir.string().c_str());

    Pipeline pipe;

    // Warm up on up to 5 images (matches Java's Baseline.java warmup).
    int32_t warmupN = std::min(static_cast<int32_t>(images.size()), 5);
    for (int32_t i = 0; i < warmupN; i++) {
        cv::Mat gray = loadGray(images[static_cast<std::size_t>(i)]);
        if (!gray.empty()) {
            try { pipe.run(gray); } catch (...) {}
        }
    }
    std::printf("Warmed up on %d images\n", warmupN);

    auto globalStart = std::chrono::steady_clock::now();

    // Open summary.json incrementally — write the records array as we
    // go so we don't hold all 562 records in RAM (each may have
    // alignment vectors etc).
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
    int32_t idx = 0;
    for (const fs::path& img : images) {
        idx++;
        Record rec;
        processOne(pipe, img, inputDir, rec);

        // Write per-image JSON file mirroring dataset structure.
        fs::path rel = fs::relative(img, inputDir);
        fs::path outFile = outputDir / rel;
        outFile.replace_extension(".json");
        fs::create_directories(outFile.parent_path());
        std::ofstream f(outFile);
        if (f.is_open()) {
            f << recordJson(rec);
        }

        // Append to summary.json's records array.
        if (firstRec) {
            firstRec = false;
            summary << " ";
        } else {
            summary << ", ";
        }
        summary << recordJson(rec);

        if (idx % 50 == 0 || idx == static_cast<int32_t>(images.size())) {
            std::printf("[%d/%zu] processed\n", idx, images.size());
        }
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

}  // namespace

int main(int argc, char** argv) {
    if (argc == 4 && std::string(argv[1]) == "--dump-stages") {
        return runDumpStages(argv[2], argv[3]);
    } else if (argc == 2) {
        return runSingle(argv[1]);
    } else if (argc == 3) {
        return runBatch(argv[1], argv[2]);
    } else {
        std::fprintf(stderr, "Usage:\n");
        std::fprintf(stderr, "  qr_scan <input_dir> <output_dir>   batch\n");
        std::fprintf(stderr, "  qr_scan <single_image.png>         single image\n");
        std::fprintf(stderr, "  qr_scan --dump-stages <image> <outDir>   stage dumps\n");
        return 2;
    }
}
