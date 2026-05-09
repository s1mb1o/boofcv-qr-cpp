// Port of boofcv.alg.fiducial.qrcode.QrCodeMaskPattern (BoofCV v1.3.0).
//
// QR mask patterns: 8 fixed XOR functions over (row, col) that the
// encoder applies to the data modules to break up runs of like-coloured
// modules. The decoder applies the same mask in reverse to recover the
// original bits. Mask choice is encoded in the format-info bits.
//
// Algorithm description: src/decoder/qr_code_mask_pattern.md.

#ifndef BOOFCV_QR_QR_CODE_MASK_PATTERN_HPP
#define BOOFCV_QR_QR_CODE_MASK_PATTERN_HPP

#include <cstdint>
#include <string>

namespace boofcv_qr {

class QrCodeMaskPattern {
public:
    int32_t bits;
    explicit QrCodeMaskPattern(int32_t bits_) : bits(bits_) {}
    virtual ~QrCodeMaskPattern() = default;

    // Apply the mask: returns bitValue ^ mask(row, col).
    virtual int32_t apply(int32_t row, int32_t col, int32_t bitValue) const = 0;

    virtual std::string name() const = 0;

    // Singletons. Return references because they must be addressable
    // (QrCode::mask is a non-owning pointer).
    static const QrCodeMaskPattern& M000();
    static const QrCodeMaskPattern& M001();
    static const QrCodeMaskPattern& M010();
    static const QrCodeMaskPattern& M011();
    static const QrCodeMaskPattern& M100();
    static const QrCodeMaskPattern& M101();
    static const QrCodeMaskPattern& M110();
    static const QrCodeMaskPattern& M111();

    // Lookup by 3-bit pattern code. Throws on unknown.
    static const QrCodeMaskPattern& lookupMask(int32_t maskPattern);

    // Lookup by name string ("M000" or "000"). Throws on unknown.
    static const QrCodeMaskPattern& lookupMask(const std::string& maskPattern);
};

}  // namespace boofcv_qr

#endif  // BOOFCV_QR_QR_CODE_MASK_PATTERN_HPP
