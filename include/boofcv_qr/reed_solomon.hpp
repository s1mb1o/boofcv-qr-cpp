// Port of:
//   boofcv.alg.fiducial.qrcode.ReedSolomonCodes_U8
//   boofcv.alg.fiducial.qrcode.ReedSolomonCodes_U16
// Upstream: BoofCV v1.3.0.
//
// As with `GaliosFieldTableOpsT`, the two Java classes are line-for-line
// identical apart from element type. We share the algorithm via a single
// template `ReedSolomonCodesT<WordT>` and expose `_U8` / `_U16` aliases.
// Loop structure, variable names, and inline comments mirror the Java
// sources directly. Companion doc: src/reed_solomon/reed_solomon.md.

#ifndef BOOFCV_QR_REED_SOLOMON_HPP
#define BOOFCV_QR_REED_SOLOMON_HPP

#include "boofcv_qr/galois.hpp"
#include "boofcv_qr/galois_table_ops.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace boofcv_qr {

template <typename WordT>
class ReedSolomonCodesT {
public:
    using Math = GaliosFieldTableOpsT<WordT>;

    // generatorBase is 0 for QR, 1 for Aztec.
    ReedSolomonCodesT(int32_t numBits, int32_t primitive, int32_t generatorBase_)
        : math(numBits, primitive), generatorBase(generatorBase_) {
        if (generatorBase_ < 0 || generatorBase_ > 1)
            throw std::invalid_argument("generatorBase must be 0 or 1");
    }

    /**
     * Given the input message compute the error correction code for it.
     * `input` is modified internally then returned to its initial state.
     */
    void computeECC(std::vector<WordT>& input, std::vector<WordT>& output) {
        std::size_t N = generator_.size() - 1;
        std::size_t origSize = input.size();
        input.resize(origSize + N, 0);

        math.polyDivide(input, generator_, tmp0, output);

        input.resize(origSize);
    }

    /**
     * Decodes the message and performs any necessary error correction.
     * Returns true on success, false if RS could not recover.
     */
    bool correct(std::vector<WordT>& input, std::vector<WordT>& ecc) {
        computeSyndromes(input, ecc, syndromes);
        findErrorLocatorPolynomialBM(syndromes, errorLocatorPoly);
        if (errorLocatorPoly.size() == 1) {
            errorLocations.clear();
            return true;
        }
        if (!findErrorLocations_BruteForce(
                errorLocatorPoly,
                static_cast<int32_t>(input.size() + ecc.size()),
                errorLocations))
            return false;

        correctErrors(input,
                      static_cast<int32_t>(input.size() + ecc.size()),
                      syndromes, errorLocatorPoly, errorLocations);
        return true;
    }

    /**
     * Computes the syndromes for the message (input + ecc). If there's no
     * error then the output will be zero.
     */
    void computeSyndromes(const std::vector<WordT>& input,
                          const std::vector<WordT>& ecc,
                          std::vector<WordT>& syndromesOut) {
        if (input.size() + ecc.size() >
            static_cast<std::size_t>(math.num_values))
            throw std::invalid_argument(
                "Combined size of input and ecc is larger than the field");
        syndromesOut.resize(syndromeLength());
        for (std::size_t i = 0; i < syndromesOut.size(); i++) {
            int32_t val = generatorPower(static_cast<int32_t>(i));
            int32_t eval = math.polyEval(input, val);
            syndromesOut[i] = static_cast<WordT>(
                math.polyEvalContinue(eval, ecc, val));
        }
    }

