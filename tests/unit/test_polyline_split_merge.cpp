// Mirrors TestPolylineSplitMerge.java + TestMaximumLineDistance.java.
// Upstream: BoofCV v1.3.0.

#include "boofcv_qr/polyline/polyline_split_merge.hpp"

#include <gtest/gtest.h>

#include <opencv2/core.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using boofcv_qr::CandidatePolyline;
using boofcv_qr::ConfigLength;
using boofcv_qr::MaximumLineDistance;
using boofcv_qr::PolylineSplitMerge;
using boofcv_qr::SplitSelector;
using Iter = PolylineSplitMerge::CornerList::Iter;
using Corner = PolylineSplitMerge::Corner;

namespace {

constexpr double TEST_F64 = 1e-8;

// Mirrors `TestPolylineSplitMerge.line(int x0,int y0,int x1,int y1)`.
std::vector<cv::Point2i> make_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    std::vector<cv::Point2i> out;
    int32_t lengthY = std::abs(y1 - y0);
    int32_t lengthX = std::abs(x1 - x0);
    int32_t x, y;
    if (lengthY > lengthX) {
        for (int32_t i = 0; i < lengthY; i++) {
            x = x0 + (x1 - x0) * lengthX * i / lengthY;
            y = y0 + (y1 - y0) * i / lengthY;
            out.push_back({x, y});
        }
    } else {
        for (int32_t i = 0; i < lengthX; i++) {
            x = x0 + (x1 - x0) * i / lengthX;
            y = y0 + (y1 - y0) * lengthY * i / lengthX;
            out.push_back({x, y});
        }
    }
    return out;
}

// Mirrors `TestPolylineSplitMerge.rect(int x0,int y0,int x1,int y1)`.
std::vector<cv::Point2i> make_rect(int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    std::vector<cv::Point2i> out;
    auto add = [&](const std::vector<cv::Point2i>& v) {
        out.insert(out.end(), v.begin(), v.end());
    };
    add(make_line(x0, y0, x1, y0));
    add(make_line(x1, y0, x1, y1));
    add(make_line(x1, y1, x0, y1));
    add(make_line(x0, y1, x0, y0));
    return out;
}

std::vector<cv::Point2i> flip_contour(const std::vector<cv::Point2i>& in) {
    std::vector<cv::Point2i> out;
    for (auto it = in.rbegin(); it != in.rend(); ++it) out.push_back(*it);
    return out;
}

}  // namespace

TEST(PolylineSplitMerge, process_line) {
    PolylineSplitMerge alg;
    alg.setCornerScorePenalty(0.1);
    alg.setMinimumSideLength(5);
    alg.setMaxNumberOfSideSamples(10);
    alg.setConvex(false);
    alg.setLoops(false);

    std::vector<cv::Point2i> contour = make_line(5, 2, 20, 2);

    // Java test does NOT assert process() return value — `bestPolyline` is set
    // by the saving stage regardless of whether the final `bestSize<minSides`
    // gate trips (default minSides=3, this is a 2-corner result).
    alg.process(contour);
    auto result = alg.getBestPolyline();
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(2u, result->splits.size());
    EXPECT_EQ(0, result->splits[0]);
    EXPECT_EQ(static_cast<int32_t>(contour.size()) - 1, result->splits[1]);
    EXPECT_NEAR(0.1 * 1, result->score, 1e-8);

    // flip the line around and see if that changes the results
    contour = flip_contour(contour);
    alg.process(contour);
    result = alg.getBestPolyline();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(2u, result->splits.size());
    EXPECT_EQ(0, result->splits[0]);
    EXPECT_NEAR(0.1 * 1, result->score, 1e-8);
}

TEST(PolylineSplitMerge, process_twoSegments) {
    PolylineSplitMerge alg;
    alg.setCornerScorePenalty(0.1);
    alg.setMinimumSideLength(5);
    alg.setMaxNumberOfSideSamples(10);
    alg.setConvex(false);
    alg.setLoops(false);

    std::vector<cv::Point2i> contour = make_line(5, 2, 20, 2);
    auto seg2 = make_line(20, 2, 20, 30);
    contour.insert(contour.end(), seg2.begin(), seg2.end());

    EXPECT_TRUE(alg.process(contour));
    auto result = alg.getBestPolyline();
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(3u, result->splits.size());
    EXPECT_EQ(0, result->splits[0]);
    EXPECT_EQ(static_cast<int32_t>(contour.size()) - 1, result->splits[2]);
    EXPECT_NEAR(0.1 * 2, result->score, 1e-8);

    // flip the line around and see if that changes the results
    contour = flip_contour(contour);
    EXPECT_TRUE(alg.process(contour));
    result = alg.getBestPolyline();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(3u, result->splits.size());
    EXPECT_EQ(0, result->splits[0]);
    EXPECT_EQ(static_cast<int32_t>(contour.size()) - 1, result->splits[2]);
    EXPECT_NEAR(0.1 * 2, result->score, 1e-8);
}

