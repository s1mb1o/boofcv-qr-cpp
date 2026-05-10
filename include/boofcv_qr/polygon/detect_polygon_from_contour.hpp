// Port of boofcv.alg.shapes.polygon.DetectPolygonFromContour
// (BoofCV v1.3.0). Step 7b (part 2).
//
// See src/polygon/detect_polygon_from_contour.md for the algorithm
// explanation and rationale for departures from Java types.
//
// Per CLAUDE.md type mappings:
//   GrayU8 -> cv::Mat CV_8UC1 (we commit to a single image type; Java's
//             template <T extends ImageGray<T>> is dropped)
//   Polygon2D_F64 -> std::vector<cv::Point2d>
//   Point2D_I32 -> cv::Point2i
//   List<Point2D_I32> -> std::vector<cv::Point2i>
//   DogArray_B -> std::vector<uint8_t>
//   DogArray_I32 -> std::vector<int32_t>
//   ContourPacked + Contour -> Contour (struct holding the points
//             directly; OpenCV's findContours gives us the points
//             outright, so we drop BoofCV's packed-set indirection)
//   PolygonHelper -> abstract base (caller may override; QR doesn't)
//   PointsToPolyline -> abstract base (default impl wraps PolylineSplitMerge)

#ifndef BOOFCV_QR_POLYGON_DETECT_POLYGON_FROM_CONTOUR_HPP
#define BOOFCV_QR_POLYGON_DETECT_POLYGON_FROM_CONTOUR_HPP

#include "boofcv_qr/binary/linear_contour_label_chang2004.hpp"
#include "boofcv_qr/polyline/polyline_split_merge.hpp"

#include <opencv2/core.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace boofcv_qr {

// Mirror of `boofcv.alg.filter.binary.Contour`. External outline of one
// dark blob plus zero-or-more internal-hole outlines.
struct Contour {
    std::vector<cv::Point2i> external;
    std::vector<std::vector<cv::Point2i>> internal;

    void reset() {
        external.clear();
        internal.clear();
    }
};

// PointsToPolyline interface (mirror of
// boofcv.abst.shapes.polyline.PointsToPolyline). Mostly here so callers
// can swap in alternative corner finders; default is the
// `PolylineSplitMergeAdapter` defined below which wraps our ported
// `PolylineSplitMerge`.
struct PointsToPolyline {
    virtual ~PointsToPolyline() = default;

    virtual bool process(const std::vector<cv::Point2i>& input,
                         std::vector<int32_t>& vertexes) = 0;

    virtual void setMinimumSides(int32_t v) = 0;
    virtual int32_t getMinimumSides() const = 0;

    virtual void setMaximumSides(int32_t v) = 0;
    virtual int32_t getMaximumSides() const = 0;

    virtual bool isLoop() const = 0;

    virtual void setConvex(bool convex) = 0;
    virtual bool isConvex() const = 0;
};

// Mirror of boofcv.abst.shapes.polyline.{BaseConfigPolyline,ConfigPolylineSplitMerge}.
// Holds every PolylineSplitMerge tunable that BoofCV's
// `NewSplitMerge_to_PointsToPolyline` ctor plumbs through. The
// `PolylineSplitMergeAdapter` ctor below applies *all* of these to its
// underlying `PolylineSplitMerge` — without this the QR overrides from
// `ConfigQrCode.java` (minimumSideLength=2, cornerScorePenalty=0.4,
// maxSideError=relative(0.12,3), etc.) would silently revert to
// `PolylineSplitMerge`'s baked-in defaults at the contour-fitting
// stage. Defaults below match Java's `ConfigPolylineSplitMerge`
// defaults verbatim.
struct ConfigPolylineSplitMerge {
    // BaseConfigPolyline fields
    bool loops = true;
    int32_t minimumSides = 3;
    int32_t maximumSides = std::numeric_limits<int32_t>::max();
    bool convex = true;

    // ConfigPolylineSplitMerge fields (defaults from upstream)
    int32_t minimumSideLength = 2;
    ConfigLength extraConsider = ConfigLength::relative(1.0, 0);
    double cornerScorePenalty = 0.025;
    double thresholdSideSplitScore = 0.2;
    int32_t maxNumberOfSideSamples = 50;
    double convexTest = 2.5;
    ConfigLength maxSideError = ConfigLength::relative(0.05, 3);
    // refineIterations is intentionally not exposed: RefinePolyLineCorner
    // is a separate (deferred) port stage. Setting refineIterations on
    // this struct has no effect — documented here so callers don't
    // expect it to.
};

