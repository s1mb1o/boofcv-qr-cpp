// Mirrors TestSquareGraph.java. Upstream: BoofCV v1.3.0.

#include "boofcv_qr/squares/square_edge.hpp"
#include "boofcv_qr/squares/square_graph.hpp"
#include "boofcv_qr/squares/square_node.hpp"

#include <gtest/gtest.h>

#include <opencv2/core.hpp>

#include <cmath>

using boofcv_qr::LineSegment2D;
using boofcv_qr::SquareEdge;
using boofcv_qr::SquareGraph;
using boofcv_qr::SquareNode;

namespace {

constexpr double TEST_F64 = 1e-8;

// Mirrors UtilPolygons2D_F64.flip — reverses corner order in place
// (other than the first), giving the "other" winding.
void flip(std::vector<cv::Point2d>& p) {
    for (std::size_t i = 1, j = p.size() - 1; i < j; i++, j--) {
        std::swap(p[i], p[j]);
    }
}

}  // namespace

TEST(SquareGraph, computeNodeInfo) {
    SquareNode a;
    a.square = {{-1, 1}, {2, 1}, {2, -1}, {-1, -1}};

    SquareGraph::computeNodeInfo(a);

    EXPECT_NEAR(3.0, a.sideLengths[0], TEST_F64);
    EXPECT_NEAR(2.0, a.sideLengths[1], TEST_F64);
    EXPECT_NEAR(3.0, a.sideLengths[2], TEST_F64);
    EXPECT_NEAR(2.0, a.sideLengths[3], TEST_F64);

    EXPECT_NEAR(3.0, a.largestSide, TEST_F64);
    EXPECT_NEAR(2.0, a.smallestSide, TEST_F64);

    double dx = a.center.x - 0.5, dy = a.center.y - 0.0;
    EXPECT_LT(std::sqrt(dx * dx + dy * dy), TEST_F64);
}

TEST(SquareGraph, findSideIntersect) {
    SquareNode a;
    a.square = {{-1, 1}, {1, 1}, {1, -1}, {-1, -1}};

    SquareGraph alg;
    LineSegment2D line{{0, 0}, {0, 0}};
    LineSegment2D storage;
    cv::Point2d intersection;

    line.b = {0, 2};
    EXPECT_EQ(0, alg.findSideIntersect(a, line, intersection, storage));
    line.b = {0, -2};
    EXPECT_EQ(2, alg.findSideIntersect(a, line, intersection, storage));
    line.b = {2, 0};
    EXPECT_EQ(1, alg.findSideIntersect(a, line, intersection, storage));
    line.b = {-2, 0};
    EXPECT_EQ(3, alg.findSideIntersect(a, line, intersection, storage));
}

namespace {

void almostParallelHelper(bool changeClock) {
    SquareNode a;
    a.square = {{-1, 1}, {1, 1}, {1, -1}, {-1, -1}};
    SquareNode b;
    b.square = {{1, 1}, {3, 1}, {3, -1}, {1, -1}};

    int adj = 1;
    if (changeClock) {
        flip(a.square);
        flip(b.square);
        adj = 3;
    }

    SquareGraph alg;
    for (int i = 0; i < 4; i++) {
        EXPECT_TRUE(alg.almostParallel(a, i, b, i));
        EXPECT_TRUE(alg.almostParallel(a, i, b, (i + 2) % 4));
        EXPECT_FALSE(alg.almostParallel(a, i, b, (i + 1) % 4));
    }

    double angle0 = 0.1;
    double angle1 = M_PI / 4.0;

    a.square[static_cast<std::size_t>(adj)] = {-1 + 2 * std::cos(angle0),
                                                1 + 2 * std::sin(angle0)};
    EXPECT_TRUE(alg.almostParallel(a, 0, b, 0));
    EXPECT_FALSE(alg.almostParallel(a, 1, b, 0));
    a.square[static_cast<std::size_t>(adj)] = {-1 + 2 * std::cos(angle1),
                                                1 + 2 * std::sin(angle1)};
    EXPECT_TRUE(alg.almostParallel(a, 0, b, 0));
    EXPECT_FALSE(alg.almostParallel(a, 1, b, 0));
}

}  // namespace

TEST(SquareGraph, almostParallel) {
    almostParallelHelper(false);
    almostParallelHelper(true);
}

