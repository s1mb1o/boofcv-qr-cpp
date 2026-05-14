// Port of boofcv.alg.fiducial.qrcode.QrCodeDecoderBits (BoofCV v1.3.0).
//
// Orchestrator that consumes raw codewords from a successfully-located
// QR code and produces:
//   - corrected codewords (Reed-Solomon error correction per block)
//   - decoded message (mode-aware payload extraction)
// Each public step is a hook for downstream recovery pipelines per
// CLAUDE.md "Public API design".
//
// Algorithm description: src/decoder/qr_code_decoder_bits.md.

#ifndef BOOFCV_QR_QR_CODE_DECODER_BITS_HPP
#define BOOFCV_QR_QR_CODE_DECODER_BITS_HPP

#include "boofcv_qr/packed_bits.hpp"
#include "boofcv_qr/qr_code.hpp"
#include "boofcv_qr/qr_codec_bits_utils.hpp"
#include "boofcv_qr/reed_solomon.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace boofcv_qr {

class QrCodeDecoderBits {
public:
    QrCodeDecoderBits(std::optional<std::string> forceEncoding,
                      std::string defaultEncoding);

    // Reconstruct corrected codewords from `qr.rawbits` using the version
    // and ECC level already decoded into `qr`. Populates `qr.corrected`,
    // `qr.totalBitErrors`. Returns false if RS exceeded its capacity in
    // any block.
    bool applyErrorCorrection(QrCode& qr);

    // Decode the corrected codewords into a message (UTF-8 / Latin-1 /
    // raw bytes per byte-mode auto-detection). Populates `qr.message`,
    // `qr.byteEncoding`, `qr.mode`. Returns false on hard failure
    // (with `qr.failureCause` set).
    bool decodeMessage(QrCode& qr);

    // ---- Lower-level entry points (per CLAUDE.md "Public API design"). ----
    int32_t decodeEci(const PackedBits8& data, int32_t bitLocation);
    bool checkPaddingBytes(const QrCode& qr, int32_t lengthBytes) const;
    static int32_t alignToBytes(int32_t lengthBits);

    // Length-of-length-field bits for each mode at the given QR version.
    // Mirrors QrCodeEncoder.getLengthBits{Numeric, Alphanumeric, Bytes,
    // Kanji} — reproduced here so the decoder doesn't need the encoder.
    static int32_t getLengthBitsNumeric(int32_t version);
    static int32_t getLengthBitsAlphanumeric(int32_t version);
    static int32_t getLengthBitsBytes(int32_t version);
    static int32_t getLengthBitsKanji(int32_t version);

    void releaseScratch() {
        std::vector<std::uint8_t>().swap(message);
        std::vector<std::uint8_t>().swap(ecc);
        std::string().swap(utils.workString);
        std::string().swap(utils.selectedByteEncoding);
        utils.failureCause = Failure::NONE;
        utils.encodingEci.reset();
        encodingEci.reset();
        totalErrorBits = 0;
    }

    // Public mutable surface so tests / consumers can inspect intermediate
    // state. Mirrors Java's package-private fields.
    bool ignorePaddingBytes = false;
    int32_t totalErrorBits = 0;
    std::optional<std::string> encodingEci;
    QrCodeCodecBitsUtils utils;

private:
    int32_t decideMessageBits(QrCode& qr);
    static Mode updateModeLogic(Mode current, Mode candidate);

    int32_t decodeNumeric(const QrCode& qr, const PackedBits8& data,
                          int32_t bitLocation);
    int32_t decodeAlphanumeric(const QrCode& qr, const PackedBits8& data,
                               int32_t bitLocation);
    int32_t decodeByte(QrCode& qr, const PackedBits8& data, int32_t bitLocation);
    int32_t decodeKanji(const QrCode& qr, const PackedBits8& data,
                        int32_t bitLocation);

    bool decodeBlocks(QrCode& qr, int32_t bytesInDataBlock, int32_t numberOfBlocks,
                      int32_t bytesDataRead, int32_t offsetBlock,
                      int32_t offsetEcc, int32_t stride);
    void copyFromRawData(const std::vector<std::uint8_t>& input,
                         std::vector<std::uint8_t>& message,
                         std::vector<std::uint8_t>& ecc, int32_t offsetBlock,
                         int32_t stride, int32_t offsetEcc);

    ReedSolomonCodes_U8 rscodes;
    std::vector<std::uint8_t> message;
    std::vector<std::uint8_t> ecc;
};

}  // namespace boofcv_qr

#endif  // BOOFCV_QR_QR_CODE_DECODER_BITS_HPP
