// Port of boofcv.alg.shapes.polyline.splitmerge.PolylineSplitMerge
// (BoofCV v1.3.0). Step 7b.
//
// See src/polygon/polyline_split_merge.md for the algorithm explanation
// and the rationale for departures from upstream Java types.
//
// Per CLAUDE.md type mappings:
//   Point2D_I32 -> cv::Point2i
//   DogArray<Corner> -> CornerPool over std::vector<unique_ptr<Corner>>
//                       with logical active size (stable Corner pointers;
//                       CornerList holds Corner*)
//   DogArray<CandidatePolyline> -> std::vector<CandidatePolyline> (value
//                       typed; matches Java's DogArray<CandidatePolyline>
//                       which stores values, not pointers)
//   DogLinkedList<Corner> -> in-house CornerList with recycled nodes
//   ConfigLength -> nested ConfigLength struct
//   LineParametric2D_F64 -> nested LineParametric2D struct
//   LineSegment2D_F64 -> nested LineSegment2D struct (kept local; the
//     boofcv_qr::LineSegment2D in squares/square_graph.hpp is a private
//     impl detail of the squares port and uses Point2D_F64 — same shape
//     so we re-use that type).

#ifndef BOOFCV_QR_POLYLINE_POLYLINE_SPLIT_MERGE_HPP
#define BOOFCV_QR_POLYLINE_POLYLINE_SPLIT_MERGE_HPP

#include <opencv2/core.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace boofcv_qr {

// Mirror of boofcv.struct.ConfigLength — port reduced to the methods
// PolylineSplitMerge actually exercises. Full ConfigLength has more
// configuration helpers we don't need.
struct ConfigLength {
    double length = -1.0;    // fixed length, OR minimum when fraction>=0
    double fraction = -1.0;  // -1 = fixed, >=0 = relative

    static ConfigLength fixed(double len) {
        ConfigLength c;
        c.length = len;
        c.fraction = -1.0;
        return c;
    }

    static ConfigLength relative(double frac, double minimum) {
        ConfigLength c;
        c.length = minimum;
        c.fraction = frac;
        return c;
    }

    // Mirrors `ConfigLength.compute(totalLength)`.
    double compute(double totalLength) const {
        double size;
        if (fraction >= 0.0) {
            size = fraction * totalLength;
            size = std::max(size, length);
        } else {
            size = length;
        }
        return size;
    }

    // Mirrors `ConfigLength.computeI(totalLength)` — rounded int.
    // Throws on negative threshold.
    int32_t computeI(double totalLength) const;

    void setTo(const ConfigLength& src) {
        length = src.length;
        fraction = src.fraction;
    }
};

// Storage for the fitted polyline — mirrors PolylineSplitMerge.CandidatePolyline.
struct CandidatePolyline {
    std::vector<int32_t> splits;     // contour-index of each corner
    double score = 0.0;
    double maxSideError = 0.0;
    std::vector<double> sideErrors;  // per-side mean SSE

    void reset() {
        splits.clear();
        sideErrors.clear();
        score = std::numeric_limits<double>::quiet_NaN();
        maxSideError = std::numeric_limits<double>::quiet_NaN();
    }
};

// Mirrors PolylineSplitMerge.ErrorValue (a tiny by-ref scalar).
struct ErrorValue {
    double value = 0.0;
};

// Forward decl for friend-of-Corner static accessor.
class PolylineSplitMerge;

// SplitSelector interface (mirror of boofcv.alg.shapes.polyline.splitmerge.SplitSelector).
struct SplitSelector {
    virtual ~SplitSelector() = default;

    // Storage for results from selecting where to split a line.
    // (Java has it nested in PolylineSplitMerge as PolylineSplitMerge.SplitResults.
    //  We expose it here at namespace scope for SplitSelector to reference.)
    struct Results {
        int32_t index = 0;
        double score = 0.0;
    };

    virtual void selectSplitPoint(const std::vector<cv::Point2i>& contour,
                                  int32_t indexA, int32_t indexB,
                                  Results& results) = 0;

    virtual int32_t compareScore(double scoreA, double scoreB) = 0;
};

// MaximumLineDistance: select the contour point farthest from the chord.
// Mirror of boofcv.alg.shapes.polyline.splitmerge.MaximumLineDistance.
class MaximumLineDistance : public SplitSelector {
public:
    void selectSplitPoint(const std::vector<cv::Point2i>& contour,
                          int32_t indexA, int32_t indexB,
                          Results& results) override;

