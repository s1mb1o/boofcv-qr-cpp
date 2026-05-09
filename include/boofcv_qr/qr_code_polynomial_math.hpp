// Port of boofcv.alg.fiducial.qrcode.QrCodePolynomialMath (BoofCV v1.3.0).
//
// BCH(15,5) format-info codec and BCH(18,6) version-info codec, plus a
// brute-force minimum-Hamming-distance corrector. This is what locks down
// the QR's error-correction level / mask / version BEFORE Reed-Solomon
// runs on the data codewords.
//
// Algorithm description: src/decoder/qr_code_polynomial_math.md.

#ifndef BOOFCV_QR_QR_CODE_POLYNOMIAL_MATH_HPP
#define BOOFCV_QR_QR_CODE_POLYNOMIAL_MATH_HPP

#include "boofcv_qr/qr_code.hpp"

#include <cstdint>

namespace boofcv_qr {

class QrCodePolynomialMath {
public:
    static constexpr int32_t FORMAT_GENERATOR = 0b10100110111;
    static constexpr int32_t VERSION_GENERATOR = 0b1111100100101;

    // BCH(18,6) version codec.
    static int32_t encodeVersionBits(int32_t version);
    static bool checkVersionBits(int32_t bits);
    // Returns the corrected 6-bit version, or -1 if ambiguous.
    static int32_t correctVersionBits(int32_t bits);

    // BCH(15,5) format codec.
    static int32_t encodeFormatBits(ErrorLevel level, int32_t mask);
    static int32_t encodeFormatBits(int32_t message5bits);
    static bool checkFormatBits(int32_t bitsNoMask);
    // Returns the corrected 5-bit format message, or -1 if ambiguous.
    static int32_t correctFormatBits(int32_t bitsNoMask);

    // Populate qr.error / qr.mask from the 5-bit format message.
    // Caller must have already error-corrected and mask-stripped it.
    static void decodeFormatMessage(int32_t message, QrCode& qr);

    // Brute-force minimum-Hamming-distance corrector for both BCH codes.
    // Returns -1 if ambiguous (two messages tied at min distance).
    static int32_t correctDCH(int32_t N, int32_t messageNoMask, int32_t generator,
                              int32_t totalBits, int32_t dataBits);

    // GF(2)[x] polynomial division remainder. Used by both codecs.
    static int32_t bitPolyModulus(int32_t data, int32_t generator,
                                  int32_t totalBits, int32_t dataBits);

private:
    QrCodePolynomialMath() = delete;
};

}  // namespace boofcv_qr

#endif  // BOOFCV_QR_QR_CODE_POLYNOMIAL_MATH_HPP