TEST(PolylineSplitMerge, process_perfectSquare) {
    PolylineSplitMerge alg;
    alg.setCornerScorePenalty(0.1);
    alg.setMinimumSideLength(5);
    alg.setMaxNumberOfSideSamples(10);
    alg.setConvex(true);

    std::vector<cv::Point2i> contour = make_rect(10, 12, 20, 24);

    EXPECT_TRUE(alg.process(contour));
    auto result = alg.getBestPolyline();
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(4u, result->splits.size());
    EXPECT_NEAR(0.1 * 4, result->score, 1e-8);

    // set a limit to the number of sides. Test in response to a bug.
    alg.setMaxSides(4);
    alg.setExtraConsider(ConfigLength::fixed(2));
    EXPECT_TRUE(alg.process(contour));
    result = alg.getBestPolyline();
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(4u, result->splits.size());
    EXPECT_NEAR(0.1 * 4, result->score, 1e-8);
}

TEST(PolylineSplitMerge, process_perfectSquare_forcedTriangle) {
    PolylineSplitMerge alg;
    alg.setCornerScorePenalty(0.1);
    alg.setMinimumSideLength(5);
    alg.setMaxNumberOfSideSamples(10);
    alg.setConvex(true);
    alg.setMaxSides(3);
    alg.setExtraConsider(ConfigLength::fixed(2));
    alg.setMaxSideError(ConfigLength::fixed(1000));  // allow for a huge error

    std::vector<cv::Point2i> contour = make_rect(10, 12, 20, 24);

    EXPECT_TRUE(alg.process(contour));
    auto result = alg.getBestPolyline();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(3u, result->splits.size());

    // make it have a stricter error test and it should fail
    alg.setMaxSideError(ConfigLength::fixed(1));
    EXPECT_FALSE(alg.process(contour));
    EXPECT_FALSE(alg.getBestPolyline().has_value());
}

TEST(PolylineSplitMerge, savePolyline) {
    PolylineSplitMerge alg;
    alg.setCornerScorePenalty(0.5);
    alg.addCorner(0);
    alg.addCorner(0);
    alg.addCorner(0);
    EXPECT_TRUE(alg.savePolyline());
    auto e = alg.addCorner(0);
    (*e)->sideError = 10;
    EXPECT_TRUE(alg.savePolyline());
    alg.addCorner(0);
    EXPECT_TRUE(alg.savePolyline());

    EXPECT_GT(alg.getPolylines()[1].score, 2);
    EXPECT_EQ(4u, alg.getPolylines()[1].splits.size());

    // remove the bad corner and save again. The new polyline should be saved on top of the old one
    alg.list().remove(alg.list().getElement(3, true));
    EXPECT_TRUE(alg.savePolyline());

    EXPECT_LT(alg.getPolylines()[1].score, 2);
    EXPECT_EQ(4u, alg.getPolylines()[1].splits.size());

    // there should be no change now
    EXPECT_FALSE(alg.savePolyline());
}

TEST(PolylineSplitMerge, computeScore_loops) {
    PolylineSplitMerge alg;
    (*alg.addCorner(0))->sideError = 5;
    (*alg.addCorner(0))->sideError = 6;
    (*alg.addCorner(0))->sideError = 1;

    double expected = 12 / 3.0 + 0.5 * 3;
    double found = PolylineSplitMerge::computeScore(alg.list(), 0.5, true);

    EXPECT_NEAR(expected, found, 1e-8);
}

TEST(PolylineSplitMerge, computeScore_sequence) {
    PolylineSplitMerge alg;
    (*alg.addCorner(0))->sideError = 5;
    (*alg.addCorner(0))->sideError = 6;
    (*alg.addCorner(0))->sideError = 1;

    double expected = 11 / 2.0 + 0.5 * 2;
    double found = PolylineSplitMerge::computeScore(alg.list(), 0.5, false);

    EXPECT_NEAR(expected, found, 1e-8);
}