// Default `PointsToPolyline` impl wrapping `PolylineSplitMerge`.
// Owns its `PolylineSplitMerge` and forwards configuration through.
class PolylineSplitMergeAdapter : public PointsToPolyline {
public:
    // Default ctor: matches `ConfigPolylineSplitMerge`'s defaults.
    PolylineSplitMergeAdapter() : PolylineSplitMergeAdapter(ConfigPolylineSplitMerge{}) {}

    // Plumbed-config ctor — mirrors
    // `boofcv.abst.shapes.polyline.NewSplitMerge_to_PointsToPolyline`'s
    // ctor exactly: applies every one of the 11 ConfigPolylineSplitMerge
    // fields to the underlying `PolylineSplitMerge`. The QR factory
    // wires this up via `ConfigQrCode.polygon.detector.contourToPoly`.
    explicit PolylineSplitMergeAdapter(const ConfigPolylineSplitMerge& cfg) {
        impl_.setMinimumSideLength(cfg.minimumSideLength);
        impl_.setMaxNumberOfSideSamples(cfg.maxNumberOfSideSamples);
        impl_.setMaxSides(cfg.maximumSides);
        impl_.setMinSides(cfg.minimumSides);
        impl_.setExtraConsider(cfg.extraConsider);
        impl_.setConvex(cfg.convex);
        impl_.setThresholdSideSplitScore(cfg.thresholdSideSplitScore);
        impl_.setCornerScorePenalty(cfg.cornerScorePenalty);
        impl_.setConvexTest(cfg.convexTest);
        impl_.setMaxSideError(cfg.maxSideError);
        impl_.setLoops(cfg.loops);
    }

    bool process(const std::vector<cv::Point2i>& input,
                 std::vector<int32_t>& vertexes) override {
        if (!impl_.process(input)) return false;
        auto best = impl_.getBestPolyline();
        if (!best.has_value()) return false;
        vertexes = best->splits;
        return true;
    }

    void setMinimumSides(int32_t v) override { impl_.setMinSides(v); }
    int32_t getMinimumSides() const override { return impl_.getMinSides(); }

    void setMaximumSides(int32_t v) override { impl_.setMaxSides(v); }
    int32_t getMaximumSides() const override { return impl_.getMaxSides(); }

    bool isLoop() const override { return impl_.isLoops(); }

    void setConvex(bool convex) override { impl_.setConvex(convex); }
    bool isConvex() const override { return impl_.isConvex(); }

    // Direct access for callers that need to tune the underlying
    // PolylineSplitMerge after construction. Prefer the
    // ConfigPolylineSplitMerge ctor over poking at impl() — keeping all
    // config in one struct makes parity with upstream visible.
    PolylineSplitMerge& impl() { return impl_; }
    const PolylineSplitMerge& impl() const { return impl_; }

private:
    PolylineSplitMerge impl_;
};

// PolygonHelper interface (mirror of
// boofcv.alg.shapes.polygon.PolygonHelper). Caller hook for filtering
// during the detection pipeline. QR doesn't use it; the finder-pattern
// detector (step 7c) wires one in.
struct PolygonHelper {
    virtual ~PolygonHelper() = default;

    virtual void setImageShape(int32_t width, int32_t height) = 0;

    // Filter on raw contour pixels. Distorted=true on the first call
    // (raw pixels), false on the second call (after lens-distortion
    // removal — n/a in this port until lens distortion lands).
    virtual bool filterContour(const std::vector<cv::Point2i>& contour,
                                bool touchesBorder, bool distorted) = 0;

    // Filter on the fitted polygon. `borderCorners` holds per-corner
    // touch flags when `touchesBorder` is true.
    virtual bool filterPixelPolygon(const std::vector<cv::Point2d>& undistorted,
                                     const std::vector<cv::Point2d>& distorted,
                                     const std::vector<uint8_t>& borderCorners,
                                     bool touchesBorder) = 0;

    // Allow the helper to reconfigure the polyline fit before running.
    virtual void configureBeforePolyline(PointsToPolyline& contourToPolyline,
                                          bool touchesBorder) = 0;
};

