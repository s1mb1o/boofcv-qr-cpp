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
#include <opencv2/core/utility.hpp>
#include <opencv2/imgcodecs.hpp>

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace py = pybind11;

namespace {

struct Point2D {
    double x = 0.0;
    double y = 0.0;

    Point2D() = default;
    Point2D(double x_, double y_) : x(x_), y(y_) {}

    std::pair<double, double> get_tuple() const { return {x, y}; }

    double distance(const Point2D& other) const {
        double dx = other.x - x;
        double dy = other.y - y;
        return std::sqrt(dx * dx + dy * dy);
    }

    Point2D copy() const { return *this; }
};

struct Polygon2D {
    std::vector<Point2D> vertexes;

    Polygon2D() = default;
    explicit Polygon2D(int32_t size) {
        if (size < 0)
            throw py::value_error("Polygon2D size must be non-negative");
        vertexes.resize(static_cast<std::size_t>(size));
    }
    explicit Polygon2D(std::vector<Point2D> points)
        : vertexes(std::move(points)) {}

    std::vector<std::pair<double, double>> convert_tuple() const {
        std::vector<std::pair<double, double>> out;
        out.reserve(vertexes.size());
        for (const Point2D& point : vertexes)
            out.emplace_back(point.x, point.y);
        return out;
    }

    double side_length(std::size_t side) const {
        if (vertexes.empty())
            throw py::index_error("Polygon2D has no vertexes");
        if (side >= vertexes.size())
            throw py::index_error("Polygon2D side index out of range");
        return vertexes[side].distance(vertexes[(side + 1) % vertexes.size()]);
    }

    std::string to_string() const {
        std::ostringstream out;
        out << "Polygon2D( ";
        for (const Point2D& point : vertexes)
            out << "(" << point.x << "," << point.y << ") ";
        out << ")";
        return out.str();
    }
};

struct QrCodeAlignment {
    Point2D pixel;
    int32_t moduleX = 0;
    int32_t moduleY = 0;
    Point2D moduleFound;
    double threshold = 0.0;
};

struct ImageType {
    std::string family = "SINGLE_BAND";
    std::string dtype = "uint8";

    std::string to_string() const {
        return "ImageType(family='" + family + "', dtype='" + dtype + "')";
    }
};

struct ConfigQrCode {
    std::string defaultEncoding = "ISO-8859-1";
    py::object forceEncoding = py::none();
    bool considerTransposed = true;
    bool ignorePaddingBytes = true;
};

struct BatchScanConfig {
    int32_t threads = 0;
    ConfigQrCode config;
    int32_t opencvThreads = 0;
};

struct QrCode {
    int32_t version = -1;
    std::string message;
    std::vector<std::uint8_t> correctedBytes;
    bool hasCorrected = false;
    std::string byteEncoding;
    int32_t totalBitErrors = -1;
    bool bitsTransposed = false;
    std::string error_level;
    std::string mask_pattern;
    std::string mode;
    std::string failure_cause;
    Polygon2D bounds{4};
    Polygon2D pp_right{4};
    Polygon2D pp_corner{4};
    Polygon2D pp_down{4};
    double threshCorner = 0.0;
    double threshDown = 0.0;
    double threshRight = 0.0;
    double threshDownRight = 0.0;
    std::vector<QrCodeAlignment> alignment;