TEST(PolylineSplitMerge, findInitialTriangle) {
    std::vector<cv::Point2i> contour;
    for (int32_t i = 0; i < 10; i++) contour.push_back({i, i});
    for (int32_t i = 0; i < 10; i++) contour.push_back({9 - i, 9});
    for (int32_t i = 0; i < 8; i++) contour.push_back({0, 8 - i});

    PolylineSplitMerge alg;

    EXPECT_TRUE(alg.findInitialTriangle(contour));

    EXPECT_EQ(3u, alg.list().size());

    // the order was specially selected knowing what the current algorithm is
    // te indexes are what it should be no matter what
    auto e = alg.list().getHead();
    EXPECT_EQ(9, (*e)->index);
    ++e;
    EXPECT_EQ(19, (*e)->index);
    ++e;
    EXPECT_EQ(0, (*e)->index);
}

TEST(PolylineSplitMerge, ensureTriangleOrder) {
    std::vector<cv::Point2i> contour = make_rect(10, 12, 20, 22);

    // no change needed
    PolylineSplitMerge alg;
    alg.addCorner(0);
    alg.addCorner(10);
    alg.addCorner(16);

    alg.ensureTriangleOrder(contour);
    {
        auto e = alg.list().getHead();
        ++e;
        EXPECT_EQ(10, (*e)->index);
    }

    // it's in the wrong order here
    PolylineSplitMerge alg2;
    alg2.addCorner(0);
    alg2.addCorner(16);
    alg2.addCorner(10);

    alg2.ensureTriangleOrder(contour);
    {
        auto e = alg2.list().getHead();
        ++e;
        EXPECT_EQ(10, (*e)->index);
    }
}

TEST(PolylineSplitMerge, increaseNumberOfSidesByOne_loops) {
    std::vector<cv::Point2i> contour = make_rect(10, 12, 20, 22);

    PolylineSplitMerge alg;
    // when the new side as an error of zero this will make it impossible to split
    alg.setMaxSideError(ConfigLength::fixed(1));
    alg.addCorner(0);
    alg.addCorner(10);
    alg.addCorner(20);
    alg.addCorner(30);

    // set up polyline variables
    auto e = alg.list().getHead();
    while (e != alg.list().end()) {
        (*e)->splitable = false;
        ++e;
    }
    e = alg.list().getTail();
    (*e)->sideError = alg.computeSideError(contour, (*e)->index, 5);
    (*e)->splitable = true;
    (*e)->splitLocation = 34;
    (*e)->splitError0 = 0;
    (*e)->splitError1 = 0;

    EXPECT_TRUE(alg.increaseNumberOfSidesByOne(contour, true));

    EXPECT_EQ(5u, alg.list().size());
    e = alg.list().getHead();
    EXPECT_EQ(0, (*e)->index);
    ++e;
    EXPECT_EQ(10, (*e)->index);
    ++e;
    EXPECT_EQ(20, (*e)->index);
    ++e;
    EXPECT_EQ(30, (*e)->index);
    ++e;
    EXPECT_FALSE((*e)->splitable);  // less than minimum side length
    EXPECT_EQ(34, (*e)->index);
}

TEST(PolylineSplitMerge, increaseNumberOfSidesByOne_sequence) {
    std::vector<cv::Point2i> contour = make_rect(10, 12, 20, 22);

    PolylineSplitMerge alg;
    alg.setLoops(false);

    // when the new side as an error of zero this will make it impossible to split
    alg.setMaxSideError(ConfigLength::fixed(1));
    alg.addCorner(0);
    alg.addCorner(10);
    alg.addCorner(20);
    alg.addCorner(30);

    auto e = alg.list().getHead();
    while (e != alg.list().end()) {
        (*e)->splitable = false;
        ++e;
    }
    // this should be ignored and not selected even though by score it would be
    e = alg.list().getTail();
    (*e)->sideError = alg.computeSideError(contour, (*e)->index, 5);
    (*e)->splitable = true;
    (*e)->splitLocation = 34;
    (*e)->splitError0 = 0;
    (*e)->splitError1 = 0;

    --e;
    (*e)->sideError = alg.computeSideError(contour, (*e)->index, 5);
    (*e)->splitable = true;
    (*e)->splitLocation = 24;
    (*e)->splitError0 = 1;
    (*e)->splitError1 = 1;

    EXPECT_TRUE(alg.increaseNumberOfSidesByOne(contour, false));

    EXPECT_EQ(5u, alg.list().size());
    e = alg.list().getHead();
    EXPECT_EQ(0, (*e)->index);
    ++e;
    EXPECT_EQ(10, (*e)->index);
    ++e;
    EXPECT_EQ(20, (*e)->index);
    ++e;
    EXPECT_FALSE((*e)->splitable);  // less than minimum side length
    EXPECT_EQ(24, (*e)->index);
    ++e;
    EXPECT_EQ(30, (*e)->index);
}

