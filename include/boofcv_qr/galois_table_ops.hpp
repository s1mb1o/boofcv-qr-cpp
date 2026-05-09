// Port of:
//   boofcv.alg.fiducial.qrcode.GaliosFieldTableOps_U8
//   boofcv.alg.fiducial.qrcode.GaliosFieldTableOps_U16
// Upstream: BoofCV v1.3.0.
//
// In Java these are two separate classes because Java generics can't
// parameterise on primitive types (no `Foo<byte>` or `Foo<short>`). C++
// has no such restriction, so we share the algorithm via a single template
// `GaliosFieldTableOpsT<WordT>` and expose `_U8` / `_U16` aliases for naming
// parity with the upstream. Loop structure, variable names, and inline
// comments are preserved line-for-line from each Java source.
//
// Algorithm description: src/galois/galois_table_ops.md.

#ifndef BOOFCV_QR_GALOIS_TABLE_OPS_HPP
#define BOOFCV_QR_GALOIS_TABLE_OPS_HPP

#include "boofcv_qr/galois.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace boofcv_qr {

// Precomputed look up table for performing operations on GF polynomials of
// the specified degree, with poly-on-array helpers parameterised by the
// element type (uint8_t for QR, uint16_t for codes that need a larger field).
template <typename WordT>
class GaliosFieldTableOpsT : public GaliosFieldTableOps {
public:
    GaliosFieldTableOpsT(int32_t numBits, int32_t primitive)
        : GaliosFieldTableOps(numBits, primitive) {}

    // Two-phase name lookup: bring the table-driven scalar ops in scope
    // so the template body can call them unqualified, matching Java idiom.
    using GaliosFieldTableOps::multiply;
    using GaliosFieldTableOps::divide;
    using GaliosFieldTableOps::power;
    using GaliosFieldTableOps::power_n;
    using GaliosFieldTableOps::inverse;

    // ---------------------------------------------------------------
    // Coefficients largest-first: 2*x^3 + 8*x^2 + 1 = [2, 8, 0, 1].
    // ---------------------------------------------------------------

    void polyScale(const std::vector<WordT>& input, int32_t scale,
                   std::vector<WordT>& output) const {
        output.resize(input.size());

        for (std::size_t i = 0; i < input.size(); i++) {
            output[i] = static_cast<WordT>(multiply(input[i], scale));
        }
    }

    void polyAdd(const std::vector<WordT>& polyA, const std::vector<WordT>& polyB,
                 std::vector<WordT>& output) const {
        output.resize(std::max(polyA.size(), polyB.size()));

        // compute offset that would align the smaller polynomial with the larger polynomial
        std::size_t offsetA = polyB.size() > polyA.size() ? polyB.size() - polyA.size() : 0;
        std::size_t offsetB = polyA.size() > polyB.size() ? polyA.size() - polyB.size() : 0;
        std::size_t N = output.size();

        for (std::size_t i = 0; i < offsetB; i++) {
            output[i] = polyA[i];
        }
        for (std::size_t i = 0; i < offsetA; i++) {
            output[i] = polyB[i];
        }
        for (std::size_t i = std::max(offsetA, offsetB); i < N; i++) {
            output[i] = static_cast<WordT>(polyA[i - offsetA] ^ polyB[i - offsetB]);
        }
    }

    // Coefficients smallest-first: 2*x^3 + 8*x^2 + 1 = [1, 0, 2, 8].
    void polyAdd_S(const std::vector<WordT>& polyA, const std::vector<WordT>& polyB,
                   std::vector<WordT>& output) const {
        output.resize(std::max(polyA.size(), polyB.size()));
        std::size_t M = std::min(polyA.size(), polyB.size());

        for (std::size_t i = M; i < polyA.size(); i++) {
            output[i] = polyA[i];
        }
        for (std::size_t i = M; i < polyB.size(); i++) {
            output[i] = polyB[i];
        }

        for (std::size_t i = 0; i < M; i++) {
            output[i] = static_cast<WordT>(polyA[i] ^ polyB[i]);
        }
    }

    void polyAddScaleB(const std::vector<WordT>& polyA, const std::vector<WordT>& polyB,
                       int32_t scaleB, std::vector<WordT>& output) const {
        output.resize(std::max(polyA.size(), polyB.size()));

        std::size_t offsetA = polyB.size() > polyA.size() ? polyB.size() - polyA.size() : 0;
        std::size_t offsetB = polyA.size() > polyB.size() ? polyA.size() - polyB.size() : 0;
        std::size_t N = output.size();

        for (std::size_t i = 0; i < offsetB; i++) {
            output[i] = polyA[i];
        }
        for (std::size_t i = 0; i < offsetA; i++) {
            output[i] = static_cast<WordT>(multiply(polyB[i], scaleB));
        }
        for (std::size_t i = std::max(offsetA, offsetB); i < N; i++) {
            output[i] = static_cast<WordT>(
                polyA[i - offsetA] ^ multiply(polyB[i - offsetB], scaleB));
        }
    }

    void polyMult(const std::vector<WordT>& polyA, const std::vector<WordT>& polyB,
                  std::vector<WordT>& output) const {
        // Lots of room for efficiency improvements in this function
        output.assign(polyA.size() + polyB.size() - 1, 0);

        for (std::size_t j = 0; j < polyB.size(); j++) {
            int32_t vb = polyB[j];
            for (std::size_t i = 0; i < polyA.size(); i++) {
                int32_t va = polyA[i];
                output[i + j] = static_cast<WordT>(output[i + j] ^ multiply(va, vb));
            }
        }
    }

