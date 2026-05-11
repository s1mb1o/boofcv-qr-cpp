// Mirrors:
//   boofcv-recognition/src/test/java/boofcv/alg/fiducial/qrcode/TestReedSolomonCodes_U8.java
//   boofcv-recognition/src/test/java/boofcv/alg/fiducial/qrcode/TestReedSolomonCodes_U16.java
// Upstream: BoofCV v1.3.0.
//
// Java has two test classes, one per element type. Our port is templated on
// element type, so we cover both via TYPED_TEST. The U8 cases below mirror
// the Java JUnit cases 1:1; for U16 we also exercise correct_random as a
// smoke test that the larger storage type compiles and runs correctly on
// the same GF(2^8) field.

#include "boofcv_qr/reed_solomon.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <vector>

namespace {

constexpr int32_t primitive8 = 0b1'0001'1101;
constexpr int32_t primitive4 = 0b1'0011;

template <typename WordT>
class ReedSolomonCodesTyped : public ::testing::Test {
protected:
    ReedSolomonCodesTyped() : rand(234) {}

    int32_t nextInt(int32_t bound) {
        std::uniform_int_distribution<int32_t> dist(0, bound - 1);
        return dist(rand);
    }

    std::vector<WordT> randomMessage(int32_t maxValue, int32_t N) {
        std::vector<WordT> out;
        out.reserve(static_cast<std::size_t>(N));
        for (int32_t i = 0; i < N; i++)
            out.push_back(static_cast<WordT>(nextInt(maxValue + 1)));
        return out;
    }

    // Mirrors TestReedSolomonCodes_U8.selectN: a Knuth-shuffle-style sampler
    // that picks `setSize` distinct values from [0, maxValue).
    std::vector<int32_t> selectN(int32_t setSize, int32_t maxValue) {
        std::vector<int32_t> a(static_cast<std::size_t>(maxValue));
        for (int32_t i = 0; i < maxValue; i++)
            a[static_cast<std::size_t>(i)] = i;
        for (int32_t i = 0; i < setSize; i++) {
            int32_t selected = nextInt(maxValue - i) + i;
            std::swap(a[static_cast<std::size_t>(selected)],
                      a[static_cast<std::size_t>(i)]);
        }
        a.resize(static_cast<std::size_t>(setSize));
        return a;
    }

    std::mt19937 rand;
};

using WordTypes = ::testing::Types<std::uint8_t, std::uint16_t>;

// Helper to build a vector from an int initializer list and cast each element
// to WordT. Mirrors TestReedSolomonCodes_U8.array(...) varargs.
template <typename WordT>
static std::vector<WordT> arr(std::initializer_list<int32_t> values) {
    std::vector<WordT> out;
    out.reserve(values.size());
    for (int32_t v : values)
        out.push_back(static_cast<WordT>(v));
    return out;
}

}  // namespace

TYPED_TEST_SUITE(ReedSolomonCodesTyped, WordTypes);

TYPED_TEST(ReedSolomonCodesTyped, computeECC) {
    using WordT = TypeParam;
    using RS = boofcv_qr::ReedSolomonCodesT<WordT>;

    std::vector<WordT> message = this->randomMessage(0xFF, 50);
    std::vector<WordT> ecc;

    for (int32_t base = 0; base < 2; base++) {
        RS alg(8, primitive8, base);
        alg.generator(6);
        alg.computeECC(message, ecc);

        EXPECT_EQ(6u, ecc.size());

        int32_t numNotZero = 0;
        for (std::size_t dataIdx = 0; dataIdx < ecc.size(); dataIdx++) {
            if (0 != ecc[dataIdx])
                numNotZero++;
        }
        EXPECT_GE(numNotZero, 5);
    }
}

// Compare against results from a Python reference run.
TYPED_TEST(ReedSolomonCodesTyped, computeECC_python) {
    using WordT = TypeParam;
    using RS = boofcv_qr::ReedSolomonCodesT<WordT>;

    std::vector<WordT> a = arr<WordT>({0x40, 0xd2, 0x75, 0x47, 0x76, 0x17, 0x32,
                                       0x06, 0x27, 0x26, 0x96, 0xc6, 0xc6,
                                       0x96, 0x70, 0xec});
    std::vector<WordT> b = arr<WordT>({0xbc, 0x2a, 0x90, 0x13, 0x6b,
                                       0xaf, 0xef, 0xfd, 0x4b, 0xe0});

    auto message = a;
    std::vector<WordT> ecc;

    RS alg(8, 0x11d, 0);
    alg.generator(10);
    alg.computeECC(message, ecc);

    ASSERT_EQ(10u, ecc.size());
    for (std::size_t i = 0; i < b.size(); i++)
        EXPECT_EQ(b[i], ecc[i]);
}

