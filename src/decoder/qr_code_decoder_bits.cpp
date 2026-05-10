// Port of QrCodeDecoderBits. Verbatim per CLAUDE.md — loop structure
// and variable names match the Java original line-by-line.

#include "boofcv_qr/qr_code_decoder_bits.hpp"
#include "boofcv_qr/eci_encoding.hpp"

#include <cstring>
#include <stdexcept>
#include <utility>

namespace boofcv_qr {

QrCodeDecoderBits::QrCodeDecoderBits(std::optional<std::string> forceEncoding,
                                     std::string defaultEncoding)
    : utils(std::move(forceEncoding), std::move(defaultEncoding)),
      rscodes(8, 0b100011101, 0) {}

bool QrCodeDecoderBits::applyErrorCorrection(QrCode& qr) {
    // Sanity-check inputs. A public stage shouldn't UB on garbage; mirror
    // Java's failure path (which would NPE / IOOB and propagate up).
    if (qr.version < 1 || qr.version > QrCode::MAX_VERSION) {
        qr.failureCause = Failure::VERSION;
        return false;
    }
    const VersionInfo& info =
        QrCode::VERSION_INFO()[static_cast<std::size_t>(qr.version)];
    if (static_cast<int32_t>(qr.rawbits.size()) < info.codewords) {
        qr.failureCause = Failure::READING_BITS;
        return false;
    }
    auto blockIt = info.levels.find(qr.error);
    if (blockIt == info.levels.end()) return false;
    const BlockInfo& block = blockIt->second;

    int32_t wordsBlockAllA = block.codewords;
    int32_t wordsBlockDataA = block.dataCodewords;
    int32_t wordsEcc = wordsBlockAllA - wordsBlockDataA;
    int32_t numBlocksA = block.blocks;

    int32_t wordsBlockAllB = wordsBlockAllA + 1;
    int32_t wordsBlockDataB = wordsBlockDataA + 1;
    int32_t numBlocksB = (info.codewords - wordsBlockAllA * numBlocksA) / wordsBlockAllB;

    int32_t totalBlocks = numBlocksA + numBlocksB;
    int32_t totalDataBytes = wordsBlockDataA * numBlocksA + wordsBlockDataB * numBlocksB;
    qr.corrected.assign(static_cast<std::size_t>(totalDataBytes), 0);

    ecc.assign(static_cast<std::size_t>(wordsEcc), 0);
    rscodes.generator(wordsEcc);

    // CLAUDE.md "Public API design" mandate: surface per-block status
    // and the byte offsets RS corrected. Pre-size + clear here so the
    // step-9 orchestrator can rely on these being populated whether or
    // not RS succeeds in every block.
    qr.blockStatus.assign(static_cast<std::size_t>(totalBlocks),
                          QrCode::BlockStatus::NOT_DECODED);
    qr.rsErrorLocations.clear();

    totalErrorBits = 0;
    if (!decodeBlocks(qr, wordsBlockDataA, numBlocksA, 0, 0, totalDataBytes, totalBlocks))
        return false;

    if (!decodeBlocks(qr, wordsBlockDataB, numBlocksB,
                      numBlocksA * wordsBlockDataA, numBlocksA, totalDataBytes,
                      totalBlocks))
        return false;

    qr.totalBitErrors = totalErrorBits;
    return true;
}

bool QrCodeDecoderBits::decodeBlocks(QrCode& qr, int32_t bytesInDataBlock,
                                     int32_t numberOfBlocks,
                                     int32_t bytesDataRead, int32_t offsetBlock,
                                     int32_t offsetEcc, int32_t stride) {
    message.assign(static_cast<std::size_t>(bytesInDataBlock), 0);

    for (int32_t idxBlock = 0; idxBlock < numberOfBlocks; idxBlock++) {
        copyFromRawData(qr.rawbits, message, ecc, offsetBlock + idxBlock,
                        stride, offsetEcc);

        QrCodeCodecBitsUtils::flipBits8(message, message.size());
        QrCodeCodecBitsUtils::flipBits8(ecc, ecc.size());

        int32_t globalBlockIndex = offsetBlock + idxBlock;
        if (!rscodes.correct(message, ecc)) {
            qr.blockStatus[static_cast<std::size_t>(globalBlockIndex)] =
                QrCode::BlockStatus::ERROR_CORRECTION_FAILED;
            return false;
        }
        int32_t blockErrors = rscodes.getTotalErrors();
        totalErrorBits += blockErrors;
        qr.blockStatus[static_cast<std::size_t>(globalBlockIndex)] =
            (blockErrors == 0) ? QrCode::BlockStatus::SUCCESS_NO_ERRORS
                               : QrCode::BlockStatus::SUCCESS;

        // Translate per-block error positions back to raw-bits byte
        // offsets so downstream consumers (multi-frame fusion,
        // known-prefix recovery) can re-decode without re-running RS.
        // RS reports positions in the concatenation [message | ecc];
        // the de-interleaved raw-bits index for codeword `j` in this
        // block is `j*stride + (offsetBlock + idxBlock)` for data
        // bytes and `(j - msg.size())*stride + (offsetBlock + idxBlock)
        // + offsetEcc` for ecc bytes.
        int32_t msgSize = static_cast<int32_t>(message.size());
        for (std::size_t k = 0; k < rscodes.errorLocations.size(); k++) {
            int32_t loc = rscodes.errorLocations[k];
            int32_t rawIdx;
            if (loc < msgSize) {
                rawIdx = loc * stride + offsetBlock + idxBlock;
            } else {
                rawIdx = (loc - msgSize) * stride + offsetBlock + idxBlock
                         + offsetEcc;
            }
            qr.rsErrorLocations.push_back(rawIdx);
        }

        QrCodeCodecBitsUtils::flipBits8(message, message.size());
        for (std::size_t i = 0; i < message.size(); i++)
            qr.corrected[static_cast<std::size_t>(bytesDataRead) + i] = message[i];
        bytesDataRead += static_cast<int32_t>(message.size());
    }
    return true;
}

void QrCodeDecoderBits::copyFromRawData(const std::vector<std::uint8_t>& input,
                                        std::vector<std::uint8_t>& msg,
                                        std::vector<std::uint8_t>& eccOut,
                                        int32_t offsetBlock, int32_t stride,
                                        int32_t offsetEcc) {
    for (std::size_t i = 0; i < msg.size(); i++) {
        msg[i] = input[static_cast<std::size_t>(
            static_cast<int32_t>(i) * stride + offsetBlock)];
    }
    for (std::size_t i = 0; i < eccOut.size(); i++) {
        eccOut[i] = input[static_cast<std::size_t>(
            static_cast<int32_t>(i) * stride + offsetBlock + offsetEcc)];
    }
}

bool QrCodeDecoderBits::decodeMessage(QrCode& qr) {
    encodingEci.reset();
    utils.workString.clear();

    try {
        int32_t location = decideMessageBits(qr);
        if (location < 0) return false;

        location = alignToBytes(location);
        int32_t lengthBytes = location / 8;

        qr.message = utils.workString;

        if (!ignorePaddingBytes && !checkPaddingBytes(qr, lengthBytes)) {
            qr.failureCause = Failure::READING_PADDING;
            return false;
        }
        return true;
    } catch (const std::exception&) {
        qr.failureCause = Failure::DECODING_MESSAGE;
        qr.message = utils.workString;
        return false;
    }
}

int32_t QrCodeDecoderBits::decideMessageBits(QrCode& qr) {
    qr.byteEncoding.clear();

    PackedBits8 bits = PackedBits8::wrap(
        qr.corrected, static_cast<int32_t>(qr.corrected.size()) * 8);

    int32_t location = 0;
    while (location + 4 <= bits.size) {
        int32_t modeBits = bits.read(location, 4, true);
        location += 4;
        if (modeBits == 0) {  // escape indicator
            break;
        }
        Mode mode = Mode_lookup(modeBits);

        qr.mode = updateModeLogic(qr.mode, mode);
        switch (mode) {
            case Mode::NUMERIC: location = decodeNumeric(qr, bits, location); break;
            case Mode::ALPHANUMERIC: location = decodeAlphanumeric(qr, bits, location); break;
            case Mode::BYTE: {
                location = decodeByte(qr, bits, location);
                if (qr.byteEncoding.empty()) {
                    qr.byteEncoding = utils.selectedByteEncoding;
                }
                break;
            }
            case Mode::KANJI: location = decodeKanji(qr, bits, location); break;
            case Mode::ECI: location = decodeEci(bits, location); break;
            case Mode::FNC1_FIRST:
            case Mode::FNC1_SECOND:
                // Not properly handled, but parsing continues.
                break;
            default:
                qr.failureCause = Failure::UNKNOWN_MODE;
                return -1;
        }

        if (location < 0) {
            qr.failureCause = utils.failureCause;
            return -1;
        }
    }
    return location;
}

Mode QrCodeDecoderBits::updateModeLogic(Mode current, Mode candidate) {
    if (current == candidate) return current;
    if (current == Mode::UNKNOWN) return candidate;
    return Mode::MIXED;
}

int32_t QrCodeDecoderBits::alignToBytes(int32_t lengthBits) {
    return lengthBits + (8 - lengthBits % 8) % 8;
}

bool QrCodeDecoderBits::checkPaddingBytes(const QrCode& qr,
                                          int32_t lengthBytes) const {
    bool a = true;

    for (std::size_t i = static_cast<std::size_t>(lengthBytes);
         i < qr.corrected.size(); i++) {
        std::uint8_t v = qr.corrected[i];
        if (a) {
            if (0b00110111 != v) return false;
        } else {
            if (0b10001000 != v) {
                // The pattern restarts at the beginning of a block.
                // Strict enforcement requires knowing block layout —
                // not worth the implementation cost.
                if (0b00110111 == v) {
                    a = true;
                } else {
                    return false;
                }
            }
        }
        a = !a;
    }
    return true;
}

int32_t QrCodeDecoderBits::decodeNumeric(const QrCode& qr,
                                         const PackedBits8& data,
                                         int32_t bitLocation) {
    int32_t lengthBits = getLengthBitsNumeric(qr.version);
    return utils.decodeNumeric(data, bitLocation, lengthBits);
}

int32_t QrCodeDecoderBits::decodeAlphanumeric(const QrCode& qr,
                                              const PackedBits8& data,
                                              int32_t bitLocation) {
    int32_t lengthBits = getLengthBitsAlphanumeric(qr.version);
    return utils.decodeAlphanumeric(data, bitLocation, lengthBits);
}

int32_t QrCodeDecoderBits::decodeByte(QrCode& qr, const PackedBits8& data,
                                      int32_t bitLocation) {
    int32_t lengthBits = getLengthBitsBytes(qr.version);
    utils.encodingEci = encodingEci;
    return utils.decodeByte(data, bitLocation, lengthBits);
}

int32_t QrCodeDecoderBits::decodeKanji(const QrCode& qr,
                                       const PackedBits8& data,
                                       int32_t bitLocation) {
    int32_t lengthBits = getLengthBitsKanji(qr.version);
    return utils.decodeKanji(data, bitLocation, lengthBits);
}

int32_t QrCodeDecoderBits::decodeEci(const PackedBits8& data,
                                     int32_t bitLocation) {
    // NOTE (BoofCV comment preserved): ECI is hard to test —
    // most encoders use UTF-8 by default, which already covers most chars.

    // number of 1 bits before first 0 defines number of additional codewords
    int32_t firstByte = data.read(bitLocation, 8, true);
    bitLocation += 8;

    int32_t numCodeWords = 1;
    while (numCodeWords <= 7 &&
           (firstByte & (1 << (7 - numCodeWords))) != 0) {
        numCodeWords++;
    }
    if (numCodeWords > 7) {
        // All-ones first byte: malformed ECI prefix. Java would walk off
        // the end into UB shifts; we throw and let decodeMessage's
        // try/catch convert this into Failure::DECODING_MESSAGE.
        throw std::runtime_error("ECI: malformed numCodeWords prefix");
    }
    // strip the bits that indicate the number of code words
    if (numCodeWords > 1) {
        firstByte <<= numCodeWords - 1;
        firstByte = static_cast<int32_t>(
            static_cast<uint32_t>(firstByte) >> (numCodeWords - 1));
    }

    int32_t assignmentValue = firstByte;
    for (int32_t i = 1; i < numCodeWords; i++) {
        assignmentValue <<= 8;
        assignmentValue |= data.read(bitLocation, 8, true);
        bitLocation += 8;
    }

    encodingEci = EciEncoding::getEciCharacterSet(assignmentValue);
    return bitLocation;
}

// ----- length-bits-per-version table (mirrors QrCodeEncoder) -----

int32_t QrCodeDecoderBits::getLengthBitsNumeric(int32_t version) {
    if (version < 10) return 10;
    if (version < 27) return 12;
    return 14;
}
int32_t QrCodeDecoderBits::getLengthBitsAlphanumeric(int32_t version) {
    if (version < 10) return 9;
    if (version < 27) return 11;
    return 13;
}
int32_t QrCodeDecoderBits::getLengthBitsBytes(int32_t version) {
    if (version < 10) return 8;
    return 16;
}
int32_t QrCodeDecoderBits::getLengthBitsKanji(int32_t version) {
    if (version < 10) return 8;
    if (version < 27) return 10;
    return 12;
}

}  // namespace boofcv_qr
