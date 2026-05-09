// Mirrors:
//   boofcv-recognition/src/test/java/boofcv/alg/fiducial/qrcode/TestGaliosFieldTableOps_U8.java
//   boofcv-recognition/src/test/java/boofcv/alg/fiducial/qrcode/TestGaliosFieldTableOps_U16.java
// Upstream: BoofCV v1.3.0.
//
// Java has two test classes because U8 and U16 are separate concrete classes.
// In our port both come from the same `GaliosFieldTableOpsT<WordT>` template,
// so we run the test bodies through a typed fixture (`TYPED_TEST_SUITE`) and
// instantiate for both `uint8_t` and `uint16_t`. Each TYPED_TEST corresponds
// 1:1 to the @Test methods on the Java side.

#include "boofcv_qr/galois_table_ops.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <vector>

namespace {

template <typename WordT>
struct GaliosTableTypeParam {
    using Word = WordT;
    static constexpr int32_t numBits =
        std::is_same_v<WordT, std::uint8_t> ? 8 : 8;  // both default to GF(2^8)
    static constexpr int32_t primitive = 0b1'0001'1101;
};

template <typename WordT>
class GaliosFieldTableOpsTyped : public ::testing::Test {
protected:
    GaliosFieldTableOpsTyped() : rand(234) {}

    int32_t nextInt(int32_t bound) {
        std::uniform_int_distribution<int32_t> dist(0, bound - 1);
        return dist(rand);
    }

    using Alg = boofcv_qr::GaliosFieldTableOpsT<WordT>;

    std::vector<WordT> createArbitraryPolynomial(int32_t mask) const {
        // Mirror createArbitraryPolynomial: 0x12*x^2 + 0x54*x + 0xFF
        std::vector<WordT> input(3);
        input[0] = static_cast<WordT>(0x12 & mask);
        input[1] = static_cast<WordT>(0x54 & mask);
        input[2] = static_cast<WordT>(0xFF & mask);
        return input;
    }

    static void assertEqualsG(const std::vector<WordT>& a,
                              const std::vector<WordT>& b) {
        std::size_t offsetA = 0, offsetB = 0;
        if (a.size() > b.size())
            offsetA = a.size() - b.size();
        else
            offsetB = b.size() - a.size();
        for (std::size_t i = 0; i < offsetA; i++)
            EXPECT_EQ(0, a[i]);
        for (std::size_t i = 0; i < offsetB; i++)
            EXPECT_EQ(0, b[i]);

        std::size_t N = std::min(a.size(), b.size());
        for (std::size_t i = 0; i < N; i++)
            EXPECT_EQ(a[i + offsetA], b[i + offsetB]);
    }

    static void assertEqualsG_S(const std::vector<WordT>& a,
                                const std::vector<WordT>& b) {
        std::size_t M = std::min(a.size(), b.size());
        for (std::size_t i = M; i < a.size(); i++)
            EXPECT_EQ(0, a[i]);
        for (std::size_t i = M; i < b.size(); i++)
            EXPECT_EQ(0, b[i]);
        for (std::size_t i = 0; i < M; i++)
            EXPECT_EQ(a[i], b[i]);
    }

    void randomPoly(std::vector<WordT>& out, int32_t length, int32_t maxValue) {
        out.clear();
        out.reserve(static_cast<std::size_t>(length));
        for (int32_t j = 0; j < length; j++) {
            out.push_back(static_cast<WordT>(nextInt(maxValue + 1)));
        }
    }

    std::mt19937 rand;
};

using WordTypes = ::testing::Types<std::uint8_t, std::uint16_t>;

template <typename T>
struct PrimitiveFor;

template <>
struct PrimitiveFor<std::uint8_t> {
    static constexpr int32_t bits = 8;
    static constexpr int32_t primitive = 0b1'0001'1101;
};

template <>
struct PrimitiveFor<std::uint16_t> {
    static constexpr int32_t bits = 8;  // exercise U16 storage on a GF(2^8) field
    static constexpr int32_t primitive = 0b1'0001'1101;
};

}  // namespace

TYPED_TEST_SUITE(GaliosFieldTableOpsTyped, WordTypes);