    /**
     * Berlekamp-Massey: compute the error locator polynomial from syndromes.
     */
    void findErrorLocatorPolynomialBM(const std::vector<WordT>& syndromesIn,
                                      std::vector<WordT>& errorLocator) {
        std::vector<WordT>& C = errorLocator;  // error polynomial
        std::vector<WordT>& B = err_eval;       // previous error polynomial

        initToOne(C, syndromesIn.size() + 1);
        initToOne(B, syndromesIn.size() + 1);

        std::vector<WordT>& tmp = errorX;
        tmp.resize(syndromesIn.size());

        // int L = 0;
        // int m = 1;  // stores how much B is 'shifted' by
        int32_t b = 1;

        for (std::size_t n = 0; n < syndromesIn.size(); n++) {
            // Compute discrepancy delta
            int32_t delta = syndromesIn[n];

            for (std::size_t j = 1; j < C.size(); j++) {
                delta ^= math.multiply(C[C.size() - j - 1],
                                       syndromesIn[n - j]);
            }

            // B = D^m * B
            B.push_back(0);

            // Step 3 is implicitly handled
            // m = m + 1

            if (delta != 0) {
                int32_t scale = math.multiply(delta, math.inverse(b));
                math.polyAddScaleB(C, B, scale, tmp);

                if (B.size() <= C.size()) {
                    // if 2*L > N — Step 4
                    // m += 1;
                } else {
                    // if 2*L <= N — Step 5
                    B = C;
                    // L = n+1-L;
                    b = delta;
                    // m = 1;
                }
                C = tmp;
            }
        }

        removeLeadingZeros(C);
    }

    /**
     * Compute the error locator polynomial when given the error locations
     * directly. Used by tests to cross-check the BM result.
     */
    void findErrorLocatorPolynomial(int32_t messageLength,
                                    const std::vector<int32_t>& errorLocs,
                                    std::vector<WordT>& errorLocator) {
        tmp1.assign(2, 0);
        tmp1[1] = 1;
        errorLocator.assign(1, 0);
        errorLocator[0] = 1;
        for (std::size_t i = 0; i < errorLocs.size(); i++) {
            int32_t where = messageLength - errorLocs[i] - 1;

            // tmp1 = [2**w, 1]
            tmp1[0] = static_cast<WordT>(math.power(2, where));
            // tmp1[1] = 1;

            tmp0 = errorLocator;
            math.polyMult(tmp0, tmp1, errorLocator);
        }
    }

    /**
     * Brute-force error-location search. Returns true if exactly the
     * expected number of locations were found.
     */
    bool findErrorLocations_BruteForce(const std::vector<WordT>& errorLocator,
                                       int32_t messageLength,
                                       std::vector<int32_t>& locations) {
        locations.clear();
        for (int32_t i = 0; i < messageLength; i++) {
            if (math.polyEval_S(errorLocator, math.power(2, i)) == 0) {
                locations.push_back(messageLength - i - 1);
            }
        }
        return locations.size() == errorLocator.size() - 1;
    }

    /**
     * Forney algorithm: compute correction values for each errored position
     * and XOR them into the message in place.
     */
    void correctErrors(std::vector<WordT>& message,
                       int32_t length_msg_ecc,
                       const std::vector<WordT>& syndromesIn,
                       const std::vector<WordT>& errorLocator,
                       const std::vector<int32_t>& errorLocs) {
        findErrorEvaluator(syndromesIn, errorLocator, err_eval);

        // Compute error positions
        errorX.assign(errorLocs.size(), 0);
        for (std::size_t i = 0; i < errorLocs.size(); i++) {
            int32_t coef_pos = length_msg_ecc - errorLocs[i] - 1;
            errorX[i] = static_cast<WordT>(math.power(2, coef_pos));
            // Reference variant (BoofCV comment):
            // int coef_pos = math.max_value - (length_msg_ecc - errorLocs[i] - 1);
            // errorX[i] = (WordT)math.power_n(2, -coef_pos);
        }

        err_loc_prime_tmp.resize(errorX.size());

        for (std::size_t i = 0; i < errorX.size(); i++) {
            int32_t Xi = errorX[i];
            int32_t Xi_inv = math.inverse(Xi);

            // Compute the polynomial derivative
            err_loc_prime_tmp.clear();
            for (std::size_t j = 0; j < errorX.size(); j++) {
                if (i == j)
                    continue;
                err_loc_prime_tmp.push_back(static_cast<WordT>(
                    GaliosFieldOps::subtract(
                        1, math.multiply(Xi_inv, errorX[j]))));
            }
            // compute the product (errata locator derivative — Forney denom)
            int32_t err_loc_prime = 1;
            for (std::size_t j = 0; j < err_loc_prime_tmp.size(); j++) {
                err_loc_prime = math.multiply(err_loc_prime,
                                              err_loc_prime_tmp[j]);
            }

            int32_t y = math.polyEval_S(err_eval, Xi_inv);
            y = math.multiply(math.power(Xi, 1), y);

            // Compute the magnitude
            int32_t magnitude = math.divide(y, err_loc_prime);
            if (generatorBase != 0) {
                magnitude = math.multiply(magnitude, Xi_inv);
            }

            // only apply a correction if it's part of the message and not the ECC
            int32_t loc = errorLocs[i];
            if (static_cast<std::size_t>(loc) < message.size())
                message[static_cast<std::size_t>(loc)] = static_cast<WordT>(
                    message[static_cast<std::size_t>(loc)] ^ magnitude);
        }
    }

