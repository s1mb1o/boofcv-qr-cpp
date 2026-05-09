// Port of QrCodePolynomialMath. Verbatim per CLAUDE.md.

#include "boofcv_qr/qr_code_polynomial_math.hpp"
#include "boofcv_qr/qr_code_mask_pattern.hpp"

#include <cstdint>

namespace boofcv_qr {

namespace {

// Hamming distance between two bit patterns. Mirrors
// boofcv.alg.descriptor.DescriptorDistance.hamming(int) — pop-count of
// the XOR. We inline it here rather than dragging the descriptor
// module into our dependency graph.
inline int32_t hamming_pop(int32_t x) {
    int32_t count = 0;
    while (x != 0) {
        count += x & 1;
        x = static_cast<int32_t>(static_cast<uint32_t>(x) >> 1);
    }
    return count;
}

}  // namespace

int32_t QrCodePolynomialMath::encodeVersionBits(int32_t version) {
    int32_t message = version << 12;
    return message ^ bitPolyModulus(message, VERSION_GENERATOR, 18, 6);
}

bool QrCodePolynomialMath::checkVersionBits(int32_t bits) {
    return bitPolyModulus(bits, VERSION_GENERATOR, 18, 6) == 0;
}

int32_t QrCodePolynomialMath::correctVersionBits(int32_t bits) {
    return correctDCH(64, bits, VERSION_GENERATOR, 18, 6);
}

int32_t QrCodePolynomialMath::encodeFormatBits(ErrorLevel level, int32_t mask) {
    return encodeFormatBits((ErrorLevel_value(level) << 3) |
                             (mask & static_cast<int32_t>(0xFFFFFFF7)));
}

int32_t QrCodePolynomialMath::encodeFormatBits(int32_t message5bits) {
    int32_t message = message5bits << 10;
    return message ^ bitPolyModulus(message, FORMAT_GENERATOR, 15, 5);
}

bool QrCodePolynomialMath::checkFormatBits(int32_t bitsNoMask) {
    return bitPolyModulus(bitsNoMask, FORMAT_GENERATOR, 15, 5) == 0;
}

void QrCodePolynomialMath::decodeFormatMessage(int32_t message, QrCode& qr) {
    int32_t error = message >> 3;

    qr.error = ErrorLevel_lookup(error);
    qr.mask = &QrCodeMaskPattern::lookupMask(message & 0x07);
}

int32_t QrCodePolynomialMath::correctFormatBits(int32_t bitsNoMask) {
    return correctDCH(32, bitsNoMask, FORMAT_GENERATOR, 15, 5);
}

int32_t QrCodePolynomialMath::correctDCH(int32_t N, int32_t messageNoMask,
                                          int32_t generator, int32_t totalBits,
                                          int32_t dataBits) {
    int32_t bestHamming = 255;
    int32_t bestMessage = -1;

    int32_t errorBits = totalBits - dataBits;

    // exhaustively check all possibilities
    for (int32_t i = 0; i < N; i++) {
        int32_t test = i << errorBits;
        test = test ^ bitPolyModulus(test, generator, totalBits, dataBits);

        int32_t distance = hamming_pop(test ^ messageNoMask);

        if (distance < bestHamming) {
            bestHamming = distance;
            bestMessage = i;
        } else if (distance == bestHamming) {
            // ambiguous so reject
            bestMessage = -1;
        }
    }
    return bestMessage;
}

int32_t QrCodePolynomialMath::bitPolyModulus(int32_t data, int32_t generator,
                                              int32_t totalBits, int32_t dataBits) {
    int32_t errorBits = totalBits - dataBits;
    for (int32_t i = dataBits - 1; i >= 0; i--) {
        if ((data & (1 << (i + errorBits))) != 0) {
            data ^= generator << i;
        }
    }
    return data;
}

}  // namespace boofcv_qr
