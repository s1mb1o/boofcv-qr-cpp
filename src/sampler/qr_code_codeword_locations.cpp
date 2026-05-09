// Port of QrCodeCodeWordLocations. Verbatim per CLAUDE.md.

#include "boofcv_qr/qr_code_codeword_locations.hpp"
#include "boofcv_qr/qr_code.hpp"

namespace boofcv_qr {

QrCodeCodeWordLocations::QrCodeCodeWordLocations(int32_t numModules)
    : numRows(numModules), numCols(numModules),
      data(static_cast<std::size_t>(numModules) *
               static_cast<std::size_t>(numModules),
           false) {}

QrCodeCodeWordLocations QrCodeCodeWordLocations::qrcode(int32_t version) {
    int32_t numModules = QrCode::totalModules(version);
    const std::vector<int32_t>& alignment =
        QrCode::VERSION_INFO()[static_cast<std::size_t>(version)].alignment;
    bool hasVersion = version >= QrCode::VERSION_ENCODED_AT;

    QrCodeCodeWordLocations locations(numModules);
    locations.featureMaskQrCode(numModules, alignment, hasVersion);
    locations.computeBitLocations(false);
    return locations;
}

int32_t QrCodeCodeWordLocations::getTotalDataBits() const {
    int32_t sum = 0;
    for (bool b : data) sum += b ? 1 : 0;
    return numRows * numRows - sum;
}

void QrCodeCodeWordLocations::featureMaskQrCode(
    int32_t numModules, const std::vector<int32_t>& alignment, bool hasVersion) {
    // mark alignment patterns + format info
    markSquare(0, 0, 9);
    markRectangle(numModules - 8, 0, 9, 8);
    markRectangle(0, numModules - 8, 8, 9);

    // timing pattern
    markRectangle(8, 6, 1, numModules - 8 - 8);
    markRectangle(6, 8, numModules - 8 - 8, 1);

    // version info
    if (hasVersion) {
        markRectangle(numModules - 11, 0, 6, 3);
        markRectangle(0, numModules - 11, 3, 6);
    }

    // alignment patterns
    int32_t n = static_cast<int32_t>(alignment.size());
    for (int32_t i = 0; i < n; i++) {
        int32_t row = alignment[static_cast<std::size_t>(i)];
        for (int32_t j = 0; j < n; j++) {
            if (i == 0 && j == 0) continue;
            if (i == n - 1 && j == 0) continue;
            if (i == 0 && j == n - 1) continue;

            int32_t col = alignment[static_cast<std::size_t>(j)];
            markSquare(row - 2, col - 2, 5);
        }
    }
}

void QrCodeCodeWordLocations::markSquare(int32_t row, int32_t col, int32_t width) {
    for (int32_t i = 0; i < width; i++) {
        for (int32_t j = 0; j < width; j++) {
            set(row + i, col + j, true);
        }
    }
}

void QrCodeCodeWordLocations::markRectangle(int32_t row, int32_t col,
                                            int32_t width, int32_t height) {
    for (int32_t i = 0; i < height; i++) {
        for (int32_t j = 0; j < width; j++) {
            set(row + i, col + j, true);
        }
    }
}

void QrCodeCodeWordLocations::computeBitLocations(bool isMicro) {
    int32_t N = numRows;
    int32_t row = N - 1;
    int32_t col = N - 1;
    int32_t direction = -1;

    while (col > 0) {
        if (col == 6 && !isMicro)
            col -= 1;

        if (!get(row, col)) {
            bits.emplace_back(col, row);
        }
        if (!get(row, col - 1)) {
            bits.emplace_back(col - 1, row);
        }

        row += direction;

        if (row < 0 || row >= N) {
            direction = -direction;
            col -= 2;
            row += direction;
        }
    }
}

}  // namespace boofcv_qr