TEST(PolylineSplitMerge, isSideConvex) {
    std::vector<cv::Point2i> contour = make_rect(10, 12, 20, 22);

    // straight line on a perfect side
    PolylineSplitMerge alg;
    alg.addCorner(0);
    alg.addCorner(10);
    EXPECT_TRUE(alg.isSideConvex(contour, alg.list().getHead()));

    // it now has to go the long way around
    alg.list().reset();
    alg.addCorner(10);
    alg.addCorner(0);
    EXPECT_FALSE(alg.isSideConvex(contour, alg.list().getHead()));
}

TEST(PolylineSplitMerge, selectCornerToSplit_loop) {
    PolylineSplitMerge alg;
    auto c1 = alg.addCorner(10);
    auto c2 = alg.addCorner(20);
    auto c3 = alg.addCorner(30);
    auto c4 = alg.addCorner(40);

    // net reduction of 2
    (*c2)->splitable = true;
    (*c2)->sideError = 6;
    (*c2)->splitError0 = 1;
    (*c2)->splitError1 = 3;
    // net reduction of 1
    (*c3)->splitable = true;
    (*c3)->sideError = 6;
    (*c3)->splitError0 = 5;
    (*c3)->splitError1 = 0;
    // small split error but an increase
    (*c1)->splitable = true;
    (*c1)->sideError = 2;
    (*c1)->splitError0 = 1;
    (*c1)->splitError1 = 2;
    // c0 is no change
    // massive reduction but marked as not splittable
    (*c4)->splitable = false;
    (*c4)->sideError = 20;

    EXPECT_TRUE(alg.selectCornerToSplit(true) == c2);
}

TEST(PolylineSplitMerge, selectCornerToSplit_sequence) {
    PolylineSplitMerge alg;
    auto c0 = alg.addCorner(0);
    auto c1 = alg.addCorner(10);
    auto c2 = alg.addCorner(20);
    auto c3 = alg.addCorner(30);
    auto c4 = alg.addCorner(40);

    // net reduction of 2
    (*c2)->splitable = true;
    (*c2)->sideError = 6;
    (*c2)->splitError0 = 1;
    (*c2)->splitError1 = 3;
    // massive reduction but marked as not splittable
    (*c3)->splitable = false;
    (*c3)->sideError = 20;
    (*c3)->splitError0 = 5;
    (*c3)->splitError1 = 0;
    // small split error but an increase
    (*c1)->splitable = true;
    (*c1)->sideError = 2;
    (*c1)->splitError0 = 1;
    (*c1)->splitError1 = 2;
    // considered by not as good as 2
    (*c0)->splitable = true;
    (*c0)->sideError = 3.5;
    (*c0)->splitError0 = 2;
    (*c0)->splitError1 = 1;
    // c4 is good a split candidate that should be ignored because it's on the tail
    (*c4)->splitable = true;
    (*c4)->sideError = 20;
    (*c4)->splitError0 = 1;
    (*c4)->splitError1 = 1;

    EXPECT_TRUE(alg.selectCornerToSplit(false) == c2);
}

TEST(PolylineSplitMerge, removeAndSaveCorner_positive) {
    PolylineSplitMerge alg;
    alg.setCornerScorePenalty(0.5);
    alg.addCorner(0);
    alg.addCorner(10);
    alg.addCorner(14);
    auto useless = alg.list().getElement(2, true);
    (*useless)->sideError = 5;  // give it an error so that it will be removed
    // create a list of polylines up to 5 corners
    EXPECT_TRUE(alg.savePolyline());
    alg.addCorner(16);
    EXPECT_TRUE(alg.savePolyline());
    alg.addCorner(26);
    EXPECT_TRUE(alg.savePolyline());

    // sanity check
    const CandidatePolyline* c = &alg.getPolylines()[1];
    EXPECT_EQ(4u, c->splits.size());
    EXPECT_EQ(14, c->splits[2]);

    // remove the useless corner
    EXPECT_TRUE(alg.removeCornerAndSavePolyline(useless, 0));

    c = &alg.getPolylines()[1];
    EXPECT_EQ(4u, c->splits.size());
    EXPECT_EQ(16, c->splits[2]);
}

TEST(PolylineSplitMerge, removeAndSaveCorner_negative) {
    PolylineSplitMerge alg;
    alg.setCornerScorePenalty(0.5);
    alg.addCorner(0);
    alg.addCorner(10);
    alg.addCorner(16);
    EXPECT_TRUE(alg.savePolyline());
    alg.addCorner(26);
    EXPECT_TRUE(alg.savePolyline());

    // All the corners are needed and the score will get worse if removed
    auto selected = alg.list().getElement(1, true);
    EXPECT_FALSE(alg.removeCornerAndSavePolyline(selected, 5));

    const CandidatePolyline* c = &alg.getPolylines()[0];
    EXPECT_EQ(3u, c->splits.size());
    EXPECT_EQ(10, c->splits[1]);  // should still be there
}