TYPED_TEST(ReedSolomonCodesTyped, computeSyndromes) {
    using WordT = TypeParam;
    using RS = boofcv_qr::ReedSolomonCodesT<WordT>;

    std::vector<WordT> message = this->randomMessage(0xFF, 50);
    std::vector<WordT> ecc;

    for (int32_t base = 0; base < 2; base++) {
        RS alg(8, primitive8, base);
        alg.generator(6);
        alg.computeECC(message, ecc);

        std::vector<WordT> syndromes(6, 0);
        alg.computeSyndromes(message, ecc, syndromes);

        // no error → all syndromes zero
        for (std::size_t i = 0; i < syndromes.size(); i++)
            EXPECT_EQ(0, syndromes[i]);

        // introduce an error
        message[6] = static_cast<WordT>(message[6] + 7);
        alg.computeSyndromes(message, ecc, syndromes);

        int32_t notZero = 0;
        for (std::size_t i = 0; i < syndromes.size(); i++) {
            if (syndromes[i] != 0)
                notZero++;
        }
        EXPECT_GT(notZero, 1);

        // restore for next iteration
        message[6] = static_cast<WordT>(message[6] - 7);
    }
}

TYPED_TEST(ReedSolomonCodesTyped, correct_noErrorsClearsPriorLocations) {
    using WordT = TypeParam;
    using RS = boofcv_qr::ReedSolomonCodesT<WordT>;

    auto original = this->randomMessage(0xFF, 32);

    for (int32_t base = 0; base < 2; base++) {
        RS alg(8, primitive8, base);
        alg.generator(10);

        auto message = original;
        std::vector<WordT> ecc;
        alg.computeECC(message, ecc);

        auto corrupted = message;
        corrupted[5] = static_cast<WordT>(corrupted[5] ^ 0x12);
        ASSERT_TRUE(alg.correct(corrupted, ecc));
        EXPECT_EQ(message, corrupted);
        EXPECT_GT(alg.getTotalErrors(), 0);

        auto clean = message;
        auto cleanEcc = ecc;
        ASSERT_TRUE(alg.correct(clean, cleanEcc));
        EXPECT_EQ(message, clean);
        EXPECT_EQ(ecc, cleanEcc);
        EXPECT_EQ(0, alg.getTotalErrors());
        ASSERT_EQ(1u, alg.errorLocatorPoly.size());
        EXPECT_EQ(1, alg.errorLocatorPoly[0]);
    }
}

TYPED_TEST(ReedSolomonCodesTyped, generatorFamily0) {
    using WordT = TypeParam;
    using RS = boofcv_qr::ReedSolomonCodesT<WordT>;

    RS alg(8, primitive8, 0);
    alg.generator(5);

    for (int32_t i = 0; i < 5; i++) {
        int32_t value = alg.math.power(2, i);
        int32_t found = alg.math.polyEval(alg.generator_, value);
        EXPECT_EQ(0, found);
    }

    EXPECT_NE(0, alg.math.polyEval(alg.generator_, 5));
}

TYPED_TEST(ReedSolomonCodesTyped, generatorBase1) {
    using WordT = TypeParam;
    using RS = boofcv_qr::ReedSolomonCodesT<WordT>;

    RS alg(8, primitive8, 1);
    alg.generator(5);

    for (int32_t i = 0; i < 5; i++) {
        int32_t value = alg.math.power(2, i + 1);
        int32_t found = alg.math.polyEval(alg.generator_, value);
        EXPECT_EQ(0, found);
    }

    EXPECT_NE(0, alg.math.polyEval(alg.generator_, 5));
}

// Compare to a known solution from ISO 18004 §7.2.3.
TYPED_TEST(ReedSolomonCodesTyped, generatorBase1_Known1) {
    using WordT = TypeParam;
    using RS = boofcv_qr::ReedSolomonCodesT<WordT>;

    RS alg(4, 19, 1);
    alg.generator(5);

    ASSERT_EQ(6u, alg.generator_.size());
    EXPECT_EQ(1, alg.generator_[0]);  // x^5
    EXPECT_EQ(11, alg.generator_[1]);  // x^4
    EXPECT_EQ(4, alg.generator_[2]);
    EXPECT_EQ(6, alg.generator_[3]);
    EXPECT_EQ(2, alg.generator_[4]);
    EXPECT_EQ(1, alg.generator_[5]);  // x^0
}