// ---- shared helpers visible inside the typed tests ----
namespace {
template <typename TestFixture>
constexpr int32_t primitive4 = 0b1'0011;
}

TYPED_TEST(GaliosFieldTableOpsTyped, polyScale) {
    using WordT = TypeParam;
    using Alg = boofcv_qr::GaliosFieldTableOpsT<WordT>;
    constexpr int32_t numBits = 8;
    constexpr int32_t primitive8 = PrimitiveFor<WordT>::primitive;

    Alg alg(numBits, primitive8);

    std::vector<WordT> input = this->createArbitraryPolynomial(alg.max_value);
    int32_t mask = alg.max_value;
    int32_t scale = 0x45 & mask;

    std::vector<WordT> output;
    auto inputCopy = input;
    alg.polyScale(inputCopy, scale, output);

    EXPECT_EQ(input.size(), output.size());
    for (std::size_t i = 0; i < input.size(); i++) {
        int32_t expected = alg.multiply(input[i], scale);
        EXPECT_EQ(expected, static_cast<int32_t>(output[i]));
    }
}

TYPED_TEST(GaliosFieldTableOpsTyped, polyAdd) {
    using WordT = TypeParam;
    using Alg = boofcv_qr::GaliosFieldTableOpsT<WordT>;
    Alg alg(8, PrimitiveFor<WordT>::primitive);

    std::vector<WordT> inputA = this->createArbitraryPolynomial(alg.max_value);

    // Create an arbitrary polynomial: 0xA0*x^3 + 0x45
    std::vector<WordT> inputB(4, 0);
    inputB[0] = 0xA0;
    inputB[3] = 0x45;

    std::vector<WordT> output0;
    alg.polyAdd(inputA, inputB, output0);

    std::vector<WordT> output1;
    alg.polyAdd(inputB, inputA, output1);

    EXPECT_EQ(4u, output0.size());
    this->assertEqualsG(output0, output1);

    // compare to hand computed solution
    EXPECT_EQ(0xA0, output0[0]);
    EXPECT_EQ(0x12, output0[1]);
    EXPECT_EQ(0x54, output0[2]);
    EXPECT_EQ(0xFF ^ 0x45, output0[3]);
}

TYPED_TEST(GaliosFieldTableOpsTyped, polyAdd_S) {
    using WordT = TypeParam;
    using Alg = boofcv_qr::GaliosFieldTableOpsT<WordT>;
    Alg alg(8, PrimitiveFor<WordT>::primitive);

    // Create an arbitrary polynomial: 0x12*x^2 + 0x54*x + 0xFF (smallest-first)
    std::vector<WordT> inputA(3, 0);
    inputA[2] = 0x12;
    inputA[1] = 0x54;
    inputA[0] = 0xFF;

    // Create an arbitrary polynomial: 0xA0*x^3 + 0x45
    std::vector<WordT> inputB(4, 0);
    inputB[3] = 0xA0;
    inputB[0] = 0x45;

    std::vector<WordT> output0;
    alg.polyAdd_S(inputA, inputB, output0);

    std::vector<WordT> output1;
    alg.polyAdd_S(inputB, inputA, output1);

    EXPECT_EQ(4u, output0.size());
    this->assertEqualsG_S(output0, output1);

    EXPECT_EQ(0xA0, output0[3]);
    EXPECT_EQ(0x12, output0[2]);
    EXPECT_EQ(0x54, output0[1]);
    EXPECT_EQ(0xFF ^ 0x45, output0[0]);
}

TYPED_TEST(GaliosFieldTableOpsTyped, polyAddScaleB) {
    using WordT = TypeParam;
    using Alg = boofcv_qr::GaliosFieldTableOpsT<WordT>;
    Alg alg(8, PrimitiveFor<WordT>::primitive);

    std::vector<WordT> inputA = this->createArbitraryPolynomial(alg.max_value);

    std::vector<WordT> inputB(4, 0);
    inputB[0] = 0xA0;
    inputB[3] = 0x45;

    int32_t scale = 0x62;
    std::vector<WordT> scaleB;
    alg.polyScale(inputB, scale, scaleB);
    std::vector<WordT> expected;
    alg.polyAdd(inputA, scaleB, expected);

    std::vector<WordT> found;
    alg.polyAddScaleB(inputA, inputB, scale, found);

    this->assertEqualsG(expected, found);
}