TEST(PolylineSplitMerge, selectCornerToRemove) {
    std::vector<cv::Point2i> contour = make_rect(10, 12, 20, 18);

    PolylineSplitMerge alg;
    alg.setCornerScorePenalty(0.5);
    alg.addCorner(0);
    alg.addCorner(5);  // pointless corner
    alg.addCorner(10);
    alg.addCorner(16);
    alg.addCorner(26);

    auto expected = alg.list().getHead();
    ++expected;
    boofcv_qr::ErrorValue foundError;
    auto found = alg.selectCornerToRemove(contour, foundError, true);

    EXPECT_TRUE(expected == found);
    EXPECT_NEAR(0, foundError.value, 1e-8);
}

TEST(PolylineSplitMerge, selectCornerToRemove_null) {
    std::vector<cv::Point2i> contour = make_rect(10, 12, 20, 18);

    PolylineSplitMerge alg;
    alg.setCornerScorePenalty(0.5);
    alg.addCorner(0);
    alg.addCorner(10);
    alg.addCorner(16);

    // fails because it has 3 sides
    boofcv_qr::ErrorValue foundError;
    EXPECT_TRUE(alg.selectCornerToRemove(contour, foundError, true) == alg.list().end());

    // won't fail because it has more than 3 corners. There is no good choice to remove
    // but it will still pick one
    alg.addCorner(26);
    EXPECT_FALSE(alg.selectCornerToRemove(contour, foundError, true) == alg.list().end());
    EXPECT_GT(foundError.value, 1);
}

TEST(PolylineSplitMerge, findCornerSeed) {
    std::vector<cv::Point2i> contour;
    for (int32_t i = 0; i < 20; i++) contour.push_back({i, 0});
    contour.push_back({2, 1});
    contour.push_back({2, 2});

    EXPECT_EQ(19, PolylineSplitMerge::findCornerSeed(contour));
}

TEST(PolylineSplitMerge, maximumDistance) {
    std::vector<cv::Point2i> contour;
    for (int32_t i = 0; i < 20; i++) contour.push_back({i, 0});

    contour[8] = cv::Point2i(8, 20);

    EXPECT_EQ(8, PolylineSplitMerge::maximumDistance(contour, 19, 3));
}

TEST(PolylineSplitMerge, computePotentialSplitScore) {
    PolylineSplitMerge alg;
    alg.setMinimumSideLength(5);
    alg.setThresholdSideSplitScore(0);

    std::vector<cv::Point2i> contour;
    for (int32_t i = 0; i < 20; i++) contour.push_back({i, 0});

    // add some texture
    contour[3].y = 5;
    contour[15].y = 5;
    contour[10].y = 20;  // this will be selected as the corner since it's the farthest away

    alg.addCorner(0);
    alg.addCorner(19);
    auto e = alg.list().getHead();
    (*e)->sideError = 20;

    alg.computePotentialSplitScore(contour, e, false);

    EXPECT_TRUE((*e)->splitable);
    EXPECT_GT((*e)->splitError0, 0);
    EXPECT_GT((*e)->splitError1, 0);
    EXPECT_EQ(10, (*e)->splitLocation);
}

TEST(PolylineSplitMerge, computeSideError_exhaustive) {
    PolylineSplitMerge alg;
    alg.setMaxNumberOfSideSamples(300);  // have it exhaustively sample all pixels

    std::vector<cv::Point2i> contour;
    for (int32_t i = 0; i < 20; i++) contour.push_back({i, 0});
    EXPECT_NEAR(0, alg.computeSideError(contour, 0, 19), 1e-8);
    for (int32_t i = 1; i < 19; i++) contour[static_cast<std::size_t>(i)].y = 5;
    contour[10].y = 0;  // need this to be zero so that two lines are the same
    // average SSE
    double expected = (5.0 * 5.0 * 17) / 18.0;
    EXPECT_NEAR(expected, alg.computeSideError(contour, 0, 19), 1e-8);
    // the error should have this property to not bias it based on the number of sides
    expected = (5 * 5 * 9) / 9.0 + (5 * 5 * 8) / 8.0;
    double split = alg.computeSideError(contour, 0, 10) +
                   alg.computeSideError(contour, 10, 19);
    EXPECT_NEAR(expected, split, 1e-8);

    //----------- Test the wrapping around case
    std::vector<cv::Point2i> contour2;
    for (std::size_t i = 0; i < contour.size(); i++) {
        contour2.push_back(contour[(i + 10) % contour.size()]);
    }
    expected = (5 * 5 * 9) / 9.0;
    EXPECT_NEAR(expected, alg.computeSideError(contour2, 10, 0), 1e-8);
}