// Computes the average gray-value of points sampled on the inside and
// outside of a contour at regular intervals. Used to discard low-contrast
// false-positive polygons.
//
// Mirror of `boofcv.alg.shapes.polygon.ContourEdgeIntensity`. Templated on
// image type in Java; we commit to `CV_8UC1` per CLAUDE.md.
class ContourEdgeIntensity {
public:
    ContourEdgeIntensity(int32_t contourSamples, int32_t tangentSamples,
                          double tangentStep);

    void setImage(const cv::Mat& image);

    void process(const std::vector<cv::Point2i>& contour, bool isCCW);

    float getEdgeInsideAverage() const { return edgeInsideAverage_; }
    float getEdgeOutsideAverage() const { return edgeOutsideAverage_; }

private:
    // Configuration (immutable after ctor — same as Java's `final` fields).
    const int32_t contourSamples_;
    const int32_t tangentSamples_;
    const float tangentStep_;

    // Bound image. Sampler uses BorderType.EXTENDED == cv::BORDER_REPLICATE
    // bilinear interpolation.
    cv::Mat image_;
    int32_t imageWidth_ = 0;
    int32_t imageHeight_ = 0;

    float edgeInsideAverage_ = 0.0f;
    float edgeOutsideAverage_ = 0.0f;

    // EXTENDED-border bilinear sample.
    float sample(float x, float y) const;
};

// Detects black-blob polygons of a given side count in a binarised
// image. The result list contains one `DetectedInfo` per accepted
// candidate.
class DetectPolygonFromContour {
public:
    // Mirror of DetectPolygonFromContour.Info.
    struct DetectedInfo {
        // Was this from an external (true) or internal (false) contour.
        bool external = true;

        // Average pixel intensity inside / outside the polygon edge.
        // Disabled (==-1) if `contourEdgeThreshold <= 0`.
        double edgeInside = -1.0;
        double edgeOutside = -1.0;

        // True if the source contour touches the image border.
        bool contourTouchesBorder = true;

        // Per-corner border-touch booleans. Empty if no corner touches.
        std::vector<uint8_t> borderCorners;

        // Polygon in undistorted image pixels.
        std::vector<cv::Point2d> polygon;
        // Polygon in original (distorted) image pixels. Until lens
        // distortion lands these are identical to `polygon`.
        std::vector<cv::Point2d> polygonDistorted;

        // Indices into the source `contour.external` where the corners
        // sit. Useful for downstream refinement which needs per-side
        // contour pixels.
        std::vector<int32_t> splits;

        // Source contour (external + internal). Held by value because
        // OpenCV's findContours gives us full point arrays.
        Contour contour;

        double computeEdgeIntensity() const { return edgeOutside - edgeInside; }
        bool hasInternal() const { return !contour.internal.empty(); }

        void reset() {
            external = false;
            edgeInside = edgeOutside = -1.0;
            contourTouchesBorder = true;
            borderCorners.clear();
            splits.clear();
            polygon.clear();
            polygonDistorted.clear();
            contour.reset();
        }
    };

    // Construct with a polyline fitter (typically `PolylineSplitMergeAdapter`).
    // `outputClockwiseUpY` mirrors the Java field with the same caveat:
    // CCW with +y up == CW with image +y down.
    //
    // For QR `ConfigQrCode` defaults: outputClockwiseUpY=false,
    // canTouchBorder=false, contourEdgeThreshold=3, tangentEdgeIntensity=1.5.
    DetectPolygonFromContour(std::unique_ptr<PointsToPolyline> contourToPolyline,
                              bool outputClockwiseUpY, bool canTouchBorder,
                              double contourEdgeThreshold,
                              double tangentEdgeIntensity);

    // Test-only ctor — leaves the polyline fitter unset. Mirrors Java's
    // protected no-arg ctor used by JUnit subclasses.
    DetectPolygonFromContour();

    // Run the detection pipeline. `gray` is `CV_8UC1`. `binary` is
    // `CV_8UC1` 0/1 or 0/255 (any non-zero counts as foreground = dark
    // module per CLAUDE.md). `binary` is cloned internally because
    // `cv::findContours` mutates its input.
    void process(const cv::Mat& gray, const cv::Mat& binary);

    // Detection results. The reference is invalidated by the next
    // `process()` call (the underlying vector is reused). Per CLAUDE.md
    // "Public API design", this owns its storage value-typed —
    // consumers wanting longer lifetimes should copy.
    const std::vector<DetectedInfo>& getFoundInfo() const { return foundInfo_; }

