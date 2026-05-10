// Mirrors TestLinearContourLabelChang2004.java
// (BoofCV v1.3.0, boofcv-ip/src/test/java/boofcv/alg/filter/binary/).
//
// Same 4 hand-crafted binary fixtures, same 7 tests, same expected
// contour counts + the inner/outer structural assertion.

#include "boofcv_qr/binary/linear_contour_label_chang2004.hpp"

#include <gtest/gtest.h>

#include <opencv2/core.hpp>

#include <cstdint>
#include <vector>

using boofcv_qr::ConnectRule;
using boofcv_qr::ContourPacked;
using boofcv_qr::LinearContourLabelChang2004;
using boofcv_qr::PackedSetsPoint2D_I32;

namespace {

// Mirror of `new GrayU8(byte[][])`. Each row is one scan-line; the
// 2D layout is stored row-major into a CV_8UC1 cv::Mat.
cv::Mat makeBinary(const std::vector<std::vector<uint8_t>>& rows) {
    int32_t h = static_cast<int32_t>(rows.size());
    int32_t w = static_cast<int32_t>(rows[0].size());
    cv::Mat m(h, w, CV_8UC1);
    for (int32_t y = 0; y < h; y++) {
        for (int32_t x = 0; x < w; x++) {
            m.at<uint8_t>(y, x) = rows[static_cast<std::size_t>(y)]
                                       [static_cast<std::size_t>(x)];
        }
    }
    return m;
}

const std::vector<std::vector<uint8_t>> TEST1 = {
    {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0},
    {0, 0, 1, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0},
    {0, 0, 1, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0},
    {0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0},
};

const std::vector<std::vector<uint8_t>> TEST2 = {
    {0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0},
    {0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0},
    {0, 0, 1, 0, 0, 1, 0, 1, 0, 1, 1, 1, 0},
    {0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 1, 0},
    {0, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0},
    {0, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1, 1, 0},
    {1, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0},
    {0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0},
};

const std::vector<std::vector<uint8_t>> TEST3 = {
    {0, 0, 0, 0, 0},
    {0, 1, 1, 1, 0},
    {0, 1, 1, 1, 0},
    {0, 1, 0, 1, 0},
    {0, 1, 1, 1, 0},
    {0, 0, 0, 0, 0},
};

const std::vector<std::vector<uint8_t>> TEST4 = {
    {0, 0, 0, 0, 0, 0, 0},
    {0, 0, 1, 1, 1, 1, 1},
    {0, 1, 0, 1, 1, 1, 1},
    {0, 1, 1, 1, 0, 1, 1},
    {0, 1, 1, 1, 1, 1, 1},
    {0, 1, 1, 1, 1, 1, 1},
    {0, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0},
};

// "local" — Java's 8 Moore-neighbour offsets (closed loop).
const std::vector<cv::Point2i> kLocal = {
    {-1, -1}, {0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1},
    {-1, 1}, {-1, 0}, {-1, -1},
};

// Mirrors the JUnit's `findContour8`/`findContour4`. Returns an
// unordered list of all pixels that have label `target` AND are on
// the blob boundary by the chosen connectivity rule. The boundary
// criterion matches Java exactly.
int32_t labeledAt(const cv::Mat& labeled, int32_t x, int32_t y) {
    if (x < 0 || y < 0 || x >= labeled.cols || y >= labeled.rows) return 0;
    return labeled.at<int32_t>(y, x);
}

std::vector<cv::Point2i> findContour8(const cv::Mat& labeled, int32_t target) {
    std::vector<cv::Point2i> list;
    for (int32_t y = 0; y < labeled.rows; y++) {
        for (int32_t x = 0; x < labeled.cols; x++) {
            if (target == labeledAt(labeled, x, y)) {
                bool isContour = false;
                for (std::size_t i = 0; i + 1 < kLocal.size(); i++) {
                    const cv::Point2i& a = kLocal[i];
                    const cv::Point2i& b = kLocal[i + 1];
                    if (labeledAt(labeled, x + a.x, y + a.y) != target &&
                        labeledAt(labeled, x + b.x, y + b.y) != target) {
                        isContour = true;
                        break;
                    }
                }
                if (!isContour && labeledAt(labeled, x + 1, y) != target)
                    isContour = true;
                if (!isContour && labeledAt(labeled, x - 1, y) != target)
                    isContour = true;
                if (!isContour && labeledAt(labeled, x, y + 1) != target)
                    isContour = true;
                if (!isContour && labeledAt(labeled, x, y - 1) != target)
                    isContour = true;

                if (isContour) list.emplace_back(x, y);
            }
        }
    }
    return list;
}

std::vector<cv::Point2i> findContour4(const cv::Mat& labeled, int32_t target) {
    std::vector<cv::Point2i> list;
    for (int32_t y = 0; y < labeled.rows; y++) {
        for (int32_t x = 0; x < labeled.cols; x++) {
            if (target == labeledAt(labeled, x, y)) {
                bool isContour = false;
                for (std::size_t i = 0; i < kLocal.size(); i++) {
                    const cv::Point2i& a = kLocal[i];
                    if (labeledAt(labeled, x + a.x, y + a.y) != target) {
                        isContour = true;
                    }
                }
                if (isContour) list.emplace_back(x, y);
            }
        }
    }
    return list;
}

std::vector<cv::Point2i> removeDuplicates(const std::vector<cv::Point2i>& in) {
    std::vector<cv::Point2i> ret;
    for (std::size_t i = 0; i < in.size(); i++) {
        const cv::Point2i& p = in[i];
        bool matched = false;
        for (std::size_t j = i + 1; j < in.size(); j++) {
            const cv::Point2i& c = in[j];
            if (p.x == c.x && p.y == c.y) {
                matched = true;
                break;
            }
        }
        if (!matched) ret.push_back(p);
    }
    return ret;
}

void addPointsToList(LinearContourLabelChang2004& alg, int32_t set,
                     std::vector<cv::Point2i>& list) {
    PackedSetsPoint2D_I32::SetIterator iter = alg.getPackedPoints().createIterator();
    iter.setup(set);
    while (iter.hasNext()) {
        list.push_back(iter.next());
    }
}

void checkContour(LinearContourLabelChang2004& alg, const cv::Mat& labeled,
                  int32_t rule) {
    const std::vector<ContourPacked>& contours = alg.getContours();

    for (std::size_t i = 0; i < contours.size(); i++) {
        const ContourPacked& c = contours[i];
        ASSERT_GT(c.id, 0);

        std::vector<cv::Point2i> found;
        addPointsToList(alg, c.externalIndex, found);
        for (std::size_t j = 0; j < c.internalIndexes.size(); j++) {
            addPointsToList(alg, c.internalIndexes[j], found);
        }

        // there can be duplicate points, remove them
        found = removeDuplicates(found);

        // see if the two lists are equivalent
        std::vector<cv::Point2i> expected = (rule == 8)
            ? findContour8(labeled, c.id)
            : findContour4(labeled, c.id);

        ASSERT_EQ(expected.size(), found.size());

        for (const cv::Point2i& f : found) {
            bool match = false;
            for (const cv::Point2i& e : expected) {
                if (f.x == e.x && f.y == e.y) {
                    match = true;
                    break;
                }
            }
            ASSERT_TRUE(match);
        }
    }
}

}  // namespace

