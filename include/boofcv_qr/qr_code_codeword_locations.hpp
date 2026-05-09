// Port of boofcv.alg.fiducial.qrcode.QrCodeCodeWordLocations (BoofCV v1.3.0).
//
// Pre-computes which (col, row) modules in a QR grid are "feature"
// (finder, alignment, timing, format/version) vs "data". Then snakes
// through the data modules in the spec-defined zigzag order to produce
// the bit-extraction sequence used by the sampler.
//
// Algorithm description: src/sampler/qr_code_codeword_locations.md.

#ifndef BOOFCV_QR_QR_CODE_CODEWORD_LOCATIONS_HPP
#define BOOFCV_QR_QR_CODE_CODEWORD_LOCATIONS_HPP

#include <cstdint>
#include <vector>

namespace boofcv_qr {

// (col, row) integer point. Mirrors georegression.Point2D_I32.
struct Point2I {
    int32_t x;
    int32_t y;
    Point2I() : x(0), y(0) {}
    Point2I(int32_t x_, int32_t y_) : x(x_), y(y_) {}
    bool operator==(const Point2I& o) const { return x == o.x && y == o.y; }
};

class QrCodeCodeWordLocations {
public:
    // numRows == numCols == totalModules(version) == version*4 + 17.
    int32_t numRows = 0;
    int32_t numCols = 0;

    // True == "feature" / non-data module.
    std::vector<bool> data;

    // Bit-extraction order: each entry is the (col, row) of the next
    // data module to sample.
    std::vector<Point2I> bits;

    // Build the QR-code mask (uses VERSION_INFO[version].alignment).
    static QrCodeCodeWordLocations qrcode(int32_t version);

    // Number of data bits available (not the same as RS data; raw module count).
    int32_t getTotalDataBits() const;

    bool get(int32_t row, int32_t col) const {
        return data[static_cast<std::size_t>(row * numCols + col)];
    }
    void set(int32_t row, int32_t col, bool v) {
        data[static_cast<std::size_t>(row * numCols + col)] = v;
    }

private:
    explicit QrCodeCodeWordLocations(int32_t numModules);

    void featureMaskQrCode(int32_t numModules,
                           const std::vector<int32_t>& alignment,
                           bool hasVersion);
    void computeBitLocations(bool isMicro);
    void markSquare(int32_t row, int32_t col, int32_t width);
    void markRectangle(int32_t row, int32_t col, int32_t width, int32_t height);
};

}  // namespace boofcv_qr

#endif  // BOOFCV_QR_QR_CODE_CODEWORD_LOCATIONS_HPP