namespace {

void acuteAngleHelper(bool changeClock) {
    SquareNode a;
    a.square = {{-1, 1}, {1, 1}, {1, -1}, {-1, -1}};
    SquareNode b;
    b.square = {{1, 1}, {3, 1}, {3, -1}, {1, -1}};
    if (changeClock) {
        flip(a.square);
        flip(b.square);
    }

    SquareGraph alg;
    EXPECT_NEAR(0.0, alg.acuteAngle(a, 0, b, 0), TEST_F64);
    EXPECT_NEAR(0.0, alg.acuteAngle(a, 0, b, 2), TEST_F64);
    EXPECT_NEAR(0.0, alg.acuteAngle(a, 2, b, 0), TEST_F64);
    EXPECT_NEAR(0.0, alg.acuteAngle(a, 2, b, 2), TEST_F64);

    EXPECT_NEAR(0.0, alg.acuteAngle(a, 1, b, 1), TEST_F64);
    EXPECT_NEAR(0.0, alg.acuteAngle(a, 1, b, 3), TEST_F64);
    EXPECT_NEAR(0.0, alg.acuteAngle(a, 3, b, 1), TEST_F64);
    EXPECT_NEAR(0.0, alg.acuteAngle(a, 3, b, 3), TEST_F64);

    EXPECT_NEAR(M_PI / 2.0, alg.acuteAngle(a, 0, b, 1), TEST_F64);
    EXPECT_NEAR(M_PI / 2.0, alg.acuteAngle(a, 0, b, 3), TEST_F64);
    EXPECT_NEAR(M_PI / 2.0, alg.acuteAngle(a, 2, b, 1), TEST_F64);
    EXPECT_NEAR(M_PI / 2.0, alg.acuteAngle(a, 2, b, 3), TEST_F64);

    EXPECT_NEAR(M_PI / 2.0, alg.acuteAngle(a, 1, b, 0), TEST_F64);
    EXPECT_NEAR(M_PI / 2.0, alg.acuteAngle(a, 3, b, 0), TEST_F64);
    EXPECT_NEAR(M_PI / 2.0, alg.acuteAngle(a, 1, b, 2), TEST_F64);
    EXPECT_NEAR(M_PI / 2.0, alg.acuteAngle(a, 3, b, 2), TEST_F64);
}

}  // namespace

TEST(SquareGraph, acuteAngle) {
    acuteAngleHelper(true);
    acuteAngleHelper(false);
}

TEST(SquareGraph, detachEdge) {
    SquareNode a, b;
    SquareGraph alg;
    alg.connect(&a, 1, &b, 2, 2.5);
    SquareEdge* e = a.edges[1];
    alg.detachEdge(e);
    EXPECT_EQ(1u, alg.unused.size());
    EXPECT_EQ(nullptr, a.edges[1]);
    EXPECT_EQ(nullptr, b.edges[2]);
}

TEST(SquareGraph, connect) {
    SquareNode a, b;
    SquareGraph alg;
    alg.connect(&a, 1, &b, 2, 2.5);
    EXPECT_EQ(a.edges[1], b.edges[2]);
    SquareEdge* e = a.edges[1];
    EXPECT_NEAR(2.5, e->distance, TEST_F64);
    EXPECT_EQ(1, e->sideA);
    EXPECT_EQ(2, e->sideB);
}

namespace {
void assertConnected(const SquareNode& a, int indexA, const SquareNode& b,
                     int indexB, double distance) {
    EXPECT_EQ(a.edges[static_cast<std::size_t>(indexA)],
              b.edges[static_cast<std::size_t>(indexB)]);
    EXPECT_NEAR(distance,
                a.edges[static_cast<std::size_t>(indexA)]->distance, 1e-8);
}
void assertNotConnected(const SquareNode& a, const SquareNode& b) {
    for (int i = 0; i < 4; i++) {
        if (a.edges[static_cast<std::size_t>(i)] != nullptr) {
            SquareEdge* e = a.edges[static_cast<std::size_t>(i)];
            EXPECT_NE(e->a, &b);
            EXPECT_NE(e->b, &b);
        }
        if (b.edges[static_cast<std::size_t>(i)] != nullptr) {
            SquareEdge* e = b.edges[static_cast<std::size_t>(i)];
            EXPECT_NE(e->a, &a);
            EXPECT_NE(e->b, &a);
        }
    }
}
}  // namespace

TEST(SquareGraph, checkConnect) {
    SquareNode a, b, c;
    SquareGraph alg;

    // no prior connection
    alg.checkConnect(&a, 2, &b, 0, 2);
    assertConnected(a, 2, b, 0, 2);

    // prior on A, proposed worse → no change
    alg.checkConnect(&a, 2, &c, 1, 3);
    assertNotConnected(a, c);

    // prior on A, proposed better → swap
    alg.checkConnect(&a, 2, &c, 1, 1);
    assertNotConnected(a, b);
    assertConnected(a, 2, c, 1, 1);

    // prior on B, proposed worse → no change
    alg.checkConnect(&b, 3, &c, 1, 3);
    assertNotConnected(b, c);

    // prior on B, proposed better → swap
    alg.checkConnect(&b, 3, &c, 1, 0.5);
    assertNotConnected(a, c);
    assertConnected(b, 3, c, 1, 0.5);
}