TYPED_TEST(GaliosFieldTableOpsTyped, polyMult) {
    using WordT = TypeParam;
    using Alg = boofcv_qr::GaliosFieldTableOpsT<WordT>;
    Alg alg(8, PrimitiveFor<WordT>::primitive);

    std::vector<WordT> inputA = this->createArbitraryPolynomial(alg.max_value);

    std::vector<WordT> inputB(2, 0);
    inputB[1] = 0x03;

    std::vector<WordT> output0;
    alg.polyMult(inputA, inputB, output0);

    std::vector<WordT> output1;
    alg.polyMult(inputB, inputA, output1);

    EXPECT_EQ(4u, output0.size());
    this->assertEqualsG(output0, output1);
}

TYPED_TEST(GaliosFieldTableOpsTyped, polyMult_flipA) {
    using WordT = TypeParam;
    using Alg = boofcv_qr::GaliosFieldTableOpsT<WordT>;
    Alg alg(8, PrimitiveFor<WordT>::primitive);

    std::vector<WordT> inputA(3, 0);
    inputA[2] = 0x12;
    inputA[1] = 0x54;
    inputA[0] = 0xFF;

    std::vector<WordT> inputB(2, 0);
    inputB[1] = 0x03;

    std::vector<WordT> output0;
    alg.polyMult_flipA(inputA, inputB, output0);

    EXPECT_EQ(4u, output0.size());
}

TYPED_TEST(GaliosFieldTableOpsTyped, polyMult_S) {
    using WordT = TypeParam;
    using Alg = boofcv_qr::GaliosFieldTableOpsT<WordT>;
    Alg alg(8, PrimitiveFor<WordT>::primitive);

    std::vector<WordT> inputA(3, 0);
    inputA[2] = 0x12;
    inputA[1] = 0x54;
    inputA[0] = 0xFF;

    std::vector<WordT> inputB(2, 0);
    inputB[0] = 0x03;

    std::vector<WordT> output0;
    alg.polyMult_S(inputA, inputB, output0);

    std::vector<WordT> output1;
    alg.polyMult_S(inputB, inputA, output1);

    EXPECT_EQ(4u, output0.size());
    this->assertEqualsG_S(output0, output1);
}

TYPED_TEST(GaliosFieldTableOpsTyped, polyEval) {
    using WordT = TypeParam;
    using Alg = boofcv_qr::GaliosFieldTableOpsT<WordT>;
    Alg alg(8, PrimitiveFor<WordT>::primitive);

    std::vector<WordT> inputA(3, 0);
    inputA[0] = 0x12;
    inputA[1] = 0x54;
    inputA[2] = 0xFF;

    int32_t input = 0x09;
    int32_t found = alg.polyEval(inputA, input);

    int32_t expected = 0xFF ^ alg.multiply(0x54, input);
    expected ^= alg.multiply(0x12, alg.multiply(input, input));

    EXPECT_EQ(expected, found);
}

TYPED_TEST(GaliosFieldTableOpsTyped, polyEval_random) {
    using WordT = TypeParam;
    using Alg = boofcv_qr::GaliosFieldTableOpsT<WordT>;
    Alg alg(8, PrimitiveFor<WordT>::primitive);

    std::vector<WordT> inputA;
    for (int32_t i = 0; i < 1000; i++) {
        this->randomPoly(inputA, 30, 255);
        int32_t value = this->nextInt(256);

        int32_t found = alg.polyEval(inputA, value);
        EXPECT_GE(found, 0);
        EXPECT_LT(found, 256);
    }
}

TYPED_TEST(GaliosFieldTableOpsTyped, polyEval_S) {
    using WordT = TypeParam;
    using Alg = boofcv_qr::GaliosFieldTableOpsT<WordT>;
    Alg alg(8, PrimitiveFor<WordT>::primitive);

    std::vector<WordT> inputA(3, 0);
    inputA[2] = 0x12;
    inputA[1] = 0x54;
    inputA[0] = 0xFF;

    int32_t input = 0x09;
    int32_t found = alg.polyEval_S(inputA, input);

    int32_t expected = 0xFF ^ alg.multiply(0x54, input);
    expected ^= alg.multiply(0x12, alg.multiply(input, input));

    EXPECT_EQ(expected, found);
}