TEST(PolylineSplitMerge, computeSideError_skip) {
    PolylineSplitMerge alg;
    alg.setMaxNumberOfSideSamples(5);  // it will sub sample

    std::vector<cv::Point2i> contour;
    for (int32_t i = 0; i < 20; i++) contour.push_back({i, 0});
    EXPECT_NEAR(0, alg.computeSideError(contour, 0, 19), 1e-8);
    for (int32_t i = 1; i < 19; i++) contour[static_cast<std::size_t>(i)].y = 5;
    contour[10].y = 0;

    // see if it is within the expected by some error margin
    double expected = (5.0 * 5.0 * 17) / 19.0;
    EXPECT_NEAR(expected, alg.computeSideError(contour, 0, 19), expected * 0.15);

    //----------- Now in the reverse direction
    std::vector<cv::Point2i> contour2;
    for (std::size_t i = 0; i < contour.size(); i++) {
        contour2.push_back(contour[(i + 10) % contour.size()]);
    }
    expected = (5 * 5 * 5) / 5.0;
    EXPECT_NEAR(expected, alg.computeSideError(contour2, 10, 0), 1e-8);
}

TEST(PolylineSplitMerge, addCorner) {
    PolylineSplitMerge alg;

    EXPECT_EQ(0u, alg.list().size());
    alg.addCorner(3);
    EXPECT_EQ(1u, alg.list().size());
    alg.addCorner(4);
    EXPECT_EQ(2u, alg.list().size());
    EXPECT_EQ(3, (*alg.list().getElement(0, true))->index);
    EXPECT_EQ(4, (*alg.list().getElement(1, true))->index);
}

TEST(PolylineSplitMerge, setSplitVariables) {
    std::vector<cv::Point2i> contour = make_rect(5, 6, 12, 20);

    PolylineSplitMerge alg;
    alg.setConvex(false);  // make sure this test doesn't get triggered
    alg.setMinimumSideLength(5);

    // corners at 0,7,21,28
    alg.addCorner(0);
    alg.addCorner(21);
    alg.addCorner(28);

    auto e = alg.list().getHead();
    // these values should be overwritten
    (*e)->splitLocation = -1;
    (*e)->splitError0 = -1;
    (*e)->splitError1 = -1;

    alg.setSplitVariables(contour, e, alg.list().getElement(1, true));

    EXPECT_EQ(7, (*e)->splitLocation);
    EXPECT_NEAR(0, (*e)->splitError0, 1e-4);
    EXPECT_NEAR(0, (*e)->splitError1, 1e-4);

    // turn on contour test. Should produce same results
    alg.setConvex(true);
    (*e)->splitLocation = -1;
    (*e)->splitError0 = -1;
    (*e)->splitError1 = -1;
    alg.setSplitVariables(contour, e, alg.list().getElement(1, true));
    EXPECT_TRUE((*e)->splitable);
    EXPECT_EQ(7, (*e)->splitLocation);
    EXPECT_NEAR(0, (*e)->splitError0, 1e-4);
    EXPECT_NEAR(0, (*e)->splitError1, 1e-4);
}

TEST(PolylineSplitMerge, setSplitVariables_withConvexCheck) {
    std::vector<cv::Point2i> contour = make_rect(5, 6, 12, 20);
    PolylineSplitMerge alg;
    alg.setConvex(true);

    // corners in reverse order to trigger convex failure
    std::reverse(contour.begin(), contour.end());
    alg.addCorner(28);
    alg.addCorner(21);
    alg.addCorner(0);

    auto e0 = alg.list().getHead();
    ++e0;
    auto e1 = e0;
    ++e1;

    (*e0)->splitable = true;

    alg.setSplitVariables(contour, e0, e1);

    EXPECT_FALSE((*e0)->splitable);
}

