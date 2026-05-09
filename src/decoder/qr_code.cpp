// Port of QrCode struct + VERSION_INFO[] table.
//
// The static initialiser block in BoofCV's QrCode.java populates 40
// VersionInfo entries from a "manually entered from QR Code table"
// data dump. We replicate that same data verbatim — the comment that
// the values are "magic numbers" with no closed-form derivation also
// holds for our port.
//
// Source values: ISO/IEC 18004:2015 Tables 9 (capacity per ECC level)
// and E.1 (alignment-pattern centre coordinates).

#include "boofcv_qr/qr_code.hpp"
#include "boofcv_qr/qr_code_mask_pattern.hpp"

#include <stdexcept>

namespace boofcv_qr {

int32_t VersionInfo::totalDataBytes(ErrorLevel error) const {
    auto it = levels.find(error);
    if (it == levels.end())
        throw std::invalid_argument(
            "ErrorLevel not present in VersionInfo");
    const BlockInfo& b = it->second;
    int32_t remaining = codewords - b.codewords * b.blocks;
    int32_t blocksB = remaining / (b.codewords + 1);

    return b.dataCodewords * b.blocks + (b.dataCodewords + 1) * blocksB;
}

namespace {

using AT = std::array<VersionInfo, QrCode::MAX_VERSION + 1>;

AT buildVersionInfoTable() {
    AT t{};

    // index 0 unused; entries follow ISO 18004 Tables 9 + E.1
    t[1] = VersionInfo(26, {});
    t[1].add(ErrorLevel::L, 26, 19, 1);
    t[1].add(ErrorLevel::M, 26, 16, 1);
    t[1].add(ErrorLevel::Q, 26, 13, 1);
    t[1].add(ErrorLevel::H, 26, 9, 1);

    t[2] = VersionInfo(44, {6, 18});
    t[2].add(ErrorLevel::L, 44, 34, 1);
    t[2].add(ErrorLevel::M, 44, 28, 1);
    t[2].add(ErrorLevel::Q, 44, 22, 1);
    t[2].add(ErrorLevel::H, 44, 16, 1);

    t[3] = VersionInfo(70, {6, 22});
    t[3].add(ErrorLevel::L, 70, 55, 1);
    t[3].add(ErrorLevel::M, 70, 44, 1);
    t[3].add(ErrorLevel::Q, 35, 17, 2);
    t[3].add(ErrorLevel::H, 35, 13, 2);

    t[4] = VersionInfo(100, {6, 26});
    t[4].add(ErrorLevel::L, 100, 80, 1);
    t[4].add(ErrorLevel::M, 50, 32, 2);
    t[4].add(ErrorLevel::Q, 50, 24, 2);
    t[4].add(ErrorLevel::H, 25, 9, 4);

    t[5] = VersionInfo(134, {6, 30});
    t[5].add(ErrorLevel::L, 134, 108, 1);
    t[5].add(ErrorLevel::M, 67, 43, 2);
    t[5].add(ErrorLevel::Q, 33, 15, 2);
    t[5].add(ErrorLevel::H, 33, 11, 2);

    t[6] = VersionInfo(172, {6, 34});
    t[6].add(ErrorLevel::L, 86, 68, 2);
    t[6].add(ErrorLevel::M, 43, 27, 4);
    t[6].add(ErrorLevel::Q, 43, 19, 4);
    t[6].add(ErrorLevel::H, 43, 15, 4);

    t[7] = VersionInfo(196, {6, 22, 38});
    t[7].add(ErrorLevel::L, 98, 78, 2);
    t[7].add(ErrorLevel::M, 49, 31, 4);
    t[7].add(ErrorLevel::Q, 32, 14, 2);
    t[7].add(ErrorLevel::H, 39, 13, 4);

    t[8] = VersionInfo(242, {6, 24, 42});
    t[8].add(ErrorLevel::L, 121, 97, 2);
    t[8].add(ErrorLevel::M, 60, 38, 2);
    t[8].add(ErrorLevel::Q, 40, 18, 4);
    t[8].add(ErrorLevel::H, 40, 14, 4);

    t[9] = VersionInfo(292, {6, 26, 46});
    t[9].add(ErrorLevel::L, 146, 116, 2);
    t[9].add(ErrorLevel::M, 58, 36, 3);
    t[9].add(ErrorLevel::Q, 36, 16, 4);
    t[9].add(ErrorLevel::H, 36, 12, 4);

    t[10] = VersionInfo(346, {6, 28, 50});
    t[10].add(ErrorLevel::L, 86, 68, 2);
    t[10].add(ErrorLevel::M, 69, 43, 4);
    t[10].add(ErrorLevel::Q, 43, 19, 6);
    t[10].add(ErrorLevel::H, 43, 15, 6);

    t[11] = VersionInfo(404, {6, 30, 54});
    t[11].add(ErrorLevel::L, 101, 81, 4);
    t[11].add(ErrorLevel::M, 80, 50, 1);
    t[11].add(ErrorLevel::Q, 50, 22, 4);
    t[11].add(ErrorLevel::H, 36, 12, 3);

    t[12] = VersionInfo(466, {6, 32, 58});
    t[12].add(ErrorLevel::L, 116, 92, 2);
    t[12].add(ErrorLevel::M, 58, 36, 6);
    t[12].add(ErrorLevel::Q, 46, 20, 4);
    t[12].add(ErrorLevel::H, 42, 14, 7);

    t[13] = VersionInfo(532, {6, 34, 62});
    t[13].add(ErrorLevel::L, 133, 107, 4);
    t[13].add(ErrorLevel::M, 59, 37, 8);
    t[13].add(ErrorLevel::Q, 44, 20, 8);
    t[13].add(ErrorLevel::H, 33, 11, 12);

    t[14] = VersionInfo(581, {6, 26, 46, 66});
    t[14].add(ErrorLevel::L, 145, 115, 3);
    t[14].add(ErrorLevel::M, 64, 40, 4);
    t[14].add(ErrorLevel::Q, 36, 16, 11);
    t[14].add(ErrorLevel::H, 36, 12, 11);

    t[15] = VersionInfo(655, {6, 26, 48, 70});
    t[15].add(ErrorLevel::L, 109, 87, 5);
    t[15].add(ErrorLevel::M, 65, 41, 5);
    t[15].add(ErrorLevel::Q, 54, 24, 5);
    t[15].add(ErrorLevel::H, 36, 12, 11);

    t[16] = VersionInfo(733, {6, 26, 50, 74});
    t[16].add(ErrorLevel::L, 122, 98, 5);
    t[16].add(ErrorLevel::M, 73, 45, 7);
    t[16].add(ErrorLevel::Q, 43, 19, 15);
    t[16].add(ErrorLevel::H, 45, 15, 3);

    t[17] = VersionInfo(815, {6, 30, 54, 78});
    t[17].add(ErrorLevel::L, 135, 107, 1);
    t[17].add(ErrorLevel::M, 74, 46, 10);
    t[17].add(ErrorLevel::Q, 50, 22, 1);
    t[17].add(ErrorLevel::H, 42, 14, 2);

    t[18] = VersionInfo(901, {6, 30, 56, 82});
    t[18].add(ErrorLevel::L, 150, 120, 5);
    t[18].add(ErrorLevel::M, 69, 43, 9);
    t[18].add(ErrorLevel::Q, 50, 22, 17);
    t[18].add(ErrorLevel::H, 42, 14, 2);

    t[19] = VersionInfo(991, {6, 30, 58, 86});
    t[19].add(ErrorLevel::L, 141, 113, 3);
    t[19].add(ErrorLevel::M, 70, 44, 3);
    t[19].add(ErrorLevel::Q, 47, 21, 17);
    t[19].add(ErrorLevel::H, 39, 13, 9);

    t[20] = VersionInfo(1085, {6, 34, 62, 90});
    t[20].add(ErrorLevel::L, 135, 107, 3);
    t[20].add(ErrorLevel::M, 67, 41, 3);
    t[20].add(ErrorLevel::Q, 54, 24, 15);
    t[20].add(ErrorLevel::H, 43, 15, 15);

    t[21] = VersionInfo(1156, {6, 28, 50, 72, 94});
    t[21].add(ErrorLevel::L, 144, 116, 4);
    t[21].add(ErrorLevel::M, 68, 42, 17);
    t[21].add(ErrorLevel::Q, 50, 22, 17);
    t[21].add(ErrorLevel::H, 46, 16, 19);

    t[22] = VersionInfo(1258, {6, 26, 50, 74, 98});
    t[22].add(ErrorLevel::L, 139, 111, 2);
    t[22].add(ErrorLevel::M, 74, 46, 17);
    t[22].add(ErrorLevel::Q, 54, 24, 7);
    t[22].add(ErrorLevel::H, 37, 13, 34);

    t[23] = VersionInfo(1364, {6, 30, 54, 78, 102});
    t[23].add(ErrorLevel::L, 151, 121, 4);
    t[23].add(ErrorLevel::M, 75, 47, 4);
    t[23].add(ErrorLevel::Q, 54, 24, 11);
    t[23].add(ErrorLevel::H, 45, 15, 16);

    t[24] = VersionInfo(1474, {6, 28, 54, 80, 106});
    t[24].add(ErrorLevel::L, 147, 117, 6);
    t[24].add(ErrorLevel::M, 73, 45, 6);
    t[24].add(ErrorLevel::Q, 54, 24, 11);
    t[24].add(ErrorLevel::H, 46, 16, 30);

    t[25] = VersionInfo(1588, {6, 32, 58, 84, 110});
    t[25].add(ErrorLevel::L, 132, 106, 8);
    t[25].add(ErrorLevel::M, 75, 47, 8);
    t[25].add(ErrorLevel::Q, 54, 24, 7);
    t[25].add(ErrorLevel::H, 45, 15, 22);

    t[26] = VersionInfo(1706, {6, 30, 58, 86, 114});
    t[26].add(ErrorLevel::L, 142, 114, 10);
    t[26].add(ErrorLevel::M, 74, 46, 19);
    t[26].add(ErrorLevel::Q, 50, 22, 28);
    t[26].add(ErrorLevel::H, 46, 16, 33);

    t[27] = VersionInfo(1828, {6, 34, 62, 90, 118});
    t[27].add(ErrorLevel::L, 152, 122, 8);
    t[27].add(ErrorLevel::M, 73, 45, 22);
    t[27].add(ErrorLevel::Q, 53, 23, 8);
    t[27].add(ErrorLevel::H, 45, 15, 12);

    t[28] = VersionInfo(1921, {6, 26, 50, 74, 98, 122});
    t[28].add(ErrorLevel::L, 147, 117, 3);
    t[28].add(ErrorLevel::M, 73, 45, 3);
    t[28].add(ErrorLevel::Q, 54, 24, 4);
    t[28].add(ErrorLevel::H, 45, 15, 11);

    t[29] = VersionInfo(2051, {6, 30, 54, 78, 102, 126});
    t[29].add(ErrorLevel::L, 146, 116, 7);
    t[29].add(ErrorLevel::M, 73, 45, 21);
    t[29].add(ErrorLevel::Q, 53, 23, 1);
    t[29].add(ErrorLevel::H, 45, 15, 19);

    t[30] = VersionInfo(2185, {6, 26, 52, 78, 104, 130});
    t[30].add(ErrorLevel::L, 145, 115, 5);
    t[30].add(ErrorLevel::M, 75, 47, 19);
    t[30].add(ErrorLevel::Q, 54, 24, 15);
    t[30].add(ErrorLevel::H, 45, 15, 23);

    t[31] = VersionInfo(2323, {6, 30, 56, 82, 108, 134});
    t[31].add(ErrorLevel::L, 145, 115, 13);
    t[31].add(ErrorLevel::M, 74, 46, 2);
    t[31].add(ErrorLevel::Q, 54, 24, 42);
    t[31].add(ErrorLevel::H, 45, 15, 23);

    t[32] = VersionInfo(2465, {6, 34, 60, 86, 112, 138});
    t[32].add(ErrorLevel::L, 145, 115, 17);
    t[32].add(ErrorLevel::M, 74, 46, 10);
    t[32].add(ErrorLevel::Q, 54, 24, 10);
    t[32].add(ErrorLevel::H, 45, 15, 19);

    t[33] = VersionInfo(2611, {6, 30, 58, 86, 114, 142});
    t[33].add(ErrorLevel::L, 145, 115, 17);
    t[33].add(ErrorLevel::M, 74, 46, 14);
    t[33].add(ErrorLevel::Q, 54, 24, 29);
    t[33].add(ErrorLevel::H, 45, 15, 11);

    t[34] = VersionInfo(2761, {6, 34, 62, 90, 118, 146});
    t[34].add(ErrorLevel::L, 145, 115, 13);
    t[34].add(ErrorLevel::M, 74, 46, 14);
    t[34].add(ErrorLevel::Q, 54, 24, 44);
    t[34].add(ErrorLevel::H, 46, 16, 59);

    t[35] = VersionInfo(2876, {6, 30, 54, 78, 102, 126, 150});
    t[35].add(ErrorLevel::L, 151, 121, 12);
    t[35].add(ErrorLevel::M, 75, 47, 12);
    t[35].add(ErrorLevel::Q, 54, 24, 39);
    t[35].add(ErrorLevel::H, 45, 15, 22);

    t[36] = VersionInfo(3034, {6, 24, 50, 76, 102, 128, 154});
    t[36].add(ErrorLevel::L, 151, 121, 6);
    t[36].add(ErrorLevel::M, 75, 47, 6);
    t[36].add(ErrorLevel::Q, 54, 24, 46);
    t[36].add(ErrorLevel::H, 45, 15, 2);

    t[37] = VersionInfo(3196, {6, 28, 54, 80, 106, 132, 158});
    t[37].add(ErrorLevel::L, 152, 122, 17);
    t[37].add(ErrorLevel::M, 74, 46, 29);
    t[37].add(ErrorLevel::Q, 54, 24, 49);
    t[37].add(ErrorLevel::H, 45, 15, 24);

    t[38] = VersionInfo(3362, {6, 32, 58, 84, 110, 136, 162});
    t[38].add(ErrorLevel::L, 152, 122, 4);
    t[38].add(ErrorLevel::M, 74, 46, 13);
    t[38].add(ErrorLevel::Q, 54, 24, 48);
    t[38].add(ErrorLevel::H, 45, 15, 42);

    t[39] = VersionInfo(3532, {6, 26, 54, 82, 110, 138, 166});
    t[39].add(ErrorLevel::L, 147, 117, 20);
    t[39].add(ErrorLevel::M, 75, 47, 40);
    t[39].add(ErrorLevel::Q, 54, 24, 43);
    t[39].add(ErrorLevel::H, 45, 15, 10);

    t[40] = VersionInfo(3706, {6, 30, 58, 86, 114, 142, 170});
    t[40].add(ErrorLevel::L, 148, 118, 19);
    t[40].add(ErrorLevel::M, 75, 47, 18);
    t[40].add(ErrorLevel::Q, 54, 24, 34);
    t[40].add(ErrorLevel::H, 45, 15, 20);

    return t;
}

}  // namespace

const std::array<VersionInfo, QrCode::MAX_VERSION + 1>& QrCode::VERSION_INFO() {
    static const AT table = buildVersionInfoTable();
    return table;
}

void QrCode::reset() {
    threshCorner = 0;
    threshDown = 0;
    threshRight = 0;
    threshDownRight = 0;
    version = -1;
    error = ErrorLevel::L;
    mask = &QrCodeMaskPattern::M111();
    mode = Mode::UNKNOWN;
    failureCause = Failure::NONE;
    byteEncoding.clear();
    rawbits.clear();
    corrected.clear();
    message.clear();
    bitsTransposed = false;
    totalBitErrors = 0;
    alignment.clear();
}

}  // namespace boofcv_qr
