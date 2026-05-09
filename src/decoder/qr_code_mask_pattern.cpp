// Port of QrCodeMaskPattern. The mask formulas come from ISO/IEC 18004
// §7.8.2 / Table 10. Verbatim per CLAUDE.md — XOR identity preserved
// even where it could be simplified.

#include "boofcv_qr/qr_code_mask_pattern.hpp"

#include <stdexcept>

namespace boofcv_qr {

namespace {

class M000Impl final : public QrCodeMaskPattern {
public:
    M000Impl() : QrCodeMaskPattern(0b000) {}
    int32_t apply(int32_t row, int32_t col, int32_t bitValue) const override {
        int32_t mask = (row + col) % 2;
        return bitValue ^ ((~mask) & 0x1);
    }
    std::string name() const override { return "M000"; }
};

class M001Impl final : public QrCodeMaskPattern {
public:
    M001Impl() : QrCodeMaskPattern(0b001) {}
    int32_t apply(int32_t row, int32_t /*col*/, int32_t bitValue) const override {
        int32_t mask = row % 2;
        return bitValue ^ ((~mask) & 0x1);
    }
    std::string name() const override { return "M001"; }
};

class M010Impl final : public QrCodeMaskPattern {
public:
    M010Impl() : QrCodeMaskPattern(0b010) {}
    int32_t apply(int32_t /*row*/, int32_t col, int32_t bitValue) const override {
        return bitValue ^ (col % 3 == 0 ? 1 : 0);
    }
    std::string name() const override { return "M010"; }
};

class M011Impl final : public QrCodeMaskPattern {
public:
    M011Impl() : QrCodeMaskPattern(0b011) {}
    int32_t apply(int32_t row, int32_t col, int32_t bitValue) const override {
        return bitValue ^ ((row + col) % 3 == 0 ? 1 : 0);
    }
    std::string name() const override { return "M011"; }
};

class M100Impl final : public QrCodeMaskPattern {
public:
    M100Impl() : QrCodeMaskPattern(0b100) {}
    int32_t apply(int32_t row, int32_t col, int32_t bitValue) const override {
        int32_t mask = (row / 2 + col / 3) % 2;
        return bitValue ^ ((~mask) & 0x1);
    }
    std::string name() const override { return "M100"; }
};

class M101Impl final : public QrCodeMaskPattern {
public:
    M101Impl() : QrCodeMaskPattern(0b101) {}
    int32_t apply(int32_t row, int32_t col, int32_t bitValue) const override {
        return bitValue ^ ((row * col) % 2 + (row * col) % 3 == 0 ? 1 : 0);
    }
    std::string name() const override { return "M101"; }
};

class M110Impl final : public QrCodeMaskPattern {
public:
    M110Impl() : QrCodeMaskPattern(0b110) {}
    int32_t apply(int32_t row, int32_t col, int32_t bitValue) const override {
        return bitValue ^
               (((row * col) % 2 + (row * col) % 3) % 2 == 0 ? 1 : 0);
    }
    std::string name() const override { return "M110"; }
};

class M111Impl final : public QrCodeMaskPattern {
public:
    M111Impl() : QrCodeMaskPattern(0b111) {}
    int32_t apply(int32_t row, int32_t col, int32_t bitValue) const override {
        int32_t mask = ((row * col) % 3 + (row + col) % 2) % 2;
        return bitValue ^ ((~mask) & 0x1);
    }
    std::string name() const override { return "M111"; }
};

}  // namespace

const QrCodeMaskPattern& QrCodeMaskPattern::M000() {
    static const M000Impl inst;
    return inst;
}
const QrCodeMaskPattern& QrCodeMaskPattern::M001() {
    static const M001Impl inst;
    return inst;
}
const QrCodeMaskPattern& QrCodeMaskPattern::M010() {
    static const M010Impl inst;
    return inst;
}
const QrCodeMaskPattern& QrCodeMaskPattern::M011() {
    static const M011Impl inst;
    return inst;
}
const QrCodeMaskPattern& QrCodeMaskPattern::M100() {
    static const M100Impl inst;
    return inst;
}
const QrCodeMaskPattern& QrCodeMaskPattern::M101() {
    static const M101Impl inst;
    return inst;
}
const QrCodeMaskPattern& QrCodeMaskPattern::M110() {
    static const M110Impl inst;
    return inst;
}
const QrCodeMaskPattern& QrCodeMaskPattern::M111() {
    static const M111Impl inst;
    return inst;
}

const QrCodeMaskPattern& QrCodeMaskPattern::lookupMask(int32_t maskPattern) {
    switch (maskPattern) {
        case 0b000: return M000();
        case 0b001: return M001();
        case 0b010: return M010();
        case 0b011: return M011();
        case 0b100: return M100();
        case 0b101: return M101();
        case 0b110: return M110();
        case 0b111: return M111();
        default:
            throw std::runtime_error(
                "Unknown mask: " + std::to_string(maskPattern));
    }
}

const QrCodeMaskPattern& QrCodeMaskPattern::lookupMask(const std::string& maskPattern) {
    // Accept either "M000" or "000".
    std::string s = maskPattern;
    if (s.size() == 4 && s[0] == 'M') s = s.substr(1);
    if (s == "000") return M000();
    if (s == "001") return M001();
    if (s == "010") return M010();
    if (s == "011") return M011();
    if (s == "100") return M100();
    if (s == "101") return M101();
    if (s == "110") return M110();
    if (s == "111") return M111();
    throw std::runtime_error("Unknown mask: " + maskPattern);
}

}  // namespace boofcv_qr
