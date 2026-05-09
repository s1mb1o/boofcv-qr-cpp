// Port of:
//   boofcv.alg.fiducial.qrcode.PackedBits (interface)
//   boofcv.alg.fiducial.qrcode.PackedBits8 (concrete)
// Upstream: BoofCV v1.3.0.
//
// PackedBits32 is intentionally skipped — it is not used by the QR decode
// path. Algorithm description: src/decoder/packed_bits.md.

#ifndef BOOFCV_QR_PACKED_BITS_HPP
#define BOOFCV_QR_PACKED_BITS_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace boofcv_qr {

// Mirrors PackedBits — a stream of arbitrary-length bits stored in some
// underlying word array. We don't use this as a virtual base in the C++
// port because every consumer already knows it has PackedBits8; the
// interface is preserved here for cross-reference.

// Mirrors PackedBits8: bits packed into a byte array, little-endian within
// each byte (bit 0 of data[0] is the first bit). Public mutable fields
// (data, size) match the Java surface so the codec utils can splice in
// pre-built byte buffers via PackedBits8::wrap.
class PackedBits8 {
public:
    PackedBits8() : data(1, 0), size(0) {}
    explicit PackedBits8(int32_t totalBits) : data(1, 0), size(0) {
        resize(totalBits);
    }

    // Wrap an existing byte buffer; the caller keeps ownership of the bytes
    // (we copy them in here for safety, unlike the Java original which
    // aliases the array). Caller-owned aliasing is rare in this codebase
    // and copying makes the surface easier to reason about; if a caller
    // genuinely needs zero-copy aliasing we can revisit.
    static PackedBits8 wrap(const std::vector<std::uint8_t>& src,
                            int32_t numberOfBits) {
        PackedBits8 a;
        a.data = src;
        a.size = numberOfBits;
        return a;
    }

    void setTo(const PackedBits8& src) {
        resize(src.size);
        std::copy(src.data.begin(),
                  src.data.begin() + static_cast<std::ptrdiff_t>(src.arrayLength()),
                  data.begin());
    }

    bool isIdentical(const PackedBits8& other) const {
        if (size != other.size)
            return false;
        int32_t numBytes = size / 8 + (size % 8 == 0 ? 0 : 1);
        for (int32_t i = 0; i < numBytes; i++) {
            if (data[static_cast<std::size_t>(i)] !=
                other.data[static_cast<std::size_t>(i)])
                return false;
        }
        return true;
    }

    int32_t get(int32_t which) const {
        int32_t index = which / 8;
        int32_t offset = which % 8;
        return (data[static_cast<std::size_t>(index)] & (1 << offset)) >> offset;
    }

    void set(int32_t which, int32_t value) {
        int32_t index = which / 8;
        int32_t offset = which % 8;
        std::uint8_t& cell = data[static_cast<std::size_t>(index)];
        cell = static_cast<std::uint8_t>(
            cell ^ ((-value ^ cell) & (1 << offset)));
    }

    /**
     * Appends bytes from a buffer, fractions of a byte allowed.
     */
    void append(const std::vector<std::uint8_t>& bytes,
                int32_t numberOfBits, bool swapOrder) {
        // pre-declare required memory. TODO remove this hack in the future.
        int32_t oldSize = size;
        growArray(numberOfBits, true);
        size = oldSize;

        int32_t numBytes = numberOfBits / 8;
        for (int32_t i = 0; i < numBytes; i++) {
            append(static_cast<int32_t>(bytes[static_cast<std::size_t>(i)]), 8,
                   swapOrder);
        }
        int32_t remaining = numberOfBits - numBytes * 8;
        if (remaining == 0)
            return;
        append(static_cast<int32_t>(bytes[static_cast<std::size_t>(numBytes)]),
               remaining, swapOrder);
    }