    void polyMult_flipA(const std::vector<WordT>& polyA, const std::vector<WordT>& polyB,
                        std::vector<WordT>& output) const {
        output.assign(polyA.size() + polyB.size() - 1, 0);

        for (std::size_t j = 0; j < polyB.size(); j++) {
            int32_t vb = polyB[j];
            for (std::size_t i = 0; i < polyA.size(); i++) {
                int32_t va = polyA[polyA.size() - i - 1];
                output[i + j] = static_cast<WordT>(output[i + j] ^ multiply(va, vb));
            }
        }
    }

    // Identical to polyMult; coefficients smallest-first.
    void polyMult_S(const std::vector<WordT>& polyA, const std::vector<WordT>& polyB,
                    std::vector<WordT>& output) const {
        output.assign(polyA.size() + polyB.size() - 1, 0);

        for (std::ptrdiff_t j = static_cast<std::ptrdiff_t>(polyB.size()) - 1; j >= 0; j--) {
            int32_t vb = polyB[static_cast<std::size_t>(j)];
            for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(polyA.size()) - 1; i >= 0; i--) {
                int32_t va = polyA[static_cast<std::size_t>(i)];
                output[static_cast<std::size_t>(i + j)] = static_cast<WordT>(
                    output[static_cast<std::size_t>(i + j)] ^ multiply(va, vb));
            }
        }
    }

    // Horner's-method evaluation, coefficients largest-first.
    int32_t polyEval(const std::vector<WordT>& input, int32_t x) const {
        int32_t y = input[0];

        for (std::size_t i = 1; i < input.size(); i++) {
            y = multiply(y, x) ^ input[i];
        }

        return y;
    }

    // Horner's-method evaluation, coefficients smallest-first.
    int32_t polyEval_S(const std::vector<WordT>& input, int32_t x) const {
        int32_t y = input[input.size() - 1];

        for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(input.size()) - 2; i >= 0; i--) {
            y = multiply(y, x) ^ input[static_cast<std::size_t>(i)];
        }

        return y;
    }

    // Continue evaluation across multiple polynomial parts.
    int32_t polyEvalContinue(int32_t previousOutput,
                             const std::vector<WordT>& part, int32_t x) const {
        int32_t y = previousOutput;
        for (std::size_t i = 0; i < part.size(); i++) {
            y = multiply(y, x) ^ part[i];
        }

        return y;
    }

    // Synthetic division, coefficients largest-first.
    void polyDivide(const std::vector<WordT>& dividend, const std::vector<WordT>& divisor,
                    std::vector<WordT>& quotient, std::vector<WordT>& remainder) const {
        // handle special case
        if (divisor.size() > dividend.size()) {
            remainder = dividend;
            quotient.clear();
            return;
        } else {
            remainder.assign(divisor.size() - 1, 0);
            quotient = dividend;
        }

        int32_t normalizer = divisor[0];

        std::size_t N = dividend.size() - divisor.size() + 1;
        for (std::size_t i = 0; i < N; i++) {
            quotient[i] = static_cast<WordT>(divide(quotient[i], normalizer));

            int32_t coef = quotient[i];
            if (coef != 0) {  // division by zero is undefined.
                for (std::size_t j = 1; j < divisor.size(); j++) {
                    int32_t div_j = divisor[j];

                    if (div_j != 0) {  // log(0) is undefined.
                        quotient[i + j] = static_cast<WordT>(
                            quotient[i + j] ^ multiply(div_j, coef));
                    }
                }
            }
        }

        // quotient currently contains the quotient and remainder. Copy
        // remainder into its own polynomial.
        std::size_t copyFrom = quotient.size() - remainder.size();
        for (std::size_t i = 0; i < remainder.size(); i++) {
            remainder[i] = quotient[copyFrom + i];
        }
        quotient.resize(quotient.size() - remainder.size());
    }

    // Synthetic division, coefficients smallest-first.
    void polyDivide_S(const std::vector<WordT>& dividend, const std::vector<WordT>& divisor,
                      std::vector<WordT>& quotient, std::vector<WordT>& remainder) const {
        if (divisor.size() > dividend.size()) {
            remainder = dividend;
            quotient.clear();
            return;
        } else {
            quotient.assign(dividend.size() - divisor.size() + 1, 0);
            remainder = dividend;
        }

        int32_t normalizer = divisor[divisor.size() - 1];

        std::size_t N = dividend.size() - divisor.size() + 1;
        for (std::size_t i = 0; i < N; i++) {
            std::size_t q_i = remainder.size() - i - 1;
            remainder[q_i] = static_cast<WordT>(divide(remainder[q_i], normalizer));

            int32_t coef = remainder[q_i];
            if (coef != 0) {
                for (std::size_t j = 1; j < divisor.size(); j++) {
                    std::size_t d_j = divisor.size() - j - 1;
                    int32_t div_j = divisor[d_j];
                    if (div_j != 0) {
                        remainder[remainder.size() - i - j - 1] = static_cast<WordT>(
                            remainder[remainder.size() - i - j - 1] ^
                            multiply(div_j, coef));
                    }
                }
            }
        }

        remainder.resize(remainder.size() - quotient.size());
        for (std::size_t i = 0; i < quotient.size(); i++) {
            quotient[i] = remainder[remainder.size() + i];
        }
        // (remainder.size() was just shrunk; the storage past the new end
        // still holds the trailing quotient bytes, hence the lookup above.)
    }
};

extern template class GaliosFieldTableOpsT<std::uint8_t>;
extern template class GaliosFieldTableOpsT<std::uint16_t>;

using GaliosFieldTableOps_U8 = GaliosFieldTableOpsT<std::uint8_t>;
using GaliosFieldTableOps_U16 = GaliosFieldTableOpsT<std::uint16_t>;

}  // namespace boofcv_qr

#endif  // BOOFCV_QR_GALOIS_TABLE_OPS_HPP