TEST(PolylineSplitMerge, canBeSplit) {
    PolylineSplitMerge alg;

    // only the contour's size matters
    std::vector<cv::Point2i> contour;
    for (int32_t i = 0; i < 50; i++) contour.push_back({0, 0});

    for (int32_t i = 0; i < 10; i++) {
        Corner* c = alg.corners().grow();
        c->reset();
        c->index = i * 5;
        c->sideError = 0.1;
        alg.list().pushTail(c);
    }

    alg.setMinimumSideLength(2);
    alg.setThresholdSideSplitScore(0);  // turn off this test

    EXPECT_TRUE(alg.canBeSplit(contour, alg.list().getElement(5, true), false));
    EXPECT_TRUE(alg.canBeSplit(contour, alg.list().getElement(9, true), false));

    alg.setMinimumSideLength(5);
    EXPECT_FALSE(alg.canBeSplit(contour, alg.list().getElement(5, true), false));
    EXPECT_FALSE(alg.canBeSplit(contour, alg.list().getElement(9, true), false));

    // test side split score
    alg.setMinimumSideLength(2);
    alg.setThresholdSideSplitScore(1);

    (*alg.list().getElement(5, true))->sideError = 1.0000001;
    EXPECT_TRUE(alg.canBeSplit(contour, alg.list().getElement(5, true), false));
    (*alg.list().getElement(5, true))->sideError = 0.9999999;
    EXPECT_FALSE(alg.canBeSplit(contour, alg.list().getElement(5, true), false));

    // test the must split flag
    (*alg.list().getElement(5, true))->sideError = 0.9999999;
    EXPECT_TRUE(alg.canBeSplit(contour, alg.list().getElement(5, true), true));
    alg.setMinimumSideLength(6);
    EXPECT_FALSE(alg.canBeSplit(contour, alg.list().getElement(5, true), true));

    // Will it try to split a side with zero error if the min score is zero?
    alg.setMinimumSideLength(2);
    alg.setThresholdSideSplitScore(0);
    alg.corners().get(0)->sideError = 0;
    EXPECT_FALSE(alg.canBeSplit(contour, alg.list().getHead(), false));

    // now the minimum length is 1 and the side isn't perfect. Still shouldn't split
    // because the length is now 1
    alg.setMinimumSideLength(1);
    alg.corners().get(0)->sideError = 0.1;
    alg.corners().get(0)->index = 0;
    alg.corners().get(1)->index = 1;
    EXPECT_FALSE(alg.canBeSplit(contour, alg.list().getHead(), false));
}

TEST(PolylineSplitMerge, canBeSplit_special_case) {
    // only the contour's size matters
    std::vector<cv::Point2i> contour;
    for (int32_t i = 0; i < 12; i++) contour.push_back({0, 0});

    PolylineSplitMerge alg;
    alg.setMinimumSideLength(5);
    alg.setThresholdSideSplitScore(0);

    alg.addCorner(0);
    alg.addCorner(11);
    (*alg.list().getHead())->sideError = 1e-6;
    (*alg.list().getTail())->sideError = 1e-6;

    EXPECT_TRUE(alg.canBeSplit(contour, alg.list().getHead(), false));
    EXPECT_FALSE(alg.canBeSplit(contour, alg.list().getTail(), false));
}

TEST(PolylineSplitMerge, next) {
    PolylineSplitMerge alg;

    Corner* a = alg.corners().grow();
    a->reset();
    Corner* b = alg.corners().grow();
    b->reset();
    Corner* c = alg.corners().grow();
    c->reset();

    auto ia = alg.list().pushTail(a);
    auto ib = alg.list().pushTail(b);
    auto ic = alg.list().pushTail(c);
    (void)ia;

    EXPECT_EQ(c, *alg.next(alg.list().find(b)));
    EXPECT_EQ(a, *alg.next(alg.list().find(c)));
    (void)ib;
    (void)ic;
}

TEST(PolylineSplitMerge, previous) {
    PolylineSplitMerge alg;

    Corner* a = alg.corners().grow();
    a->reset();
    Corner* b = alg.corners().grow();
    b->reset();
    Corner* c = alg.corners().grow();
    c->reset();

    alg.list().pushTail(a);
    alg.list().pushTail(b);
    alg.list().pushTail(c);

    EXPECT_EQ(a, *alg.previous(alg.list().find(b)));
    EXPECT_EQ(c, *alg.previous(alg.list().find(a)));
}

TEST(PolylineSplitMerge, isConvexUsingMaxDistantPoints_positive) {
    std::vector<cv::Point2i> contour = make_rect(5, 6, 12, 20);

    for (std::size_t i = 0; i < contour.size(); i++) {
        std::size_t farthest = 0;
        double distance = -1;
        for (std::size_t j = 0; j < contour.size(); j++) {
            double dx = contour[i].x - contour[j].x;
            double dy = contour[i].y - contour[j].y;
            double d = std::sqrt(dx * dx + dy * dy);
            if (d > distance) {
                distance = d;
                farthest = j;
            }
        }
        EXPECT_TRUE(PolylineSplitMerge::isConvexUsingMaxDistantPoints(
            contour, static_cast<int32_t>(i), static_cast<int32_t>(farthest)));
    }
}