    int32_t compareScore(double scoreA, double scoreB) override;

private:
    // Same workspace member layout as Java (one parametric line, reused).
    // Inlined struct to avoid pulling in georegression.
    struct LineParametric2D {
        double px = 0.0, py = 0.0;
        double sx = 0.0, sy = 0.0;
    } line;
};

class PolylineSplitMerge {
public:
    // ---- nested types matching upstream ------------------------------

    // Mirror of PolylineSplitMerge.Corner.
    struct Corner {
        int32_t index = -1;
        double sideError = -1.0;
        // if this side was to be split this is where it would be split and what the scores
        // for the new sides would be
        int32_t splitLocation = -1;
        double splitError0 = -1.0, splitError1 = -1.0;

        // if a side can't be split (e.g. too small or already perfect)
        bool splitable = true;

        void reset() {
            index = -1;
            sideError = -1;
            splitLocation = -1;
            splitError0 = splitError1 = -1;
            splitable = true;
        }
    };

    using SplitResults = SplitSelector::Results;

    // Mirrors DogArray<Corner>: stable Corner pointers, recycled across reset().
    class CornerPool {
    public:
        Corner* grow();
        void reset();
        std::size_t size() const { return activeSize_; }
        Corner* get(std::size_t i) const { return storage_[i].get(); }

    private:
        std::vector<std::unique_ptr<Corner>> storage_;
        std::size_t activeSize_ = 0;
    };

    // Mirrors DogLinkedList<Corner>. We hold Corner* in a small linked list
    // with pooled nodes; iterators give the Element<Corner> handle. end() is
    // the "null Element" sentinel.
    class CornerList {
        struct Node {
            Corner* value = nullptr;
            Node* prev = nullptr;
            Node* next = nullptr;
        };

    public:
        class Iter {
        public:
            Iter() = default;

            Corner*& operator*() const { return node_->value; }

            Iter& operator++() {
                node_ = node_->next;
                return *this;
            }

            Iter& operator--() {
                node_ = node_->prev;
                return *this;
            }

            bool operator==(const Iter& other) const {
                return node_ == other.node_;
            }

            bool operator!=(const Iter& other) const {
                return node_ != other.node_;
            }

        private:
            friend class CornerList;

            explicit Iter(Node* node) : node_(node) {}

            Node* node_ = nullptr;
        };

        Iter pushTail(Corner* c) {
            Node* n = growNode(c);
            n->prev = tail_;
            n->next = nullptr;
            if (tail_ != nullptr) {
                tail_->next = n;
            } else {
                head_ = n;
            }
            tail_ = n;
            listSize_++;
            return Iter(n);
        }
        Iter insertAfter(Iter where, Corner* c) {
            Node* w = where.node_;
            Node* n = growNode(c);
            n->prev = w;
            n->next = w->next;
            if (w->next != nullptr) {
                w->next->prev = n;
            } else {
                tail_ = n;
            }
            w->next = n;
            listSize_++;
            return Iter(n);
        }
        void remove(Iter it) {
            Node* n = it.node_;
            if (n->prev != nullptr) {
                n->prev->next = n->next;
            } else {
                head_ = n->next;
            }
            if (n->next != nullptr) {
                n->next->prev = n->prev;
            } else {
                tail_ = n->prev;
            }
            n->value = nullptr;
            n->prev = nullptr;
            n->next = nullptr;
            listSize_--;
        }
        void reset() {
            head_ = nullptr;
            tail_ = nullptr;
            activeSize_ = 0;
            listSize_ = 0;
        }
        std::size_t size() const { return listSize_; }

        Iter getHead() { return Iter(head_); }
        Iter getTail() { return Iter(tail_); }
        Iter end() { return Iter(); }

        // Mirrors DogLinkedList.find(T): returns iterator pointing at the
        // element whose Corner* matches, or end() if not found.
        Iter find(const Corner* c) {
            for (Node* n = head_; n != nullptr; n = n->next) {
                if (n->value == c) return Iter(n);
            }
            return end();
        }

        // Mirrors DogLinkedList.getElement(int index, boolean forward).
        // forward=true counts from head; forward=false counts from tail
        // (BoofCV uses this for fast access on either side).
        Iter getElement(int32_t index, bool forward) {
            if (forward) {
                Node* n = head_;
                for (int32_t k = 0; k < index; k++) n = n->next;
                return Iter(n);
            } else {
                Node* n = tail_;
                for (int32_t k = 0; k < index; k++) n = n->prev;
                return Iter(n);
            }
        }

        // True if the iterator points at the head.
        bool isHead(Iter it) const { return it.node_ == head_; }
        // True if the iterator points at the tail (last real element).
        bool isTail(Iter it) const {
            return tail_ != nullptr && it.node_ == tail_;
        }

