// Mirrors:
//   boofcv-recognition/src/test/java/boofcv/alg/fiducial/qrcode/TestGaliosFieldOps.java
//   boofcv-recognition/src/test/java/boofcv/alg/fiducial/qrcode/TestGaliosFieldTableOps.java
// Upstream: BoofCV v1.3.0.
//
// Note on RNG. BoofCV's BoofStandardJUnit uses java.util.Random seeded with
// 234. We use std::mt19937 with the same seed value. Exact value sequences
// will NOT match Java because java.util.Random is a different LCG, but the
// tests below are property-based — the algebraic identity is what's being
// checked, not specific numeric outputs — so any RNG that samples the input
// space adequately exercises the same property.

#include "boofcv_qr/galois.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <random>

using boofcv_qr::GaliosFieldOps;
using boofcv_qr::GaliosFieldTableOps;

namespace {

class GaliosFieldFixture : public ::testing::Test {
protected:
    GaliosFieldFixture() : rand(234) {}

    // Mirrors java.util.Random.nextInt(bound): returns a value in [0, bound).
    int32_t nextInt(int32_t bound) {
        std::uniform_int_distribution<int32_t> dist(0, bound - 1);
        return dist(rand);
    }

    std::mt19937 rand;
};

// ---------------------------------------------------------------------------
// Mirrors TestGaliosFieldOps
// ---------------------------------------------------------------------------

TEST_F(GaliosFieldFixture, addSubtract) {
    for (int32_t i = 0; i < 100; i++) {
        int32_t a = 1 + nextInt(255);
        int32_t b = 1 + nextInt(255);

        int32_t c = GaliosFieldOps::add(a, b);
        int32_t d = GaliosFieldOps::subtract(c, b);

        EXPECT_EQ(a, d);
        EXPECT_NE(a, c);
        EXPECT_NE(b, c);

        // adding and subtracting are the same on GF(2)
        EXPECT_EQ(d, GaliosFieldOps::add(c, b));
    }
}

TEST_F(GaliosFieldFixture, multiply) {
    for (int32_t i = 0; i < 100; i++) {
        int32_t a = 1 + nextInt(255);
        int32_t b = 1 + nextInt(255);

        int32_t c = GaliosFieldOps::multiply(a, b);

        if (b != 1)
            EXPECT_NE(a, c);
        else
            EXPECT_EQ(a, c);

        if (a != 1)
            EXPECT_NE(b, c);
        else
            EXPECT_EQ(b, c);

        EXPECT_GE(c, a);
        EXPECT_GE(c, b);

        // commutative
        EXPECT_EQ(c, GaliosFieldOps::multiply(b, a));

        // can't test via addition since this multiplication operation is
        // actually not a field due to the lack of modulus operation
    }
}

TEST_F(GaliosFieldFixture, multiplyField) {
    for (int32_t i = 0; i < 100; i++) {
        int32_t a = 1 + nextInt(255);
        int32_t b = 1 + nextInt(255);

        int32_t expected = GaliosFieldOps::multiply(a, b);
        expected = GaliosFieldOps::modulus(expected, 0b100011101);
        int32_t result = GaliosFieldOps::multiply(a, b, 0b100011101, 256);
        EXPECT_EQ(expected, result);
    }
}

TEST_F(GaliosFieldFixture, divideModulus) {
    for (int32_t i = 0; i < 100; i++) {
        int32_t a = 1 + nextInt(255);
        int32_t b = 1 + nextInt(255);

        int32_t n = GaliosFieldOps::divide(a, b);
        int32_t r = GaliosFieldOps::modulus(a, b);

        if (n == 0) {
            EXPECT_LT(GaliosFieldOps::length(a), GaliosFieldOps::length(b));
            EXPECT_EQ(a, r);
        } else {
            EXPECT_GE(GaliosFieldOps::length(a), GaliosFieldOps::length(b));
            int32_t found = GaliosFieldOps::multiply(n, b) ^ r;
            EXPECT_EQ(a, found);
        }
    }
}

TEST(GaliosFieldOpsLength, lengthBasics) {
    EXPECT_EQ(0, GaliosFieldOps::length(0b00000));
    EXPECT_EQ(1, GaliosFieldOps::length(0b00001));
    EXPECT_EQ(3, GaliosFieldOps::length(0b00100));
    EXPECT_EQ(3, GaliosFieldOps::length(0b00111));
}

// ---------------------------------------------------------------------------
// Mirrors TestGaliosFieldTableOps
// ---------------------------------------------------------------------------

class GaliosFieldTableFixture : public GaliosFieldFixture {
protected:
    static constexpr int32_t primitive2 = 0b111;
    static constexpr int32_t primitive4 = 0b10011;
    static constexpr int32_t primitive8 = 0b100011101;

