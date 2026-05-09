// Mirrors boofcv-recognition/src/test/java/boofcv/alg/fiducial/qrcode/
// TestPackedBits8.java. Upstream: BoofCV v1.3.0.

#include "boofcv_qr/packed_bits.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <random>

using boofcv_qr::PackedBits8;

namespace {

class PackedBitsFixture : public ::testing::Test {
protected:
    PackedBitsFixture() : rand(234) {}

    static void isZeros(const PackedBits8& values) {
        int32_t N = values.arrayLength();
        for (int32_t i = 0; i < N; i++) {
            EXPECT_EQ(0, values.data[static_cast<std::size_t>(i)]);
        }
    }

    std::mt19937 rand;
};

TEST_F(PackedBitsFixture, set_get) {
    PackedBits8 values(60);

    values.set(2, 1);
    EXPECT_EQ(values.get(2), 1);
    values.set(2, 0);
    EXPECT_NE(values.get(2), 1);
    isZeros(values);

    values.set(33, 1);
    EXPECT_EQ(values.get(33), 1);
    values.set(33, 0);
    EXPECT_NE(values.get(33), 1);
    isZeros(values);
}

TEST_F(PackedBitsFixture, resize) {
    PackedBits8 values(60);
    EXPECT_EQ(60, values.size);
    EXPECT_EQ(60 / 8 + 1, static_cast<int32_t>(values.data.size()));
    values.resize(20);
    EXPECT_EQ(20, values.size);
    EXPECT_EQ(60 / 8 + 1, static_cast<int32_t>(values.data.size()));
    values.resize(100);
    EXPECT_EQ(100, values.size);
    EXPECT_EQ(100 / 8 + 1, static_cast<int32_t>(values.data.size()));
}

TEST_F(PackedBitsFixture, growArray) {
    PackedBits8 values(8);
    EXPECT_EQ(8, values.size);
    EXPECT_EQ(1u, values.data.size());

    values.growArray(2, false);
    EXPECT_LE(2u, values.data.size());
    EXPECT_EQ(10, values.size);

    values.growArray(7, false);
    EXPECT_LE(3u, values.data.size());
    EXPECT_EQ(17, values.size);

    // see if save value works
    values.set(10, 1);
    values.growArray(1, true);
    EXPECT_EQ(1, values.get(10));
    EXPECT_LE(3u, values.data.size());
    EXPECT_EQ(18, values.size);
    values.growArray(7, true);
    EXPECT_EQ(1, values.get(10));
    EXPECT_LE(4u, values.data.size());
    EXPECT_EQ(25, values.size);

    values = PackedBits8(8);
    values.set(2, 1);
    EXPECT_EQ(1, values.get(2));
    values.growArray(8, false);
    EXPECT_EQ(0, values.get(2));
}

TEST_F(PackedBitsFixture, append) {
    PackedBits8 values(0);

    values.append(0b1101, 4, true);
    EXPECT_EQ(4, values.size);
    EXPECT_EQ(1, values.get(0));
    EXPECT_EQ(0, values.get(1));
    EXPECT_EQ(1, values.get(2));
    EXPECT_EQ(1, values.get(3));

    values.append(0b1101, 4, false);
    EXPECT_EQ(8, values.size);
    EXPECT_EQ(1, values.get(4));
    EXPECT_EQ(1, values.get(5));
    EXPECT_EQ(0, values.get(6));
    EXPECT_EQ(1, values.get(7));
}

TEST_F(PackedBitsFixture, append_array) {
    PackedBits8 src(0);
    std::uniform_int_distribution<int32_t> dist(0, 255);
    for (int32_t i = 0; i < 2; i++) {
        src.append(dist(rand), 8, false);
    }
    src.append(0b101, 3, false);

    PackedBits8 dst(0);
    dst.append(src, src.size);

    EXPECT_EQ(src.size, dst.size);
    for (int32_t i = 0; i < src.size; i++) {
        EXPECT_EQ(src.get(i), dst.get(i)) << "i=" << i;
    }
}

TEST_F(PackedBitsFixture, read) {
    PackedBits8 values(0);

    values.append(0b1101, 4, true);
    values.append(0b00011, 5, true);

    EXPECT_EQ(0b1101, values.read(0, 4, false));
    EXPECT_EQ(0b00011, values.read(4, 5, false));

    EXPECT_EQ(0b1011, values.read(0, 4, true));
    EXPECT_EQ(0b11000, values.read(4, 5, true));
}

}  // namespace
