// Mirrors TestQrCodePositionPatternGraphGenerator.java. Upstream: BoofCV v1.3.0.

#include "boofcv_qr/finder/qr_code_position_pattern_graph_generator.hpp"
#include "boofcv_qr/squares/square_node.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using boofcv_qr::QrCodePositionPatternGraphGenerator;
using boofcv_qr::SquareNode;

namespace {

// Mirror of TestQrCodePositionPatternDetector.squareNode(x0, y0, width).
SquareNode squareNode(int32_t x0, int32_t y0, int32_t width) {
    SquareNode node;
    node.square = {
        cv::Point2d(x0, y0),
        cv::Point2d(x0 + width, y0),
        cv::Point2d(x0 + width, y0 + width),
        cv::Point2d(x0, y0 + width),
    };
    node.largestSide = width;
    node.smallestSide = width;
    node.center.x = x0 + width / 2.0;
    node.center.y = y0 + width / 2.0;
    node.sideLengths = {static_cast<double>(width), static_cast<double>(width),
                        static_cast<double>(width), static_cast<double>(width)};
    return node;
}

// Rotate node1's corners + centre about a fixed centre by `angle` (radians).
void rotateAbout(SquareNode& n, double cx, double cy, double angle) {
    double s = std::sin(angle), c = std::cos(angle);
    auto rot = [&](cv::Point2d& p) {
        double dx = p.x - cx, dy = p.y - cy;
        p.x = cx + dx * c - dy * s;
        p.y = cy + dx * s + dy * c;
    };
    for (auto& corner : n.square) rot(corner);
    rot(n.center);
}

}  // namespace

TEST(QrCodePositionPatternGraphGenerator, considerConnect_positive) {
    QrCodePositionPatternGraphGenerator alg(2);

    SquareNode n0 = squareNode(40, 60, 70);
    SquareNode n1 = squareNode(140, 60, 70);

    alg.considerConnect(&n0, &n1);

    EXPECT_EQ(1, n0.getNumberOfConnections());
    EXPECT_EQ(1, n1.getNumberOfConnections());
}

TEST(QrCodePositionPatternGraphGenerator, considerConnect_negative_rotated) {
    QrCodePositionPatternGraphGenerator alg(40);

    SquareNode n0 = squareNode(40, 60, 70);
    SquareNode n1 = squareNode(140, 60, 70);

    // Rotate n1 by 45° about its centre. The Java test uses an Se2_F64
    // composition; geometrically this just rotates n1 in place.
    double cx = 140 + 70 / 2.0;
    double cy = 60 + 70 / 2.0;
    rotateAbout(n1, cx, cy, M_PI / 4);

    alg.considerConnect(&n0, &n1);

    EXPECT_EQ(0, n0.getNumberOfConnections());
    EXPECT_EQ(0, n1.getNumberOfConnections());
}

TEST(QrCodePositionPatternGraphGenerator, process_LShapedTriple) {
    // Three finder patterns in an L: (40, 60), (140, 60), (40, 150).
    // After process() the graph should have:
    //  - n0 (top-left) connected to BOTH n1 (top-right) and n2 (bottom-left).
    //  - n1 connected only to n0.
    //  - n2 connected only to n0.
    QrCodePositionPatternGraphGenerator alg(2);

    std::vector<boofcv_qr::PositionPatternNode> nodes;
    nodes.resize(3);

    auto fillPP = [](boofcv_qr::PositionPatternNode& pp, int32_t x0,
                      int32_t y0, int32_t width) {
        pp.square = {
            cv::Point2d(x0, y0),
            cv::Point2d(x0 + width, y0),
            cv::Point2d(x0 + width, y0 + width),
            cv::Point2d(x0, y0 + width),
        };
        pp.largestSide = width;
        pp.smallestSide = width;
        pp.center.x = x0 + width / 2.0;
        pp.center.y = y0 + width / 2.0;
        pp.sideLengths = {static_cast<double>(width), static_cast<double>(width),
                           static_cast<double>(width), static_cast<double>(width)};
    };
    fillPP(nodes[0], 40, 60, 70);
    fillPP(nodes[1], 140, 60, 70);
    fillPP(nodes[2], 40, 150, 70);

    alg.process(nodes);

    EXPECT_EQ(2, nodes[0].getNumberOfConnections());
    EXPECT_EQ(1, nodes[1].getNumberOfConnections());
    EXPECT_EQ(1, nodes[2].getNumberOfConnections());
}

TEST(QrCodePositionPatternGraphGenerator, maxVersionRoundtrip) {
    QrCodePositionPatternGraphGenerator alg;
    EXPECT_EQ(40, alg.getMaxVersionQR());
    alg.setMaxVersionQR(7);
    EXPECT_EQ(7, alg.getMaxVersionQR());
}
