// Mirrors TestSquareNode.java + TestSquareEdge.java. Upstream: BoofCV v1.3.0.

#include "boofcv_qr/squares/square_edge.hpp"
#include "boofcv_qr/squares/square_node.hpp"

#include <gtest/gtest.h>

using boofcv_qr::SquareEdge;
using boofcv_qr::SquareNode;

TEST(SquareNode, distanceSqCorner) {
    SquareNode a;
    a.square = {{-2, -2}, {2, -2}, {2, 2}, {-2, 2}};

    EXPECT_NEAR(0.0, a.distanceSqCorner({-2.0, -2.0}), 1e-8);
    EXPECT_NEAR(0.0, a.distanceSqCorner({2.0, 2.0}), 1e-8);
    EXPECT_NEAR(1.0, a.distanceSqCorner({-3.0, 2.0}), 1e-8);
    EXPECT_NEAR(4.0, a.distanceSqCorner({-4.0, 2.0}), 1e-8);
}

TEST(SquareNode, getNumberOfConnections) {
    SquareNode a;
    a.square = std::vector<cv::Point2d>(4);  // 4 corners (zero-initialized)

    EXPECT_EQ(0, a.getNumberOfConnections());
    SquareEdge e0, e1, e2, e3;
    a.edges[2] = &e2;
    EXPECT_EQ(1, a.getNumberOfConnections());
    a.edges[0] = &e0;
    EXPECT_EQ(2, a.getNumberOfConnections());
    a.edges[3] = &e3;
    EXPECT_EQ(3, a.getNumberOfConnections());
    a.edges[1] = &e1;
    EXPECT_EQ(4, a.getNumberOfConnections());
}

TEST(SquareEdge, destination) {
    SquareNode a, b;
    SquareEdge e(&a, &b, 1, 2);
    EXPECT_EQ(&b, e.destination(&a));
    EXPECT_EQ(&a, e.destination(&b));
    EXPECT_EQ(2, e.destinationSide(&a));
    EXPECT_EQ(1, e.destinationSide(&b));
    EXPECT_TRUE(e.isEndPoint(&a));
    EXPECT_TRUE(e.isEndPoint(&b));
    SquareNode c;
    EXPECT_FALSE(e.isEndPoint(&c));
}

TEST(SquareEdge, reset) {
    SquareNode a, b;
    SquareEdge e(&a, &b, 1, 2);
    e.distance = 99.0;
    e.reset();
    EXPECT_EQ(nullptr, e.a);
    EXPECT_EQ(nullptr, e.b);
    EXPECT_EQ(-1, e.sideA);
    EXPECT_EQ(-1, e.sideB);
    EXPECT_DOUBLE_EQ(-1.0, e.distance);
}