    std::vector<std::uint8_t> rawCodewords;
    std::vector<std::int32_t> rsErrorLocations;
    std::vector<std::string> blockStatus;
};

struct ScanResult {
    std::string path;
    std::vector<QrCode> detections;
    std::vector<QrCode> failures;
    std::string error;
    double elapsed_ms = 0.0;
};

bool dtypeIsUint8(const py::object& dtype) {
    if (dtype.is_none())
        return true;
    py::module_ np = py::module_::import("numpy");
    py::object normalized = np.attr("dtype")(dtype);
    return py::str(normalized.attr("name")).cast<std::string>() == "uint8";
}

std::vector<Point2D> pointsFromObject(const py::object& data) {
    std::vector<Point2D> out;
    for (py::handle item : data) {
        py::sequence seq = py::reinterpret_borrow<py::sequence>(item);
        if (seq.size() != 2)
            throw py::type_error("Point entries must be length-2 sequences");
        out.emplace_back(seq[0].cast<double>(), seq[1].cast<double>());
    }
    return out;
}

Polygon2D polygonFromArray(const std::array<cv::Point2d, 4>& points) {
    std::vector<Point2D> out;
    out.reserve(points.size());
    for (const cv::Point2d& point : points)
        out.emplace_back(point.x, point.y);
    return Polygon2D(std::move(out));
}

Point2D pointFromCv(const cv::Point2d& point) {
    return Point2D(point.x, point.y);
}

std::vector<QrCodeAlignment> alignmentsFromCpp(
    const std::vector<boofcv_qr::QrCode::Alignment>& alignments) {
    std::vector<QrCodeAlignment> out;
    out.reserve(alignments.size());
    for (const boofcv_qr::QrCode::Alignment& alignment : alignments) {
        QrCodeAlignment pyAlignment;
        pyAlignment.pixel = pointFromCv(alignment.pixel);
        pyAlignment.moduleX = alignment.moduleX;
        pyAlignment.moduleY = alignment.moduleY;
        pyAlignment.moduleFound = pointFromCv(alignment.moduleFound);
        pyAlignment.threshold = alignment.threshold;
        out.push_back(pyAlignment);
    }
    return out;
}

py::array_t<std::uint8_t> vectorToArray(const std::vector<std::uint8_t>& data) {
    py::array_t<std::uint8_t> out(static_cast<py::ssize_t>(data.size()));
    if (!data.empty()) {
        std::memcpy(out.mutable_data(), data.data(), data.size() * sizeof(std::uint8_t));
    }
    return out;
}

py::object correctedArray(const QrCode& qr) {
    if (!qr.hasCorrected)
        return py::none();
    return vectorToArray(qr.correctedBytes);
}

py::object correctedList(const QrCode& qr) {
    if (!qr.hasCorrected)
        return py::none();
    return py::cast(qr.correctedBytes);
}

py::dict alignmentAsDict(const QrCodeAlignment& alignment) {
    py::dict out;
    out["pixel"] = alignment.pixel.get_tuple();
    out["moduleX"] = alignment.moduleX;
    out["moduleY"] = alignment.moduleY;
    out["moduleFound"] = alignment.moduleFound.get_tuple();
    out["threshold"] = alignment.threshold;
    return out;
}

py::dict finderPatternDict(const QrCode& qr) {
    py::dict out;
    out["corner"] = qr.pp_corner.convert_tuple();
    out["right"] = qr.pp_right.convert_tuple();
    out["down"] = qr.pp_down.convert_tuple();
    return out;
}

py::dict qrAsDict(const QrCode& qr) {
    py::dict out;
    out["version"] = qr.version;
    out["message"] = qr.message;
    out["byteEncoding"] = qr.byteEncoding;
    out["byte_encoding"] = qr.byteEncoding;
    out["totalBitErrors"] = qr.totalBitErrors;
    out["total_bit_errors"] = qr.totalBitErrors;
    out["bitsTransposed"] = qr.bitsTransposed;
    out["bits_transposed"] = qr.bitsTransposed;
    out["error_level"] = qr.error_level;
    out["mask_pattern"] = qr.mask_pattern;
    out["mode"] = qr.mode;
    out["failure_cause"] = qr.failure_cause;
    out["bounds"] = qr.bounds.convert_tuple();
    out["position_patterns"] = finderPatternDict(qr);
    out["rawCodewords"] = py::cast(qr.rawCodewords);
    out["raw_codewords"] = py::cast(qr.rawCodewords);
    out["corrected"] = correctedList(qr);
    out["rsErrorLocations"] = py::cast(qr.rsErrorLocations);
    out["rs_error_locations"] = py::cast(qr.rsErrorLocations);
    out["blockStatus"] = py::cast(qr.blockStatus);
    out["block_status"] = py::cast(qr.blockStatus);
    out["threshCorner"] = qr.threshCorner;
    out["threshDown"] = qr.threshDown;
    out["threshRight"] = qr.threshRight;
    out["threshDownRight"] = qr.threshDownRight;
    py::list alignments;
    for (const QrCodeAlignment& alignment : qr.alignment)
        alignments.append(alignmentAsDict(alignment));
    out["alignment"] = alignments;
    return out;
}

py::dict scanResultAsDict(const ScanResult& result) {
    py::dict out;
    out["path"] = result.path;
    out["error"] = result.error;
    out["ok"] = result.error.empty();
    out["elapsed_ms"] = result.elapsed_ms;
    py::list detections;
    for (const QrCode& qr : result.detections)
        detections.append(qrAsDict(qr));
    py::list failures;
    for (const QrCode& qr : result.failures)
        failures.append(qrAsDict(qr));
    out["detections"] = detections;
    out["failures"] = failures;
    return out;
}

const char* errorLevelName(boofcv_qr::ErrorLevel e) {
    switch (e) {
        case boofcv_qr::ErrorLevel::L: return "L";
        case boofcv_qr::ErrorLevel::M: return "M";
        case boofcv_qr::ErrorLevel::Q: return "Q";
        case boofcv_qr::ErrorLevel::H: return "H";
    }
    return "";
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
    return "";
}

const char* failureName(boofcv_qr::Failure f) {
    switch (f) {
        case boofcv_qr::Failure::NONE: return "";
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
    return "";
}

std::string maskName(const boofcv_qr::QrCodeMaskPattern* mask) {
    if (mask == nullptr)
        return "";
    using boofcv_qr::QrCodeMaskPattern;
    if (mask == &QrCodeMaskPattern::M000()) return "M000";
    if (mask == &QrCodeMaskPattern::M001()) return "M001";
    if (mask == &QrCodeMaskPattern::M010()) return "M010";
    if (mask == &QrCodeMaskPattern::M011()) return "M011";
    if (mask == &QrCodeMaskPattern::M100()) return "M100";
    if (mask == &QrCodeMaskPattern::M101()) return "M101";
    if (mask == &QrCodeMaskPattern::M110()) return "M110";
    if (mask == &QrCodeMaskPattern::M111()) return "M111";
    return "";
}

std::string blockStatusName(boofcv_qr::QrCode::BlockStatus status) {
    switch (status) {
        case boofcv_qr::QrCode::BlockStatus::NOT_DECODED: return "NOT_DECODED";
        case boofcv_qr::QrCode::BlockStatus::SUCCESS_NO_ERRORS:
            return "SUCCESS_NO_ERRORS";
        case boofcv_qr::QrCode::BlockStatus::SUCCESS: return "SUCCESS";
        case boofcv_qr::QrCode::BlockStatus::ERROR_CORRECTION_FAILED:
            return "ERROR_CORRECTION_FAILED";
    }
    return "UNKNOWN";
}

boofcv_qr::QrCodeDecoderImage::Config toCppConfig(const ConfigQrCode& pyConfig) {
    boofcv_qr::QrCodeDecoderImage::Config cfg;
    cfg.defaultEncoding = pyConfig.defaultEncoding;
    cfg.considerTransposed = pyConfig.considerTransposed;
    cfg.ignorePaddingBytes = pyConfig.ignorePaddingBytes;
    if (!pyConfig.forceEncoding.is_none())
        cfg.forceEncoding = pyConfig.forceEncoding.cast<std::string>();
    return cfg;
}

std::string pathToString(const py::object& path) {
    py::module_ os = py::module_::import("os");
    py::object filesystemPath = os.attr("fspath")(path);
    if (py::isinstance<py::bytes>(filesystemPath))
        return filesystemPath.cast<std::string>();
    return py::str(filesystemPath).cast<std::string>();
}

cv::Mat loadGrayFromPath(const std::string& path) {
    return cv::imread(path, cv::IMREAD_GRAYSCALE | cv::IMREAD_IGNORE_ORIENTATION);
}

QrCode toPythonQrCode(const boofcv_qr::QrCode& qr) {
    QrCode out;
    out.version = qr.version;
    out.message = qr.message;
    out.correctedBytes = qr.corrected;
    out.hasCorrected = !qr.corrected.empty();
    out.byteEncoding = qr.byteEncoding;
    out.totalBitErrors = qr.totalBitErrors;
    out.bitsTransposed = qr.bitsTransposed;
    out.error_level = errorLevelName(qr.error);
    out.mask_pattern = maskName(qr.mask);
    out.mode = modeName(qr.mode);
    out.failure_cause = failureName(qr.failureCause);
    out.bounds = polygonFromArray(qr.bounds);
    out.pp_right = polygonFromArray(qr.ppRight);
    out.pp_corner = polygonFromArray(qr.ppCorner);
    out.pp_down = polygonFromArray(qr.ppDown);
    out.threshCorner = qr.threshCorner;
    out.threshDown = qr.threshDown;
    out.threshRight = qr.threshRight;
    out.threshDownRight = qr.threshDownRight;
    out.alignment = alignmentsFromCpp(qr.alignment);
    out.rawCodewords = qr.rawCodewords;
    out.rsErrorLocations = qr.rsErrorLocations;
    out.blockStatus.reserve(qr.blockStatus.size());
    for (boofcv_qr::QrCode::BlockStatus status : qr.blockStatus)
        out.blockStatus.push_back(blockStatusName(status));
    return out;
}

struct ImageMat {
    cv::Mat gray;
};

ImageMat imageFromObject(const py::object& image) {
    if (image.is_none())
        throw py::type_error("Input is None");

    if (!py::isinstance<py::array>(image))
        throw py::type_error("Expected image to be a uint8 numpy.ndarray");

    py::array raw = py::reinterpret_borrow<py::array>(image);
    py::buffer_info rawInfo = raw.request();
    if (rawInfo.itemsize != static_cast<py::ssize_t>(sizeof(std::uint8_t)) ||
        rawInfo.format != py::format_descriptor<std::uint8_t>::format()) {
        throw py::type_error(
            "Expected GrayU8 image with dtype numpy.uint8; use "
            "load_single_band() or convert the array before detect()");
    }
    if (rawInfo.ndim == 3 && (rawInfo.shape[2] == 3 || rawInfo.shape[2] == 4)) {
        throw py::type_error(
            "Expected a single-band GrayU8 image, not an RGB/BGR/RGBA image; "
            "convert to grayscale first or use load_single_band()");
    }

    py::array_t<std::uint8_t, py::array::c_style> arr =
        py::array_t<std::uint8_t, py::array::c_style>::ensure(image);
    if (!arr)
        throw py::type_error("Expected image to be a uint8 numpy.ndarray");

    py::buffer_info info = arr.request();
    if (info.ndim == 2) {
        int rows = static_cast<int>(info.shape[0]);
        int cols = static_cast<int>(info.shape[1]);
        cv::Mat view(rows, cols, CV_8UC1, arr.mutable_data());
        return ImageMat{view.clone()};
    }
    if (info.ndim == 3 && info.shape[2] == 1) {
        int rows = static_cast<int>(info.shape[0]);
        int cols = static_cast<int>(info.shape[1]);
        cv::Mat view(rows, cols, CV_8UC1, arr.mutable_data());
        return ImageMat{view.clone()};
    }
    throw py::type_error(
        "Expected GrayU8 image with shape (height, width) or "
        "(height, width, 1)");
}

py::array_t<std::uint8_t> matToArray(const cv::Mat& mat) {
    py::array_t<std::uint8_t> out({mat.rows, mat.cols});
    std::uint8_t* dst = out.mutable_data();
    for (int y = 0; y < mat.rows; y++) {
        const std::uint8_t* src = mat.ptr<std::uint8_t>(y);
        std::memcpy(dst + static_cast<std::size_t>(y * mat.cols), src,
                    static_cast<std::size_t>(mat.cols));
    }
    return out;
}

class DetectorPipeline {
public:
    explicit DetectorPipeline(boofcv_qr::QrCodeDecoderImage::Config config)
        : orchestrator_(std::move(config)) {
        boofcv_qr::ConfigPolylineSplitMerge polyCfg;
        polyCfg.minimumSides = 4;
        polyCfg.maximumSides = 4;
        polyCfg.cornerScorePenalty = 0.4;
        polyCfg.maxSideError = boofcv_qr::ConfigLength::relative(0.12, 3.0);
        polyCfg.minimumSideLength = 2;
        auto adapter =
            std::make_unique<boofcv_qr::PolylineSplitMergeAdapter>(polyCfg);

        auto contour = std::make_unique<boofcv_qr::DetectPolygonFromContour>(
            std::move(adapter), true, false, 3.0, 1.5);
        contour->setNumberOfSides(4, 4);
        contour->setMinimumContour(boofcv_qr::ConfigLength::fixed(40.0));

        boofcv_qr::ConfigRefinePolygonLineToImage refineCfg;
        auto refine =
            std::make_shared<boofcv_qr::RefinePolygonToGrayLine>(refineCfg);

        auto wrapper =
            std::make_shared<boofcv_qr::DetectPolygonBinaryGrayRefine>(
                std::move(contour), std::move(refine), 6.0, true);

        finder_ = std::make_unique<boofcv_qr::QrCodePositionPatternDetector>(
            std::move(wrapper));
    }

    void run(const cv::Mat& gray) {
        binarizer_.process(gray, binary_);
        finder_->process(gray, binary_);
        auto& positions =
            const_cast<std::vector<boofcv_qr::PositionPatternNode>&>(
                finder_->getPositionPatterns());
        graphGen_.process(positions);
        orchestrator_.process(positions, gray);
    }

    std::vector<boofcv_qr::QrCode> detectPolygonsOnly(const cv::Mat& gray) {
        binarizer_.process(gray, binary_);
        finder_->process(gray, binary_);
        auto& positions =
            const_cast<std::vector<boofcv_qr::PositionPatternNode>&>(
                finder_->getPositionPatterns());
        graphGen_.process(positions);
        boofcv_qr::PolygonOnlyResult result =
            orchestrator_.detect_polygons_only(positions, gray);
        return std::move(result.qrCodes);
    }

    const std::vector<boofcv_qr::QrCode>& successes() const {
        return orchestrator_.getSuccesses();
    }

    const std::vector<boofcv_qr::QrCode>& failures() const {
        return orchestrator_.getFailures();
    }

private:
    boofcv_qr::ThresholdBlockOtsu binarizer_;
    std::unique_ptr<boofcv_qr::QrCodePositionPatternDetector> finder_;
    boofcv_qr::QrCodePositionPatternGraphGenerator graphGen_{40};
    boofcv_qr::QrCodeDecoderImage orchestrator_;
    cv::Mat binary_;
};

class QrCodeDetector {
public:
    explicit QrCodeDetector(ConfigQrCode config = ConfigQrCode{})
        : config_(std::move(config)),
          pipeline_(std::make_unique<DetectorPipeline>(toCppConfig(config_))) {}

    void detect(const py::object& image) {
        ImageMat input = imageFromObject(image);
        {
            py::gil_scoped_release release;
            pipeline_->run(input.gray);
        }

        detections.clear();
        failures.clear();
        for (const boofcv_qr::QrCode& qr : pipeline_->successes())
            detections.push_back(toPythonQrCode(qr));
        for (const boofcv_qr::QrCode& qr : pipeline_->failures())
            failures.push_back(toPythonQrCode(qr));
    }

    std::vector<QrCode> detect_polygons_only(const py::object& image) {
        ImageMat input = imageFromObject(image);
        std::vector<boofcv_qr::QrCode> candidates;
        {
            py::gil_scoped_release release;
            candidates = pipeline_->detectPolygonsOnly(input.gray);
        }

        std::vector<QrCode> out;
        out.reserve(candidates.size());
        for (const boofcv_qr::QrCode& qr : candidates)
            out.push_back(toPythonQrCode(qr));
        return out;
    }

    ImageType get_image_type() const { return ImageType{}; }

    std::vector<QrCode> detections;
    std::vector<QrCode> failures;

private:
    ConfigQrCode config_;
    std::unique_ptr<DetectorPipeline> pipeline_;
};

class FactoryFiducial {
public:
    explicit FactoryFiducial(py::object imageType)
        : imageType_(std::move(imageType)) {
        if (!dtypeIsUint8(imageType_)) {
            throw py::type_error(
                "Only np.uint8 / BoofCV GrayU8 images are supported");
        }
    }

    QrCodeDetector qrcode(py::object config = py::none()) const {
        if (config.is_none())
            return QrCodeDetector(ConfigQrCode{});
        return QrCodeDetector(config.cast<ConfigQrCode>());
    }

private:
    py::object imageType_;
};

void markRemainingBatchErrors(const std::vector<std::string>& inputPaths,
                              std::vector<ScanResult>& results,
                              std::atomic<std::size_t>& next,
                              const std::string& message) {
    while (true) {
        std::size_t index = next.fetch_add(1);
        if (index >= inputPaths.size())
            break;
        ScanResult result;
        result.path = inputPaths[index];
        result.error = message;
        results[index] = std::move(result);
    }
}

int32_t readOpenCvThreadsEnv() {
    const char* value = std::getenv("BOOFCV_QR_OPENCV_THREADS");
    if (value == nullptr)
        value = std::getenv("QR_SCAN_OPENCV_THREADS");
    if (value == nullptr)
        return 0;
    int32_t parsed = std::atoi(value);
    return parsed > 0 ? parsed : 0;
}

std::mutex& openCvThreadMutex() {
    static std::mutex mutex;
    return mutex;
}

void configureOpenCvThreadsForBatch(std::size_t imageWorkers,
                                    int32_t explicitThreads) {
    std::lock_guard<std::mutex> lock(openCvThreadMutex());
    int32_t requested = explicitThreads > 0 ? explicitThreads : readOpenCvThreadsEnv();
    int32_t target = 0;
    if (requested > 0) {
        target = requested;
    } else if (imageWorkers > 1) {
        target = 1;
    }
    if (target > 0 && cv::getNumThreads() != target)
        cv::setNumThreads(target);
}

py::array_t<std::uint8_t> loadSingleBand(py::object path,
                                         py::object dtype = py::none()) {
    if (!dtypeIsUint8(dtype)) {
        throw py::type_error(
            "Only np.uint8 / BoofCV GrayU8 images are supported");
    }
    std::string pathString = pathToString(path);
    cv::Mat gray = loadGrayFromPath(pathString);
    if (gray.empty())
        throw py::value_error("Failed to load image: " + pathString);
    return matToArray(gray);
}

std::vector<ScanResult> scanBatchConfigured(py::object paths,
                                            BatchScanConfig batchConfig) {
    boofcv_qr::QrCodeDecoderImage::Config cppConfig =
        toCppConfig(batchConfig.config);

    std::vector<std::string> inputPaths;
    for (py::handle item : paths)
        inputPaths.push_back(pathToString(py::reinterpret_borrow<py::object>(item)));

    std::vector<ScanResult> results(inputPaths.size());
    for (std::size_t i = 0; i < inputPaths.size(); i++)
        results[i].path = inputPaths[i];
    if (inputPaths.empty())
        return results;

    std::size_t workerCount = 1;
    if (batchConfig.threads > 0) {
        workerCount = static_cast<std::size_t>(batchConfig.threads);
    } else {
        unsigned int detected = std::thread::hardware_concurrency();
        workerCount = detected == 0 ? 1 : static_cast<std::size_t>(detected);
    }
    if (workerCount > inputPaths.size())
        workerCount = inputPaths.size();

    configureOpenCvThreadsForBatch(workerCount, batchConfig.opencvThreads);
    {
        py::gil_scoped_release release;
        std::atomic<std::size_t> next{0};
        std::vector<std::thread> workers;
        workers.reserve(workerCount);
        for (std::size_t worker = 0; worker < workerCount; worker++) {
            workers.emplace_back([&]() {
                try {
                    DetectorPipeline pipeline(cppConfig);
                    while (true) {
                        std::size_t index = next.fetch_add(1);
                        if (index >= inputPaths.size())
                            break;

                        ScanResult result;
                        result.path = inputPaths[index];
                        auto t0 = std::chrono::steady_clock::now();
                        try {
                            cv::Mat gray = loadGrayFromPath(result.path);
                            if (gray.empty()) {
                                result.error = "Failed to load image: " + result.path;
                            } else {
                                pipeline.run(gray);
                                for (const boofcv_qr::QrCode& qr :
                                     pipeline.successes())
                                    result.detections.push_back(toPythonQrCode(qr));
                                for (const boofcv_qr::QrCode& qr :
                                     pipeline.failures())
                                    result.failures.push_back(toPythonQrCode(qr));
                            }
                        } catch (const cv::Exception& e) {
                            result.error = std::string("OpenCV error: ") + e.what();
                        } catch (const std::exception& e) {
                            result.error = e.what();
                        } catch (...) {
                            result.error = "Unknown error while scanning";
                        }
                        auto t1 = std::chrono::steady_clock::now();
                        result.elapsed_ms =
                            std::chrono::duration<double, std::milli>(t1 - t0).count();
                        results[index] = std::move(result);
                    }
                } catch (const cv::Exception& e) {
                    markRemainingBatchErrors(
                        inputPaths, results, next,
                        std::string("OpenCV worker error: ") + e.what());
                } catch (const std::exception& e) {
                    markRemainingBatchErrors(inputPaths, results, next, e.what());
                } catch (...) {
                    markRemainingBatchErrors(inputPaths, results, next,
                                             "Unknown worker error");
                }
            });
        }
        for (std::thread& worker : workers)
            worker.join();
    }

    return results;
}

std::vector<ScanResult> scanBatch(py::object paths,
                                  int32_t threads,
                                  py::object config) {
    BatchScanConfig batchConfig;
    batchConfig.threads = threads;
    if (!config.is_none())
        batchConfig.config = config.cast<ConfigQrCode>();
    return scanBatchConfigured(std::move(paths), std::move(batchConfig));
}

}  // namespace

PYBIND11_MODULE(_boofcv_qr, m) {
    m.doc() = "PyBoof-compatible QR subset backed by boofcv-qr-cpp";
    m.attr("__version__") = BOOFCV_QR_VERSION;

    py::class_<Point2D>(m, "Point2D")
        .def(py::init<>())
        .def(py::init<double, double>(), py::arg("x") = 0.0, py::arg("y") = 0.0)
        .def_readwrite("x", &Point2D::x)
        .def_readwrite("y", &Point2D::y)
        .def("get_tuple", &Point2D::get_tuple)
        .def("as_tuple", &Point2D::get_tuple)
        .def("get_x", [](const Point2D& p) { return p.x; })
        .def("get_y", [](const Point2D& p) { return p.y; })
        .def("set_x", [](Point2D& p, double x) { p.x = x; })
        .def("set_y", [](Point2D& p, double y) { p.y = y; })
        .def("distance", &Point2D::distance)
        .def("copy", &Point2D::copy);

    py::class_<Polygon2D>(m, "Polygon2D")
        .def(py::init<>())
        .def(py::init<int32_t>())
        .def(py::init([](py::object data) {
            if (py::isinstance<py::int_>(data))
                return Polygon2D(data.cast<int32_t>());
            return Polygon2D(pointsFromObject(data));
        }))
        .def_readwrite("vertexes", &Polygon2D::vertexes)
        .def("convert_tuple", &Polygon2D::convert_tuple)
        .def("as_list", &Polygon2D::convert_tuple)
        .def("side_length", &Polygon2D::side_length)
        .def("__len__", [](const Polygon2D& p) { return p.vertexes.size(); })
        .def("__getitem__", [](const Polygon2D& p, std::size_t index) {
            if (index >= p.vertexes.size())
                throw py::index_error("Polygon2D vertex index out of range");
            return p.vertexes[index];
        })
        .def("__repr__", &Polygon2D::to_string)
        .def("__str__", &Polygon2D::to_string);

    py::class_<QrCodeAlignment>(m, "QrCodeAlignment")
        .def(py::init<>())
        .def_readwrite("pixel", &QrCodeAlignment::pixel)
        .def_readwrite("moduleX", &QrCodeAlignment::moduleX)
        .def_readwrite("moduleY", &QrCodeAlignment::moduleY)
        .def_readwrite("moduleFound", &QrCodeAlignment::moduleFound)
        .def_readwrite("threshold", &QrCodeAlignment::threshold)
        .def("as_dict", &alignmentAsDict);

    py::class_<ImageType>(m, "ImageType")
        .def(py::init<>())
        .def_readwrite("family", &ImageType::family)
        .def_readwrite("dtype", &ImageType::dtype)
        .def("__repr__", &ImageType::to_string)
        .def("__str__", &ImageType::to_string);

    py::class_<ConfigQrCode>(m, "ConfigQrCode")
        .def(py::init<>())
        .def_readwrite("defaultEncoding", &ConfigQrCode::defaultEncoding)
        .def_readwrite("forceEncoding", &ConfigQrCode::forceEncoding)
        .def_readwrite("considerTransposed", &ConfigQrCode::considerTransposed)
        .def_readwrite("ignorePaddingBytes", &ConfigQrCode::ignorePaddingBytes);

    py::class_<BatchScanConfig>(m, "BatchScanConfig")
        .def(py::init<>())
        .def_readwrite("threads", &BatchScanConfig::threads)
        .def_readwrite("config", &BatchScanConfig::config)
        .def_readwrite("opencv_threads", &BatchScanConfig::opencvThreads);

    py::class_<QrCode>(m, "QrCode")
        .def(py::init<>())
        .def_readwrite("version", &QrCode::version)
        .def_readwrite("message", &QrCode::message)
        .def_property_readonly("corrected", &correctedArray)
        .def_property_readonly("corrected_bytes", &correctedArray)
        .def_readwrite("byteEncoding", &QrCode::byteEncoding)
        .def_property("byte_encoding",
            [](const QrCode& qr) { return qr.byteEncoding; },
            [](QrCode& qr, const std::string& value) { qr.byteEncoding = value; })
        .def_readwrite("totalBitErrors", &QrCode::totalBitErrors)
        .def_property("total_bit_errors",
            [](const QrCode& qr) { return qr.totalBitErrors; },
            [](QrCode& qr, int32_t value) { qr.totalBitErrors = value; })
        .def_readwrite("bitsTransposed", &QrCode::bitsTransposed)
        .def_property("bits_transposed",
            [](const QrCode& qr) { return qr.bitsTransposed; },
            [](QrCode& qr, bool value) { qr.bitsTransposed = value; })
        .def_readwrite("error_level", &QrCode::error_level)
        .def_readwrite("mask_pattern", &QrCode::mask_pattern)
        .def_readwrite("mode", &QrCode::mode)
        .def_readwrite("failure_cause", &QrCode::failure_cause)
        .def_readwrite("bounds", &QrCode::bounds)
        .def_property_readonly("corners",
            [](const QrCode& qr) { return qr.bounds.convert_tuple(); })
        .def_readwrite("pp_right", &QrCode::pp_right)
        .def_readwrite("pp_corner", &QrCode::pp_corner)
        .def_readwrite("pp_down", &QrCode::pp_down)
        .def_property_readonly("position_patterns", &finderPatternDict)
        .def_readwrite("threshCorner", &QrCode::threshCorner)
        .def_readwrite("threshDown", &QrCode::threshDown)
        .def_readwrite("threshRight", &QrCode::threshRight)
        .def_readwrite("threshDownRight", &QrCode::threshDownRight)
        .def_readwrite("alignment", &QrCode::alignment)
        .def_property_readonly("alignment_patterns",
            [](const QrCode& qr) { return qr.alignment; })
        .def_property_readonly(
            "rawCodewords",
            [](const QrCode& qr) { return vectorToArray(qr.rawCodewords); })
        .def_property_readonly(
            "raw_codewords",
            [](const QrCode& qr) { return vectorToArray(qr.rawCodewords); })
        .def_readwrite("rsErrorLocations", &QrCode::rsErrorLocations)
        .def_property("rs_error_locations",
            [](const QrCode& qr) { return qr.rsErrorLocations; },
            [](QrCode& qr, std::vector<std::int32_t> value) {
                qr.rsErrorLocations = std::move(value);
            })
        .def_readwrite("blockStatus", &QrCode::blockStatus)
        .def_property("block_status",
            [](const QrCode& qr) { return qr.blockStatus; },
            [](QrCode& qr, std::vector<std::string> value) {
                qr.blockStatus = std::move(value);
            })
        .def("as_dict", &qrAsDict);

    py::class_<ScanResult>(m, "ScanResult")
        .def(py::init<>())
        .def_readwrite("path", &ScanResult::path)
        .def_readwrite("detections", &ScanResult::detections)
        .def_readwrite("failures", &ScanResult::failures)
        .def_readwrite("error", &ScanResult::error)
        .def_readwrite("elapsed_ms", &ScanResult::elapsed_ms)
        .def_property_readonly("ok",
            [](const ScanResult& result) { return result.error.empty(); })
        .def("as_dict", &scanResultAsDict);

    py::class_<QrCodeDetector>(m, "QrCodeDetector")
        .def(py::init<ConfigQrCode>(), py::arg("config") = ConfigQrCode{})
        .def("detect", &QrCodeDetector::detect)
        .def("detect_polygons_only", &QrCodeDetector::detect_polygons_only)
        .def("get_image_type", &QrCodeDetector::get_image_type)
        .def_readwrite("detections", &QrCodeDetector::detections)
        .def_readwrite("failures", &QrCodeDetector::failures);

    py::class_<FactoryFiducial>(m, "FactoryFiducial")
        .def(py::init<py::object>())
        .def("qrcode", &FactoryFiducial::qrcode,
             py::arg("config") = py::none());

    m.def("load_single_band", &loadSingleBand,
          py::arg("path"), py::arg("dtype") = py::none());
    m.def("scan_batch", &scanBatch,
          py::arg("paths"), py::arg("threads") = 0,
          py::arg("config") = py::none());
    m.def("scan_batch", &scanBatchConfigured,
          py::arg("paths"), py::arg("batch_config"));
}