TYPED_TEST(ReedSolomonCodesTyped, generatorBase1_Known0) {
    using WordT = TypeParam;
    using RS = boofcv_qr::ReedSolomonCodesT<WordT>;

    RS alg(4, 19, 1);
    alg.generator(6);

    ASSERT_EQ(7u, alg.generator_.size());
    EXPECT_EQ(1, alg.generator_[0]);
    EXPECT_EQ(7, alg.generator_[1]);
    EXPECT_EQ(9, alg.generator_[2]);
    EXPECT_EQ(3, alg.generator_[3]);
    EXPECT_EQ(12, alg.generator_[4]);
    EXPECT_EQ(10, alg.generator_[5]);
    EXPECT_EQ(12, alg.generator_[6]);
}

TYPED_TEST(ReedSolomonCodesTyped, findErrorLocatorPolynomialBM) {
    using WordT = TypeParam;
    using RS = boofcv_qr::ReedSolomonCodesT<WordT>;

    auto message = arr<WordT>({0x40, 0xd2, 0x75, 0x47, 0x76, 0x17, 0x32,
                               0x06, 0x27, 0x26, 0x96, 0xc6, 0xc6, 0x96,
                               0x70, 0xec});
    std::vector<WordT> ecc;
    int32_t nsyn = 10;
    std::vector<WordT> syndromes(static_cast<std::size_t>(nsyn), 0);

    RS alg(8, primitive8, 0);
    alg.generator(nsyn);
    alg.computeECC(message, ecc);

    message[0] = 0;
    alg.computeSyndromes(message, ecc, syndromes);
    std::vector<WordT> errorLocator;
    alg.findErrorLocatorPolynomialBM(syndromes, errorLocator);
    ASSERT_EQ(2u, errorLocator.size());
    EXPECT_EQ(3, errorLocator[0]);
    EXPECT_EQ(1, errorLocator[1]);

    message[6] = 10;
    alg.computeSyndromes(message, ecc, syndromes);
    alg.findErrorLocatorPolynomialBM(syndromes, errorLocator);
    ASSERT_EQ(3u, errorLocator.size());
    EXPECT_EQ(238, errorLocator[0]);
    EXPECT_EQ(89, errorLocator[1]);
    EXPECT_EQ(1, errorLocator[2]);
}

TYPED_TEST(ReedSolomonCodesTyped, findErrorLocatorPolynomialBM_compareToDirect) {
    using WordT = TypeParam;
    using RS = boofcv_qr::ReedSolomonCodesT<WordT>;

    std::vector<WordT> found;
    std::vector<WordT> expected;

    for (int32_t generatorBase = 0; generatorBase < 2; generatorBase++) {
        RS alg(8, primitive8, generatorBase);
        for (int32_t trial = 0; trial < 30; trial++) {
            int32_t N = 50;
            auto message = this->randomMessage(0xFF, N);

            std::vector<WordT> ecc;
            int32_t nsyn = 10;
            std::vector<WordT> syndromes(static_cast<std::size_t>(nsyn), 0);

            alg.generator(nsyn);
            alg.computeECC(message, ecc);

            int32_t where = this->nextInt(N);
            message[static_cast<std::size_t>(where)] = static_cast<WordT>(
                message[static_cast<std::size_t>(where)] ^ 0x12);
            alg.computeSyndromes(message, ecc, syndromes);

            std::vector<int32_t> whereList;
            whereList.push_back(where);

            alg.findErrorLocatorPolynomialBM(syndromes, found);
            alg.findErrorLocatorPolynomial(N + static_cast<int32_t>(ecc.size()),
                                           whereList, expected);

            ASSERT_EQ(expected.size(), found.size());
            for (std::size_t j = 0; j < found.size(); j++)
                EXPECT_EQ(expected[j], found[j]);
        }
    }
}