    private:
        Node* growNode(Corner* c) {
            if (activeSize_ == storage_.size()) {
                storage_.push_back(std::make_unique<Node>());
            }
            Node* n = storage_[activeSize_++].get();
            n->value = c;
            n->prev = nullptr;
            n->next = nullptr;
            return n;
        }

        std::vector<std::unique_ptr<Node>> storage_;
        std::size_t activeSize_ = 0;
        std::size_t listSize_ = 0;
        Node* head_ = nullptr;
        Node* tail_ = nullptr;
    };

    // ---- configuration getters/setters (mirror @Getter/@Setter) ------

    bool isLoops() const { return loops_; }
    void setLoops(bool v) { loops_ = v; }

    bool isConvex() const { return convex_; }
    void setConvex(bool v) { convex_ = v; }

    int32_t getMaxSides() const { return maxSides_; }
    void setMaxSides(int32_t v) { maxSides_ = v; }

    int32_t getMinSides() const { return minSides_; }
    void setMinSides(int32_t v) { minSides_ = v; }

    int32_t getMinimumSideLength() const { return minimumSideLength_; }
    void setMinimumSideLength(int32_t v);

    const ConfigLength& getExtraConsider() const { return extraConsider_; }
    void setExtraConsider(const ConfigLength& v) { extraConsider_.setTo(v); }

    double getCornerScorePenalty() const { return cornerScorePenalty_; }
    void setCornerScorePenalty(double v) { cornerScorePenalty_ = v; }

    double getThresholdSideSplitScore() const { return thresholdSideSplitScore_; }
    void setThresholdSideSplitScore(double v) { thresholdSideSplitScore_ = v; }

    int32_t getMaxNumberOfSideSamples() const { return maxNumberOfSideSamples_; }
    void setMaxNumberOfSideSamples(int32_t v) { maxNumberOfSideSamples_ = v; }

    double getConvexTest() const { return convexTest_; }
    void setConvexTest(double v) { convexTest_ = v; }

    const ConfigLength& getMaxSideError() const { return maxSideError_; }
    void setMaxSideError(const ConfigLength& v) { maxSideError_.setTo(v); }

    void setSplitter(std::unique_ptr<SplitSelector> s) {
        splitter_ = std::move(s);
    }

    // ---- public algorithmic API --------------------------------------

    bool process(const std::vector<cv::Point2i>& contour);

    // The set of saved polylines, indexed by `(numSides - MIN_SIZE)` where
    // MIN_SIZE is 3 for closed contours / 2 for open. CLAUDE.md "Public API
    // design" forbids `unique_ptr` in public signatures — storage is now
    // `std::vector<CandidatePolyline>` directly (value-typed; matches Java's
    // `DogArray<CandidatePolyline>`).
    const std::vector<CandidatePolyline>& getPolylines() const {
        return polylines_;
    }

    // Returns the polyline with the best score or nullopt if process()
    // failed to set one. Value-typed return per CLAUDE.md "Public API
    // design".
    std::optional<CandidatePolyline> getBestPolyline() const {
        if (bestPolylineIndex_ < 0) return std::nullopt;
        return polylines_[static_cast<std::size_t>(bestPolylineIndex_)];
    }

    // ---- methods exercised by the JUnit suite (package-private in Java)
    //      need to be callable from tests; expose them here. The header
    //      `// TEST-VISIBLE` markers tag them so future readers don't
    //      mistake them for general public API.