TEST(PolylineSplitMerge, isConvexUsingMaxDistantPoints_negative) {
    std::vector<cv::Point2i> contour = make_rect(5, 6, 12, 20);
    EXPECT_FALSE(PolylineSplitMerge::isConvexUsingMaxDistantPoints(contour, 2, 3));
}

TEST(PolylineSplitMerge, distanceSq) {
    cv::Point2i a(2, 4);
    cv::Point2i b(10, -3);

    int32_t expected = (2 - 10) * (2 - 10) + (4 - (-3)) * (4 - (-3));
    double found = PolylineSplitMerge::distanceSq(a, b);

    EXPECT_NEAR(static_cast<double>(expected), found, 1e-8);
}

TEST(PolylineSplitMerge, distanceAbs) {
    cv::Point2i a(2, 4);
    cv::Point2i b(10, -3);

    int32_t expected = std::abs(2 - 10) + std::abs(4 + 3);
    double found = PolylineSplitMerge::distanceAbs(a, b);

    EXPECT_NEAR(static_cast<double>(expected), found, 1e-8);
}

TEST(PolylineSplitMerge, assignLine_parametric) {
    std::vector<cv::Point2i> contour;
    for (int32_t i = 0; i < 20; i++) contour.push_back({i, 2});

    // make these points offset from all the others
    contour[1] = cv::Point2i(1, 5);
    contour[9] = cv::Point2i(9, 5);

    PolylineSplitMerge::LineParametric2D line;

    PolylineSplitMerge::assignLine(contour, 1, 9, line);

    auto distSq = [&](double x, double y) {
        // Inline the same parametric distance formula used internally.
        double scale = std::max(std::fabs(line.sx), std::fabs(line.sy));
        double sx = line.sx / scale, sy = line.sy / scale;
        double t = (sx * (x - line.px) + sy * (y - line.py)) /
                   (sx * sx + sy * sy);
        double a = (line.sx / scale) * t + line.px;
        double b = (line.sy / scale) * t + line.py;
        double dx = x - a, dy = y - b;
        return dx * dx + dy * dy;
    };

    EXPECT_NEAR(0, distSq(0, 5), 1e-8);
    EXPECT_NEAR(0, distSq(1, 5), 1e-8);
    EXPECT_NEAR(0, distSq(9, 5), 1e-8);
}

TEST(PolylineSplitMerge, assignLine_segment) {
    std::vector<cv::Point2i> contour;
    for (int32_t i = 0; i < 20; i++) contour.push_back({i, 2});

    contour[1] = cv::Point2i(1, 5);
    contour[9] = cv::Point2i(9, 5);

    PolylineSplitMerge::LineSegment2D line;

    PolylineSplitMerge::assignLine(contour, 1, 9, line);

    auto segDistSq = [&](double x, double y) {
        double abx = line.bx - line.ax;
        double aby = line.by - line.ay;
        double t = (abx * (x - line.ax) + aby * (y - line.ay)) /
                   (abx * abx + aby * aby);
        if (t < 0.0) {
            double dx = line.ax - x, dy = line.ay - y;
            return dx * dx + dy * dy;
        }
        if (t > 1.0) {
            double dx = line.bx - x, dy = line.by - y;
            return dx * dx + dy * dy;
        }
        double cx = line.ax + t * abx, cy = line.ay + t * aby;
        double dx = cx - x, dy = cy - y;
        return dx * dx + dy * dy;
    };

    EXPECT_NEAR(1, segDistSq(0, 5), 1e-8);
    EXPECT_NEAR(0, segDistSq(2, 5), 1e-8);
    EXPECT_NEAR(0, segDistSq(8, 5), 1e-8);
}

// ---------------------------------------------------------------------------
// MaximumLineDistance — mirrors TestMaximumLineDistance.java
// ---------------------------------------------------------------------------

TEST(MaximumLineDistance, selectSplitPoint) {
    std::vector<cv::Point2i> contour = make_rect(10, 12, 20, 22);

    MaximumLineDistance alg;
    SplitSelector::Results results;
    alg.selectSplitPoint(contour, 0, 25, results);

    EXPECT_EQ(10, results.index);
    EXPECT_GT(results.score, 1);

    // see if it handles wrapping indexes
    alg.selectSplitPoint(contour, 31, 4, results);

    EXPECT_EQ(0, results.index);
    EXPECT_GT(results.score, 1);
}

TEST(MaximumLineDistance, compareScore) {
    MaximumLineDistance alg;

    EXPECT_EQ(1, alg.compareScore(5, 1));
    EXPECT_EQ(0, alg.compareScore(5, 5));
    EXPECT_EQ(-1, alg.compareScore(1, 5));
}
