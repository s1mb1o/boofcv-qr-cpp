// Port of:
//   boofcv.alg.fiducial.qrcode.GaliosFieldOps
//   boofcv.alg.fiducial.qrcode.GaliosFieldTableOps
// Upstream: BoofCV v1.3.0 (see UPSTREAM_VERSION at repo root).
//
// Algorithm summary lives in src/galois/galois.md.

#ifndef BOOFCV_QR_GALOIS_HPP
#define BOOFCV_QR_GALOIS_HPP

#include <cstdint>
#include <vector>

namespace boofcv_qr {

// Basic operators on polynomial Galois Field (GF) with GF(2) coefficients.
// Polynomials are stored inside ints with the least significant bits first.
// Mirrors boofcv.alg.fiducial.qrcode.GaliosFieldOps. Spelled with the upstream
// typo ("Galios") for cross-reference fidelity.
class GaliosFieldOps {
public:
    static int32_t add(int32_t a, int32_t b);
    static int32_t subtract(int32_t a, int32_t b);

    // Carry-multiply two GF(2) polynomials. Result is NOT reduced into a
    // particular field — it can grow beyond `domain`. Used to populate
    // precomputed tables in GaliosFieldTableOps.
    static int32_t multiply(int32_t a, int32_t b);

    // Russian-Peasant multiplication with reduction by `primitive` so the
    // result stays in GF(2^numBits). `domain` is the field size (e.g. 256
    // for GF(2^8)).
    static int32_t multiply(int32_t x, int32_t y, int32_t primitive, int32_t domain);

    // Polynomial GF(2) modulus: result = dividend mod divisor.
    static int32_t modulus(int32_t dividend, int32_t divisor);

    // Integer (no remainder) polynomial division: result = dividend / divisor.
    static int32_t divide(int32_t dividend, int32_t divisor);

    // 1-based bit length: position of the most-significant non-zero bit + 1.
    // length(0) = 0, length(1) = 1, length(0b100) = 3.
    static int32_t length(int32_t value);

private:
    GaliosFieldOps() = delete;
};


// Precomputed exp/log tables for fast multiply, divide, power, inverse over
// GF(2^numBits) reduced by a primitive polynomial. Mirrors
// boofcv.alg.fiducial.qrcode.GaliosFieldTableOps.
//
// Fields are public (and intentionally so): the Java original declares them
// `protected`, accessed by JUnit tests in the same package. Exposing them
// here lets the ported gtests verify the table contents directly and lets
// downstream RS variants subclass/extend without indirection.
class GaliosFieldTableOps {
public:
    // numBits in [1, 16]. `primitive` is the irreducible polynomial that
    // defines the field (e.g. 0b100011101 for GF(2^8) used by QR).
    GaliosFieldTableOps(int32_t numBits, int32_t primitive);

    // (x*y) mod primitive.
    int32_t multiply(int32_t x, int32_t y) const;

    // result such that divide(multiply(x,y), y) == x for any x and any y != 0.
    int32_t divide(int32_t x, int32_t y) const;

    // x^power mod primitive. Caller must ensure log[x] is defined (x != 0)
    // when power is non-zero.
    int32_t power(int32_t x, int32_t power) const;

    // x^power but with negative-exponent guard (wraps the exp index when
    // log[x]*power goes negative after the modulus).
    int32_t power_n(int32_t x, int32_t power) const;

    // 2^(max - log[x]) mod primitive — multiplicative inverse.
    int32_t inverse(int32_t x) const;

    int32_t max_value;
    int32_t num_values;
    int32_t numBits;
    int32_t primitive;
    std::vector<int32_t> exp;
    std::vector<int32_t> log;
};

}  // namespace boofcv_qr

#endif  // BOOFCV_QR_GALOIS_HPP