    /**
     * Compute the error evaluator polynomial Omega.
     */
    void findErrorEvaluator(const std::vector<WordT>& syndromesIn,
                            const std::vector<WordT>& errorLocator,
                            std::vector<WordT>& evaluator) {
        math.polyMult_flipA(syndromesIn, errorLocator, evaluator);
        std::size_t N = errorLocator.size() - 1;
        std::size_t offset = evaluator.size() - N;
        for (std::size_t i = 0; i < N; i++) {
            evaluator[i] = evaluator[i + offset];
        }
        evaluator[N] = 0;
        evaluator.resize(errorLocator.size());

        // flip evaluator around (TODO remove flip and do it in place)
        for (std::size_t i = 0; i < evaluator.size() / 2; i++) {
            std::size_t j = evaluator.size() - i - 1;
            WordT tmp = evaluator[i];
            evaluator[i] = evaluator[j];
            evaluator[j] = tmp;
        }
    }

    /**
     * Build the generator polynomial of the given degree. Composed of
     * factors (x - α^n), `α` = 2 in `GF(2^numBits)`.
     */
    void generator(int32_t degree) {
        initToOne(generator_, static_cast<std::size_t>(degree) + 1);

        // (1*x - a[i])
        tmp1.assign(2, 0);
        tmp1[0] = 1;
        for (int32_t i = 0; i < degree; i++) {
            tmp1[1] = static_cast<WordT>(generatorPower(i));
            math.polyMult(generator_, tmp1, tmp0);
            generator_ = tmp0;
        }
    }

    void initToOne(std::vector<WordT>& poly, std::size_t length) {
        poly.assign(1, 0);
        poly.reserve(length);
        poly[0] = 1;
    }

    int32_t generatorPower(int32_t level) const {
        return math.power(2, level + generatorBase);
    }

    int32_t getGeneratorBase() const { return generatorBase; }
    int32_t getTotalErrors() const {
        return static_cast<int32_t>(errorLocations.size());
    }

    // Public for parity testing — Java tests poke at math, generator etc.
    Math math;
    std::vector<WordT> generator_;  // trailing _ to avoid clashing with member fn

    std::vector<WordT> tmp0;
    std::vector<WordT> tmp1;

    std::vector<int32_t> errorLocations;
    std::vector<WordT> errorLocatorPoly;
    std::vector<WordT> syndromes;

    // Workspace for error correction. Precomputed to avoid calls to new.
    std::vector<WordT> err_eval;
    std::vector<WordT> errorX;
    std::vector<WordT> err_loc_prime_tmp;

    int32_t generatorBase;

private:
    std::size_t syndromeLength() const { return generator_.size() - 1; }

    static void removeLeadingZeros(std::vector<WordT>& poly) {
        std::size_t count = 0;
        for (; count < poly.size(); count++) {
            if (poly[count] != 0)
                break;
        }
        for (std::size_t i = count; i < poly.size(); i++) {
            poly[i - count] = poly[i];
        }
        poly.resize(poly.size() - count);
    }
};

extern template class ReedSolomonCodesT<std::uint8_t>;
extern template class ReedSolomonCodesT<std::uint16_t>;

using ReedSolomonCodes_U8 = ReedSolomonCodesT<std::uint8_t>;
using ReedSolomonCodes_U16 = ReedSolomonCodesT<std::uint16_t>;

}  // namespace boofcv_qr

#endif  // BOOFCV_QR_REED_SOLOMON_HPP