    /**
     * Append bits to the end. `bits` holds the relevant bits at the front.
     * If swapOrder is true, the first bit in `bits` becomes the last bit
     * appended.
     */
    void append(int32_t bits, int32_t numberOfBits, bool swapOrder) {
        if (numberOfBits > 32)
            throw std::invalid_argument("Number of bits exceeds the size of bits");
        int32_t indexTail = size;
        growArray(numberOfBits, true);

        if (swapOrder) {
            for (int32_t i = 0; i < numberOfBits; i++) {
                set(indexTail + i, (bits >> i) & 1);
            }
        } else {
            for (int32_t i = 0; i < numberOfBits; i++) {
                set(indexTail + numberOfBits - i - 1, (bits >> i) & 1);
            }
        }
    }

    /**
     * Append another PackedBits8.
     */
    void append(const PackedBits8& bits, int32_t numberOfBits) {
        if (numberOfBits > bits.size)
            throw std::invalid_argument("numberOfBits must be <= bits.size");

        int32_t numWords = bits.size / 8;
        for (int32_t i = 0; i < numWords; i++) {
            append(static_cast<int32_t>(bits.data[static_cast<std::size_t>(i)]),
                   8, true);
        }
        int32_t remaining = bits.size - numWords * 8;
        if (remaining == 0)
            return;
        int32_t tail = bits.read(numWords * 8, remaining, true);
        append(tail, remaining, false);
    }

    /**
     * Append bits encoded as a binary string, e.g. "100010010011".
     */
    PackedBits8& append(const std::string& text) {
        std::size_t location;
        for (location = 0; location + 8 < text.size(); location += 8) {
            int32_t value = 0;
            for (std::size_t i = 0; i < 8; i++) {
                if (text[location + i] == '0')
                    continue;
                value |= 1 << i;
            }
            append(value, 8, true);
        }
        while (location < text.size()) {
            int32_t value = text[location++] == '0' ? 0 : 1;
            append(value, 1, true);
        }
        return *this;
    }

    /**
     * Read bits from the array.
     */
    int32_t read(int32_t location, int32_t length, bool swapOrder) const {
        if (length < 0 || length > 32)
            throw std::invalid_argument("Length can't exceed 32");
        if (location + length > size)
            throw std::invalid_argument(
                "Attempting to read past the end. length=" +
                std::to_string(length) +
                " remaining=" + std::to_string(size - location));

        // TODO speed up by reading in byte chunks.
        int32_t output = 0;
        if (swapOrder) {
            for (int32_t i = 0; i < length; i++) {
                output |= get(location + i) << (length - i - 1);
            }
        } else {
            for (int32_t i = 0; i < length; i++) {
                output |= get(location + i) << i;
            }
        }
        return output;
    }

    int32_t getArray(int32_t index) const {
        return data[static_cast<std::size_t>(index)];
    }

    void resize(int32_t totalBits) {
        size = totalBits;
        int32_t N = arrayLength();
        if (static_cast<int32_t>(data.size()) < N) {
            data.assign(static_cast<std::size_t>(N), 0);
        }
    }

    /**
     * Increase the data array so it can store `amountBits` more.
     * If saveValue is true the existing bytes are preserved.
     */
    void growArray(int32_t amountBits, bool saveValue) {
        size = size + amountBits;
        int32_t N = size / 8 + (size % 8 == 0 ? 0 : 1);

        if (N > static_cast<int32_t>(data.size())) {
            int32_t extra = std::min(1024, N + 10);
            std::vector<std::uint8_t> tmp(static_cast<std::size_t>(N + extra), 0);
            if (saveValue) {
                std::copy(data.begin(), data.end(), tmp.begin());
            }
            data = std::move(tmp);
        }
    }

    void zero() {
        int32_t N = arrayLength();
        for (int32_t i = 0; i < N; i++)
            data[static_cast<std::size_t>(i)] = 0;
    }

    int32_t length() const { return size; }

    int32_t arrayLength() const {
        if ((size % 8) == 0)
            return size / 8;
        return size / 8 + 1;
    }

    static int32_t elementBits() { return 8; }

    // Public for cross-reference parity with the Java original. data has
    // capacity >= arrayLength(); read past `size` bits is undefined.
    std::vector<std::uint8_t> data;
    int32_t size;
};

}  // namespace boofcv_qr

#endif  // BOOFCV_QR_PACKED_BITS_HPP