namespace {

template <typename WordT>
void runFindErrors_BruteForce(boofcv_qr::ReedSolomonCodesT<WordT>& alg,
                              std::mt19937& rand,
                              const std::vector<WordT>& message,
                              int32_t numErrors,
                              bool expectedFail) {
    std::vector<WordT> ecc;
    int32_t nsyn = 10;
    std::vector<WordT> syndromes(static_cast<std::size_t>(nsyn), 0);
    std::vector<WordT> errorLocator;
    std::vector<int32_t> locations;

    alg.generator(nsyn);
    auto messageCopy = message;
    alg.computeECC(messageCopy, ecc);

    auto cmessage = messageCopy;

    int32_t N = static_cast<int32_t>(messageCopy.size() + ecc.size());

    // selectN inline
    std::vector<int32_t> a(static_cast<std::size_t>(N));
    for (int32_t i = 0; i < N; i++)
        a[static_cast<std::size_t>(i)] = i;
    auto draw = [&](int32_t bound) {
        std::uniform_int_distribution<int32_t> d(0, bound - 1);
        return d(rand);
    };
    for (int32_t i = 0; i < numErrors; i++) {
        int32_t selected = draw(N - i) + i;
        std::swap(a[static_cast<std::size_t>(selected)],
                  a[static_cast<std::size_t>(i)]);
    }

    for (int32_t i = 0; i < numErrors; i++) {
        int32_t w = a[static_cast<std::size_t>(i)];
        if (static_cast<std::size_t>(w) < messageCopy.size())
            cmessage[static_cast<std::size_t>(w)] = static_cast<WordT>(
                cmessage[static_cast<std::size_t>(w)] ^ 0x45);
        else
            ecc[static_cast<std::size_t>(w) - messageCopy.size()] =
                static_cast<WordT>(
                    ecc[static_cast<std::size_t>(w) - messageCopy.size()] ^
                    0x45);
    }

    alg.computeSyndromes(cmessage, ecc, syndromes);
    alg.findErrorLocatorPolynomialBM(syndromes, errorLocator);

    if (expectedFail) {
        EXPECT_FALSE(
            alg.findErrorLocations_BruteForce(errorLocator, N, locations));
    } else {
        ASSERT_TRUE(
            alg.findErrorLocations_BruteForce(errorLocator, N, locations));

        EXPECT_EQ(static_cast<std::size_t>(numErrors), locations.size());

        for (std::size_t i = 0; i < locations.size(); i++) {
            int32_t num = 0;
            for (int32_t j = 0; j < numErrors; j++) {
                if (a[static_cast<std::size_t>(j)] == locations[i])
                    num++;
            }
            EXPECT_EQ(1, num);
        }
    }
}

}  // namespace

TYPED_TEST(ReedSolomonCodesTyped, findErrors_BruteForce) {
    using WordT = TypeParam;
    using RS = boofcv_qr::ReedSolomonCodesT<WordT>;

    auto message = this->randomMessage(0xFF, 50);
    for (int32_t generatorBase = 0; generatorBase < 2; generatorBase++) {
        RS alg(8, primitive8, generatorBase);
        for (int32_t trial = 0; trial < 200; trial++) {
            runFindErrors_BruteForce<WordT>(alg, this->rand, message,
                                            this->nextInt(5), false);
        }
    }
}

TYPED_TEST(ReedSolomonCodesTyped, findErrors_BruteForce_TooMany) {
    using WordT = TypeParam;
    using RS = boofcv_qr::ReedSolomonCodesT<WordT>;

    auto message = this->randomMessage(0xFF, 50);
    for (int32_t generatorBase = 0; generatorBase < 2; generatorBase++) {
        RS alg(8, primitive8, generatorBase);
        runFindErrors_BruteForce<WordT>(alg, this->rand, message, 6, true);
        runFindErrors_BruteForce<WordT>(alg, this->rand, message, 8, true);
    }
}

TYPED_TEST(ReedSolomonCodesTyped, findErrorEvaluator) {
    using WordT = TypeParam;
    using RS = boofcv_qr::ReedSolomonCodesT<WordT>;
    RS alg(8, primitive8, 0);

    auto check = [&](std::vector<WordT> syndromes,
                     std::vector<WordT> errorLocator,
                     std::vector<WordT> expected) {
        std::vector<WordT> found;
        alg.findErrorEvaluator(syndromes, errorLocator, found);
        ASSERT_EQ(found.size(), expected.size());
        for (std::size_t j = 0; j < found.size(); j++)
            EXPECT_EQ(found[j], expected[j]);
    };

    // one error
    check(arr<WordT>({64, 192, 93, 231, 52, 92, 228, 49, 83, 245}),
          arr<WordT>({3, 1}),
          arr<WordT>({0, 64}));

    // two errors
    check(arr<WordT>({62, 101, 255, 19, 236, 196, 112, 227, 174, 215}),
          arr<WordT>({159, 118, 1}),
          arr<WordT>({0, 62, 142}));

    // three errors
    check(arr<WordT>({32, 188, 7, 92, 8, 39, 184, 32, 101, 213}),
          arr<WordT>({97, 138, 194, 1}),
          arr<WordT>({0, 32, 217, 182}));
}