    // TEST-VISIBLE
    bool savePolyline();
    // TEST-VISIBLE
    static double computeScore(CornerList& list, double cornerPenalty, bool loops);
    // TEST-VISIBLE
    bool findInitialTriangle(const std::vector<cv::Point2i>& contour);
    // TEST-VISIBLE
    void ensureTriangleOrder(const std::vector<cv::Point2i>& contour);
    // TEST-VISIBLE
    CornerList::Iter addCorner(int32_t where);
    // TEST-VISIBLE
    bool increaseNumberOfSidesByOne(const std::vector<cv::Point2i>& contour, bool loops);
    // TEST-VISIBLE
    bool isSideConvex(const std::vector<cv::Point2i>& contour, CornerList::Iter e1);
    // TEST-VISIBLE — returns end() to signal "no corner picked" (mirrors @Nullable null in Java).
    CornerList::Iter selectCornerToSplit(bool loops);
    // TEST-VISIBLE — returns end() if list has 3 sides or fewer.
    CornerList::Iter selectCornerToRemove(const std::vector<cv::Point2i>& contour,
                                          ErrorValue& sideError, bool loops);
    // TEST-VISIBLE
    bool removeCornerAndSavePolyline(CornerList::Iter corner, double sideErrorAfterRemoved);
    // TEST-VISIBLE
    static int32_t findCornerSeed(const std::vector<cv::Point2i>& contour);
    // TEST-VISIBLE
    static int32_t maximumDistance(const std::vector<cv::Point2i>& contour,
                                   int32_t indexA, int32_t indexB);
    // TEST-VISIBLE
    double computeSideError(const std::vector<cv::Point2i>& contour,
                            int32_t indexA, int32_t indexB);
    // TEST-VISIBLE
    void computePotentialSplitScore(const std::vector<cv::Point2i>& contour,
                                    CornerList::Iter e0, bool mustSplit);
    // TEST-VISIBLE
    void setSplitVariables(const std::vector<cv::Point2i>& contour,
                           CornerList::Iter e0, CornerList::Iter e1);
    // TEST-VISIBLE
    bool canBeSplit(const std::vector<cv::Point2i>& contour,
                    CornerList::Iter e0, bool mustSplit);
    // TEST-VISIBLE — returns wrap-around next of `e`.
    CornerList::Iter next(CornerList::Iter e);
    // TEST-VISIBLE — returns wrap-around previous of `e`.
    CornerList::Iter previous(CornerList::Iter e);
    // TEST-VISIBLE
    static bool isConvexUsingMaxDistantPoints(const std::vector<cv::Point2i>& contour,
                                              int32_t indexA, int32_t indexB);
    // TEST-VISIBLE
    static double distanceSq(const cv::Point2i& a, const cv::Point2i& b);
    // TEST-VISIBLE
    static double distanceAbs(const cv::Point2i& a, const cv::Point2i& b);

    // TEST-VISIBLE — assignLine overloads (parametric / segment).
    struct LineParametric2D {
        double px = 0.0, py = 0.0;
        double sx = 0.0, sy = 0.0;
    };
    struct LineSegment2D {
        double ax = 0.0, ay = 0.0;
        double bx = 0.0, by = 0.0;
    };
    static void assignLine(const std::vector<cv::Point2i>& contour, int32_t indexA,
                           int32_t indexB, LineParametric2D& line);
    static void assignLine(const std::vector<cv::Point2i>& contour, int32_t indexA,
                           int32_t indexB, LineSegment2D& line);

    // ---- expose pool/list for the JUnit-equivalent tests (Java tests
    //      reach into package-private fields directly).

    CornerList& list() { return list_; }
    CornerPool& corners() { return corners_; }

private:
    void reset();
    void sequentialSideFit(const std::vector<cv::Point2i>& contour, bool loops);
    bool initializeScore(const std::vector<cv::Point2i>& contour, bool loops);

    // ---- configuration -----------------------------------------------

    bool loops_ = true;
    bool convex_ = false;
    int32_t maxSides_ = std::numeric_limits<int32_t>::max();
    int32_t minSides_ = 3;
    int32_t minimumSideLength_ = 10;
    ConfigLength extraConsider_ = ConfigLength::relative(1.0, 0.0);
    double cornerScorePenalty_ = 0.25;
    double thresholdSideSplitScore_ = 0.0;
    int32_t maxNumberOfSideSamples_ = 50;
    double convexTest_ = 2.5;
    ConfigLength maxSideError_ = ConfigLength::relative(0.1, 3.0);

    // ---- state -------------------------------------------------------

    // work space for side score calculation
    LineSegment2D line_;

    CornerList list_;
    CornerPool corners_;

    std::unique_ptr<SplitSelector> splitter_ = std::make_unique<MaximumLineDistance>();
    SplitResults resultsA_;
    SplitResults resultsB_;

    // List of all the found polylines and their score. Value-typed (per
    // CLAUDE.md "Public API design" — no unique_ptr in storage that the
    // public surface returns by reference). Capacity is reserved by
    // savePolyline()'s overwrite-vs-append logic so growth doesn't
    // invalidate per-iteration access patterns within a single process().
    std::vector<CandidatePolyline> polylines_;
    // -1 means "no best polyline picked"; otherwise an index into polylines_.
    int32_t bestPolylineIndex_ = -1;

    // if true that means a fatal error and no polygon can be fit
    bool fatalError_ = false;

    // storage for results
    ErrorValue sideError_;
};

}  // namespace boofcv_qr

#endif  // BOOFCV_QR_POLYLINE_POLYLINE_SPLIT_MERGE_HPP
