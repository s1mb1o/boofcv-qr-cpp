// Port of GaliosFieldOps and GaliosFieldTableOps from
// boofcv.alg.fiducial.qrcode (BoofCV v1.3.0). Verbatim translation per
// CLAUDE.md "Verbatim vs idiomize" — variable names, loop structure, and
// inline comments preserved.
//
// Algorithm description: src/galois/galois.md.

#include "boofcv_qr/galois.hpp"

#include <stdexcept>

namespace boofcv_qr {

// ---------------------------------------------------------------------------
// GaliosFieldOps
// ---------------------------------------------------------------------------

int32_t GaliosFieldOps::add(int32_t a, int32_t b) {
    return a ^ b;
}

int32_t GaliosFieldOps::subtract(int32_t a, int32_t b) {
    return a ^ b;
}

int32_t GaliosFieldOps::multiply(int32_t a, int32_t b) {
    int32_t z = 0;

    for (int32_t i = 0; (b >> i) > 0; i++) {
        if ((b & (1 << i)) != 0) {
            z ^= a << i;
        }
    }
    return z;
}

int32_t GaliosFieldOps::multiply(int32_t x, int32_t y, int32_t primitive, int32_t domain) {
    int32_t r = 0;
    while (y > 0) {
        if ((y & 1) != 0) {
            r = r ^ x;
        }
        y = y >> 1;
        x = x << 1;

        if (x >= domain) {
            x ^= primitive;
        }
    }
    return r;
}

int32_t GaliosFieldOps::modulus(int32_t dividend, int32_t divisor) {
    // Compute the position of the most significant bit for each integer
    int32_t length_end = length(dividend);
    int32_t length_sor = length(divisor);
    // If the dividend is smaller than the divisor then nothing needs to be done
    if (length_end < length_sor)
        return dividend;

    // Align the most significant 1 of the divisor to the most significant 1
    // of the dividend (by shifting the divisor)
    for (int32_t i = length_end - length_sor; i >= 0; i--) {
        // Check that the dividend is divisible (useless for the first iteration but
        // important for the next ones)
        if ((dividend & (1 << (i + length_sor - 1))) != 0) {
            // if divisible, then shift the divisor to align the most significant bits and subtract.
            dividend ^= divisor << i;
        }
    }
    return dividend;
}

int32_t GaliosFieldOps::divide(int32_t dividend, int32_t divisor) {
    int32_t length_end = length(dividend);
    int32_t length_sor = length(divisor);

    if (length_end < length_sor)
        return 0;

    int32_t result = 0;

    for (int32_t i = length_end - length_sor; i >= 0; i--) {
        if ((dividend & (1 << (i + length_sor - 1))) != 0) {
            dividend ^= divisor << i;
            result |= 1 << i;
        }
    }
    return result;
}

int32_t GaliosFieldOps::length(int32_t value) {
    int32_t length = 0;
    while ((value >> length) != 0) {
        length++;
    }
    return length;
}

// ---------------------------------------------------------------------------
// GaliosFieldTableOps
// ---------------------------------------------------------------------------

GaliosFieldTableOps::GaliosFieldTableOps(int32_t numBits, int32_t primitive) {
    if (numBits < 1 || numBits > 16)
        throw std::invalid_argument(
            "Degree must be more than 1 and less than or equal to 16");

    this->numBits = numBits;
    this->primitive = primitive;
    max_value = 0;
    for (int32_t i = 0; i < numBits; i++) {
        max_value |= 1 << i;
    }
    num_values = max_value + 1;

    log.assign(static_cast<std::size_t>(num_values), 0);
    // make it twice as long to avoid a modulus operation
    exp.assign(static_cast<std::size_t>(num_values) * 2, 0);

    // exhaustively compute all values
    int32_t x = 1;
    for (int32_t i = 0; i < max_value; i++) {
        exp[static_cast<std::size_t>(i)] = x;
        log[static_cast<std::size_t>(x)] = i;
        x = GaliosFieldOps::multiply(x, 2, primitive, num_values);
    }

    for (int32_t i = 0; i < num_values; i++) {
        exp[static_cast<std::size_t>(i + max_value)] =
            exp[static_cast<std::size_t>(i)];
    }
}

int32_t GaliosFieldTableOps::multiply(int32_t x, int32_t y) const {
    if (x == 0 || y == 0)
        return 0;
    return exp[static_cast<std::size_t>(log[static_cast<std::size_t>(x)] +
                                        log[static_cast<std::size_t>(y)])];
}

int32_t GaliosFieldTableOps::divide(int32_t x, int32_t y) const {
    if (y == 0)
        throw std::runtime_error("Divide by zero");
    if (x == 0)
        return 0;

    return exp[static_cast<std::size_t>(log[static_cast<std::size_t>(x)] +
                                        max_value -
                                        log[static_cast<std::size_t>(y)])];
}

int32_t GaliosFieldTableOps::power(int32_t x, int32_t power) const {
    return exp[static_cast<std::size_t>(
        (log[static_cast<std::size_t>(x)] * power) % max_value)];
}

int32_t GaliosFieldTableOps::power_n(int32_t x, int32_t power) const {
    int32_t a = (log[static_cast<std::size_t>(x)] * power) % max_value;
    if (a < 0)
        a = max_value * 2 + a;
    return exp[static_cast<std::size_t>(a)];
}

int32_t GaliosFieldTableOps::inverse(int32_t x) const {
    return exp[static_cast<std::size_t>(max_value -
                                        log[static_cast<std::size_t>(x)])];
}

}  // namespace boofcv_qr