TYPED_TEST(ReedSolomonCodesTyped, correctErrors_hand) {
    using WordT = TypeParam;
    using RS = boofcv_qr::ReedSolomonCodesT<WordT>;

    auto message = arr<WordT>({0x40, 0xd2, 0x75, 0x47, 0x76, 0x17, 0x32,
                               0x06, 0x27, 0x26, 0x96, 0xc6, 0xc6, 0x96,
                               0x70, 0xec});
    std::vector<WordT> ecc;
    std::vector<WordT> syndromes;
    std::vector<WordT> errorLocator;
    int32_t nsyn = 10;

    RS alg(8, primitive8, 0);
    alg.generator(nsyn);
    alg.computeECC(message, ecc);

    auto corrupted = message;
    corrupted[0] = 0;
    corrupted[4] = 8;
    corrupted[5] = 9;
    alg.computeSyndromes(corrupted, ecc, syndromes);
    alg.findErrorLocatorPolynomialBM(syndromes, errorLocator);

    std::vector<int32_t> errorLocations = {0, 4, 5};

    alg.correctErrors(corrupted,
                      static_cast<int32_t>(message.size() + ecc.size()),
                      syndromes, errorLocator, errorLocations);

    ASSERT_EQ(corrupted.size(), message.size());
    for (std::size_t j = 0; j < corrupted.size(); j++)
        EXPECT_EQ(corrupted[j], message[j]);
}

namespace {

template <typename WordT>
void runCorrectRandom(std::mt19937& rand, int32_t numBits, int32_t primitive,
                      int32_t generatorBase, int32_t iterations) {
    using RS = boofcv_qr::ReedSolomonCodesT<WordT>;
    RS alg(numBits, primitive, generatorBase);

    int32_t maxErrors = 4;
    int32_t nsyn = maxErrors * 2 + 2;
    int32_t messageSize = std::min(100, alg.math.num_values - nsyn - 2);

    std::vector<WordT> ecc;
    int32_t mask = alg.math.max_value;
    alg.generator(nsyn);

    auto draw = [&](int32_t bound) {
        std::uniform_int_distribution<int32_t> d(0, bound - 1);
        return d(rand);
    };

    for (int32_t trial = 0; trial < iterations; trial++) {
        std::vector<WordT> message;
        message.reserve(static_cast<std::size_t>(messageSize));
        for (int32_t i = 0; i < messageSize; i++)
            message.push_back(static_cast<WordT>(draw(mask + 1)));
        auto corrupted = message;

        alg.computeECC(message, ecc);

        int32_t numErrors = draw(maxErrors);

        for (int32_t j = 0; j < numErrors; j++) {
            int32_t selected = draw(static_cast<int32_t>(message.size()));
            corrupted[static_cast<std::size_t>(selected)] = static_cast<WordT>(
                corrupted[static_cast<std::size_t>(selected)] ^
                ((0x12 + j) & mask));
        }

        if (numErrors < maxErrors - 1 && draw(5) < 1) {
            int32_t selectedEcc = draw(static_cast<int32_t>(ecc.size()));
            ecc[static_cast<std::size_t>(selectedEcc)] = static_cast<WordT>(
                ecc[static_cast<std::size_t>(selectedEcc)] ^ (0x13 & mask));
        }

        alg.correct(corrupted, ecc);

        ASSERT_EQ(message.size(), corrupted.size());
        for (std::size_t j = 0; j < corrupted.size(); j++)
            ASSERT_EQ(message[j], corrupted[j])
                << "trial=" << trial << " idx=" << j;
    }
}

}  // namespace

TYPED_TEST(ReedSolomonCodesTyped, correct_random) {
    using WordT = TypeParam;

    // Java runs 20_000 iterations per config. We mirror that for U8 (the
    // QR-relevant type). For U16 we use 2,000 so the suite stays fast — the
    // algorithm is shared, U16 here is mostly verifying template
    // instantiation and storage handling.
    constexpr int32_t iters = std::is_same_v<WordT, std::uint8_t> ? 20'000 : 2'000;

    runCorrectRandom<WordT>(this->rand, 4, primitive4, 0, iters);
    runCorrectRandom<WordT>(this->rand, 8, primitive8, 0, iters);
    runCorrectRandom<WordT>(this->rand, 8, primitive8, 1, iters);
}