    // exhaustively expects pow(n, primitive, num_values) used by several
    // tests. Mirrors the private helper in TestGaliosFieldTableOps.java.
    static int32_t pow(int32_t n, int32_t primitive, int32_t num_values) {
        int32_t val = 1;
        for (int32_t i = 0; i < n; i++) {
            val <<= 1;
            if (val >= num_values) {
                val ^= primitive;
            }
        }
        return val;
    }

    // Mirrors checkTableSum from the JUnit test: each non-zero exp entry
    // covers [1, max_value] exactly once, and each log entry covers
    // [0, max_value-1] exactly once.
    static void checkTableSum(const GaliosFieldTableOps& alg) {
        int32_t expected = 0;
        for (int32_t i = 0; i < alg.max_value; i++) {
            expected += i;
        }

        // Starts counting at 1. hence - max_value
        int32_t sum = 0;
        for (int32_t i = 0; i < alg.max_value; i++) {
            sum += alg.exp[static_cast<std::size_t>(i)];
        }
        EXPECT_EQ(expected, sum - alg.max_value);

        // zero will be in the first two elements, hence up to num_values
        sum = 0;
        for (int32_t i = 0; i < alg.num_values; i++) {
            sum += alg.log[static_cast<std::size_t>(i)];
        }
        EXPECT_EQ(expected, sum);
    }

    static void multiplyExhaustive(int32_t numBits, int32_t primitive) {
        GaliosFieldTableOps alg(numBits, primitive);

        int32_t num_values = alg.num_values;

        EXPECT_EQ(0, alg.multiply(0, 5));
        EXPECT_EQ(0, alg.multiply(5, 0));

        for (int32_t i = 0; i < num_values; i++) {
            for (int32_t j = 0; j < num_values; j++) {
                int32_t expected = pow(i + j, primitive, num_values);

                int32_t valA = pow(i, primitive, num_values);
                int32_t valB = pow(j, primitive, num_values);

                int32_t found = alg.multiply(valA, valB);

                EXPECT_EQ(expected, found);
            }
        }
    }
};

TEST_F(GaliosFieldTableFixture, constructor) {
    GaliosFieldTableOps alg(2, primitive2);
    EXPECT_EQ(4, alg.num_values);
    EXPECT_EQ(3, alg.max_value);
    EXPECT_EQ(0b111, alg.primitive);
    checkTableSum(alg);

    alg = GaliosFieldTableOps(8, primitive8);
    EXPECT_EQ(256, alg.num_values);
    EXPECT_EQ(255, alg.max_value);
    EXPECT_EQ(0b100011101, alg.primitive);
    checkTableSum(alg);
}

TEST_F(GaliosFieldTableFixture, multiply) {
    multiplyExhaustive(2, primitive2);
    multiplyExhaustive(8, primitive8);
}

TEST_F(GaliosFieldTableFixture, divide) {
    GaliosFieldTableOps alg(8, primitive8);

    for (int32_t i = 0; i < 256; i++) {
        for (int32_t j = 1; j < 256; j++) {
            int32_t multAB = alg.multiply(i, j);
            int32_t found = alg.divide(multAB, j);

            EXPECT_EQ(i, found);
        }
    }
}

TEST_F(GaliosFieldTableFixture, power) {
    GaliosFieldTableOps alg(8, primitive8);

    for (int32_t i = 0; i < 100; i++) {
        int32_t a = nextInt(20);
        int32_t b = nextInt(20);

        int32_t expected = pow(a * b, primitive8, 256);

        int32_t valA = pow(a, primitive8, 256);
        int32_t found = alg.power(valA, b);

        EXPECT_EQ(expected, found);
    }
}

TEST_F(GaliosFieldTableFixture, power4) {
    GaliosFieldTableOps alg(4, primitive4);

    for (int32_t a = 0; a < 16; a++) {
        for (int32_t b = 0; b < 16; b++) {
            int32_t expected = pow(a * b, primitive4, 16);

            int32_t valA = pow(a, primitive4, 16);
            int32_t found = alg.power(valA, b);

            EXPECT_EQ(expected, found);
        }
    }
}

TEST_F(GaliosFieldTableFixture, inverse) {
    GaliosFieldTableOps alg(8, primitive8);

    for (int32_t i = 0; i < 100; i++) {
        int32_t a = nextInt(255) + 1;

        int32_t expected = alg.divide(1, a);
        int32_t found = alg.inverse(a);

        EXPECT_EQ(expected, found);
    }
}

}  // namespace