    // The refine wrapper needs to mutate polygons in place after
    // threshold-bias adjustment and post-refine edge-intensity update.
    // Mirrors Java's `DogArray<Info>` mutation semantics. Demoted to
    // friend access so it doesn't appear in the public API surface.
    friend class DetectPolygonBinaryGrayRefine;
    // Step 7c's finder-pattern detector walks the polygon list and
    // calls `DetectPolygonBinaryGrayRefine::refine(info)` on each
    // entry, which requires a non-const Info&.
    friend class QrCodePositionPatternDetector;

    // Configuration — getters/setters mirroring Java's @Getter/@Setter.
    bool isOutputClockwiseUpY() const { return outputClockwiseUpY_; }
    void setOutputClockwiseUpY(bool v) { outputClockwiseUpY_ = v; }

    double getContourEdgeThreshold() const { return contourEdgeThreshold_; }
    void setContourEdgeThreshold(double v) { contourEdgeThreshold_ = v; }

    // shared_ptr per CLAUDE.md "Public API design" line 32 (no raw
    // pointers in public signatures). Java's GC makes ownership trivial
    // here; in C++ shared ownership is the closest match for an
    // injectable strategy hook.
    void setHelper(std::shared_ptr<PolygonHelper> helper) {
        helper_ = std::move(helper);
    }

    // Number-of-sides forwarders (mirror Java's setNumberOfSides).
    void setNumberOfSides(int32_t minSides, int32_t maxSides);
    int32_t getMinimumSides() const;
    int32_t getMaximumSides() const;

    bool isConvex() const { return contourToPolyline_->isConvex(); }
    void setConvex(bool convex) { contourToPolyline_->setConvex(convex); }

    // Minimum-contour-pixels gate — relative-to-image-diagonal config,
    // mirroring `ConfigPolygonFromContour.minimumContour`. Defaults
    // match `ConfigPolygonFromContour` (`relative(0.044, 4)`); QR
    // overrides to `fixed(40)`.
    void setMinimumContour(const ConfigLength& v) { minimumContour_.setTo(v); }
    const ConfigLength& getMinimumContour() const { return minimumContour_; }

    // Maximum-contour cap. Mirrors Java's
    // `BinaryContourFinder.setMaxContour` — contours with more pixels
    // than this cap are *discarded* before the polyline fitter sees
    // them. Necessary for correctness on inputs containing huge black
    // rectangles (page borders, UI chrome) that would otherwise
    // produce phantom 4-corner polygon candidates passing every
    // downstream check including the QR finder's 1:1:3:1:1 gate.
    //
    // Default `fixed(-1)` matches Java's `ConfigPolygonFromContour`
    // default — interpreted as "no cap" (cap = INT_MAX). The QR
    // finder pattern detector overrides this in
    // `SquareLocatorPatternDetectorBase::configureContourDetector`.
    void setMaximumContour(const ConfigLength& v) { maximumContour_.setTo(v); }
    const ConfigLength& getMaximumContour() const { return maximumContour_; }

    // Profiling timers in milliseconds (exponential moving average).
    double getMilliContour() const { return milliContour_; }
    double getMilliShapes() const { return milliShapes_; }
    void resetRuntimeProfiling() { milliContour_ = milliShapes_ = 0.0; }

    // Lens distortion — deferred (CLAUDE.md "Public API design" calls
    // out the same deferral as for the grid reader). Calling this
    // function is currently a no-op; documented in the algorithm doc.
    void setLensDistortion(int32_t /*width*/, int32_t /*height*/) {}
    void clearLensDistortion() {}

    // Underlying polyline fitter (downcast to inspect tunables).
    PointsToPolyline& getContourToPolyline() { return *contourToPolyline_; }

    // Whether internal-hole contours are preserved in the output
    // `Contour`. Deviation from BoofCV: BoofCV's `polygonContour()`
    // factory wires `LinearExternalContours` (`isSaveInternalContours
    // = false`); we default to `true` so downstream recovery
    // pipelines (per CLAUDE.md "Public API design") can access hole
    // topology — the QR finder pattern in particular is three nested
    // squares. Documented in the algorithm doc under "Deviations
    // from upstream".
    bool isSaveInternalContours() const { return saveInternalContours_; }
    void setSaveInternalContours(bool v) { saveInternalContours_ = v; }

    // ---- methods exercised by the JUnit suite (package-private in
    //      Java) — exposed here so tests can call them directly.