TEST(LinearContourLabelChang2004, test1_4) {
    cv::Mat input = makeBinary(TEST1);
    cv::Mat labeled(input.rows, input.cols, CV_32SC1);
    LinearContourLabelChang2004 alg(ConnectRule::FOUR);
    alg.process(input, labeled);

    ASSERT_EQ(static_cast<std::size_t>(2), alg.getContours().size());
    checkContour(alg, labeled, 4);
}

TEST(LinearContourLabelChang2004, test1_8) {
    cv::Mat input = makeBinary(TEST1);
    cv::Mat labeled(input.rows, input.cols, CV_32SC1);
    LinearContourLabelChang2004 alg(ConnectRule::EIGHT);
    alg.process(input, labeled);

    ASSERT_EQ(static_cast<std::size_t>(1), alg.getContours().size());
    checkContour(alg, labeled, 8);
}

TEST(LinearContourLabelChang2004, test2_4) {
    cv::Mat input = makeBinary(TEST2);
    cv::Mat labeled(input.rows, input.cols, CV_32SC1);
    LinearContourLabelChang2004 alg(ConnectRule::FOUR);
    alg.process(input, labeled);

    ASSERT_EQ(static_cast<std::size_t>(14), alg.getContours().size());
    checkContour(alg, labeled, 4);
}

TEST(LinearContourLabelChang2004, test2_8) {
    cv::Mat input = makeBinary(TEST2);
    cv::Mat labeled(input.rows, input.cols, CV_32SC1);
    LinearContourLabelChang2004 alg(ConnectRule::EIGHT);
    alg.process(input, labeled);

    ASSERT_EQ(static_cast<std::size_t>(4), alg.getContours().size());
    checkContour(alg, labeled, 8);
}

TEST(LinearContourLabelChang2004, test3_4) {
    cv::Mat input = makeBinary(TEST4);
    cv::Mat labeled(input.rows, input.cols, CV_32SC1);
    LinearContourLabelChang2004 alg(ConnectRule::FOUR);
    alg.process(input, labeled);

    ASSERT_EQ(static_cast<std::size_t>(1), alg.getContours().size());
    checkContour(alg, labeled, 4);
}

TEST(LinearContourLabelChang2004, test3_8) {
    cv::Mat input = makeBinary(TEST4);
    cv::Mat labeled(input.rows, input.cols, CV_32SC1);
    LinearContourLabelChang2004 alg(ConnectRule::EIGHT);
    alg.process(input, labeled);

    ASSERT_EQ(static_cast<std::size_t>(1), alg.getContours().size());
    checkContour(alg, labeled, 8);
}

TEST(LinearContourLabelChang2004, checkInnerOuterContour) {
    cv::Mat input = makeBinary(TEST3);
    cv::Mat labeled(input.rows, input.cols, CV_32SC1);
    LinearContourLabelChang2004 alg(ConnectRule::EIGHT);
    alg.process(input, labeled);

    ASSERT_EQ(static_cast<std::size_t>(1), alg.getContours().size());
    checkContour(alg, labeled, 8);

    const ContourPacked& c = alg.getContours()[0];
    ASSERT_EQ(10, alg.getPackedPoints().sizeOfSet(c.externalIndex));
    ASSERT_EQ(static_cast<std::size_t>(1), c.internalIndexes.size());
    ASSERT_EQ(4, alg.getPackedPoints().sizeOfSet(c.externalIndex + 1));
}