TYPED_TEST(GaliosFieldTableOpsTyped, polyEvalContinue) {
    using WordT = TypeParam;
    using Alg = boofcv_qr::GaliosFieldTableOpsT<WordT>;
    Alg alg(8, PrimitiveFor<WordT>::primitive);

    std::vector<WordT> polyA;
    this->randomPoly(polyA, 30, 255);

    int32_t x = 0x09;
    int32_t expected = alg.polyEval(polyA, x);

    std::vector<WordT> polyB(10);
    for (std::size_t i = 0; i < 10; i++)
        polyB[i] = polyA[20 + i];
    polyA.resize(20);

    int32_t found = alg.polyEval(polyA, x);
    found = alg.polyEvalContinue(found, polyB, x);

    EXPECT_EQ(expected, found);
}

TYPED_TEST(GaliosFieldTableOpsTyped, polyDivide) {
    using WordT = TypeParam;
    using Alg = boofcv_qr::GaliosFieldTableOpsT<WordT>;
    Alg alg(8, PrimitiveFor<WordT>::primitive);

    // 0xBB*x^4 + 0x12*x^3 + 0x54*x^2 + 0*x + 0xFF
    std::vector<WordT> inputA(5, 0);
    inputA[0] = 0xBB;
    inputA[1] = 0x12;
    inputA[2] = 0x54;
    inputA[4] = 0xFF;

    std::vector<WordT> inputB(2, 0);
    inputB[0] = 0xF0;
    inputB[1] = 0x0A;

    std::vector<WordT> quotient, remainder;
    alg.polyDivide(inputA, inputB, quotient, remainder);
    EXPECT_EQ(4u, quotient.size());
    EXPECT_EQ(1u, remainder.size());

    // reconstruct: inputA == inputB * quotient + remainder
    {
        std::vector<WordT> tmp, found;
        alg.polyMult(inputB, quotient, tmp);
        alg.polyAdd(tmp, remainder, found);
        this->assertEqualsG(inputA, found);
    }

    // divisor larger than dividend
    alg.polyDivide(inputB, inputA, quotient, remainder);
    EXPECT_EQ(0u, quotient.size());
    EXPECT_EQ(2u, remainder.size());
    {
        std::vector<WordT> tmp, found;
        alg.polyMult(inputA, quotient, tmp);
        alg.polyAdd(tmp, remainder, found);
        this->assertEqualsG(inputB, found);
    }
}

TYPED_TEST(GaliosFieldTableOpsTyped, polyDivide_S) {
    using WordT = TypeParam;
    using Alg = boofcv_qr::GaliosFieldTableOpsT<WordT>;
    Alg alg(8, PrimitiveFor<WordT>::primitive);

    // 0xBB*x^4 + 0x12*x^3 + 0x54*x^2 + 0*x + 0xFF (smallest-first)
    std::vector<WordT> inputA(5, 0);
    inputA[4] = 0xBB;
    inputA[3] = 0x12;
    inputA[2] = 0x54;
    inputA[0] = 0xFF;

    std::vector<WordT> inputB(2, 0);
    inputB[1] = 0xF0;
    inputB[0] = 0x0A;

    std::vector<WordT> quotient, remainder;
    alg.polyDivide_S(inputA, inputB, quotient, remainder);
    EXPECT_EQ(4u, quotient.size());
    EXPECT_EQ(1u, remainder.size());
    {
        std::vector<WordT> tmp, found;
        alg.polyMult_S(inputB, quotient, tmp);
        alg.polyAdd_S(tmp, remainder, found);
        this->assertEqualsG_S(inputA, found);
    }

    alg.polyDivide_S(inputB, inputA, quotient, remainder);
    EXPECT_EQ(0u, quotient.size());
    EXPECT_EQ(2u, remainder.size());
    {
        std::vector<WordT> tmp, found;
        alg.polyMult_S(inputA, quotient, tmp);
        alg.polyAdd_S(tmp, remainder, found);
        this->assertEqualsG_S(inputB, found);
    }
}