    // TEST-VISIBLE — image-corner-touch test on a contour.
    bool touchesBorder(const std::vector<cv::Point2i>& contour) const;

    // TEST-VISIBLE — per-corner border-touch flag fill.
    void determineCornersOnBorder(const std::vector<cv::Point2d>& polygon,
                                   std::vector<uint8_t>& onImageBorder) const;

    // TEST-VISIBLE — in-place polygon vertex flip (preserves vertex 0,
    // reverses 1..N-1). Mirrors Java's static `flip(int[], int)`.
    static void flip(std::vector<int32_t>& a);

    // TEST-VISIBLE — image dims set externally for the JUnit unit tests
    // that don't go through process().
    void setImageDims(int32_t width, int32_t height) {
        imageWidth_ = width;
        imageHeight_ = height;
    }

private:
    void configure(int32_t width, int32_t height);
    void findCandidateShapes(const cv::Mat& gray);

    // Convert the LinearContourLabelChang2004 output (ContourPacked +
    // PackedSetsPoint2D_I32) into our per-blob external+internal
    // `Contour` shape consumed by `findCandidateShapes`. Skips
    // contours whose external set was wiped (size 0) — the labeller
    // wipes a set to empty when its pixel count violates the min/max
    // bounds.
    void buildContoursFromPort();

    // Configuration.
    std::unique_ptr<PointsToPolyline> contourToPolyline_;
    bool outputClockwiseUpY_ = false;
    bool canTouchBorder_ = false;
    double contourEdgeThreshold_ = 0.0;
    // tangentEdgeIntensity is consumed at ctor time when wiring
    // contourEdgeIntensity_; not stored as a field.

    // Per CLAUDE.md "Public API design" — shared ownership for
    // injectable hooks (no raw pointers in public signatures).
    std::shared_ptr<PolygonHelper> helper_;

    ConfigLength minimumContour_ = ConfigLength::relative(0.044, 4.0);
    // `fixed(-1)` matches Java's default — interpreted as "no cap"
    // (`computeNegMaxI` → INT32_MAX).
    ConfigLength maximumContour_ = ConfigLength::fixed(-1.0);

    // See `setSaveInternalContours` doc above. Default `true` is a
    // deviation from BoofCV's `polygonContour()` factory.
    bool saveInternalContours_ = true;

    // Internal state — image dims, computed thresholds.
    int32_t imageWidth_ = 0;
    int32_t imageHeight_ = 0;
    int32_t minimumContourPixels_ = 0;
    int32_t maximumContourPixels_ = std::numeric_limits<int32_t>::max();
    double minimumArea_ = 0.0;

    // Per-call working buffers (kept as members to avoid reallocations,
    // mirroring BoofCV's DogArray-recycling pattern).
    std::vector<int32_t> splits_;
    std::vector<uint8_t> borderCorners_;
    std::vector<cv::Point2d> polygonWork_;
    std::vector<cv::Point2d> polygonDistorted_;
    std::vector<cv::Point2i> contourTmp_;  // not used in the OpenCV path
                                            // but kept for parity
    std::vector<cv::Point2i> polygonPixel_;

    // Edge-intensity scorer (active only when contourEdgeThreshold > 0).
    std::unique_ptr<ContourEdgeIntensity> contourEdgeIntensity_;

    // Detection results.
    std::vector<DetectedInfo> foundInfo_;

    // Working store for the per-blob contours produced by the
    // `LinearContourLabelChang2004` port.
    std::vector<Contour> contours_;

    // The Chang2004 contour-extractor port (replaces the prior
    // `cv::findContours(RETR_CCOMP, CHAIN_APPROX_NONE)` call site —
    // see `src/binary/linear_contour_label_chang2004.md` and ADR 01).
    // Connect rule EIGHT matches `ConfigQrCode.polygon.detector.contourRule`.
    LinearContourLabelChang2004 contourLabeller_{ConnectRule::EIGHT};
    // Scratch label image — `CV_32SC1`, same dims as the input binary,
    // re-allocated lazily by `LinearContourLabelChang2004::process`.
    cv::Mat labeled_;

    // Profiling.
    double milliContour_ = 0.0;
    double milliShapes_ = 0.0;
};

}  // namespace boofcv_qr

#endif  // BOOFCV_QR_POLYGON_DETECT_POLYGON_FROM_CONTOUR_HPP
