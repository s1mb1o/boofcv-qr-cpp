// Port of boofcv.struct.PackedSetsPoint2D_I32 (BoofCV v1.3.0).
//
// Compact storage for a set of point sets. Each int[]-block of length
// `blockLength` stores up to `blockLength/2` (x,y) pairs; sets index
// into the global pair sequence by (block, start, length). Designed
// to minimise GC churn — the C++ port mirrors the shape verbatim so
// that pixel-for-pixel parity with the Java contour tracer is direct.
//
// Per CLAUDE.md type mappings:
//   int[] block          -> std::vector<int32_t> (one per block)
//   DogArray<int[]>      -> std::vector<std::vector<int32_t>>
//   DogArray<BlockIndexLength> -> std::vector<BlockIndexLength>
//   Point2D_I32          -> cv::Point2i
//
// Only the methods exercised by `LinearContourLabelChang2004` and
// `TestLinearContourLabelChang2004` are exposed:
//   reset, grow, removeTail, addPointToTail, size, sizeOfSet,
//   sizeOfTail, appendSetTo, createIterator + SetIterator::{setup, hasNext, next}.
// Unused Java conveniences (`totalPoints`, `getSet`, `writeOverSet`,
// `setToStart`) are intentionally omitted — plumbing-adjacent per
// CLAUDE.md, not needed for the algorithmic core or the JUnit-mirror
// tests.

#ifndef BOOFCV_QR_BINARY_PACKED_SETS_POINT2D_I32_HPP
#define BOOFCV_QR_BINARY_PACKED_SETS_POINT2D_I32_HPP

#include <opencv2/core.hpp>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace boofcv_qr {

// Mirror of the Java inner record `BlockIndexLength`.
struct BlockIndexLength {
    int32_t block = 0;
    int32_t start = 0;
    int32_t length = 0;
};

class PackedSetsPoint2D_I32 {
public:
    // Configures the storage. Mirrors the Java ctor: block length is
    // rounded up to even so that each block stores a whole number of
    // (x,y) pairs.
    explicit PackedSetsPoint2D_I32(int32_t blockLength) {
        if (blockLength < 2)
            throw std::invalid_argument("Block length must be more than 2");
        this->blockLength = blockLength + (blockLength % 2);
        blocks.emplace_back();
        blocks.back().resize(static_cast<std::size_t>(this->blockLength));
    }

    PackedSetsPoint2D_I32() : PackedSetsPoint2D_I32(2000) {}

    // Discards all previously stored points but does not free memory.
    void reset() {
        tailBlockSize = 0;
        activeBlocks = 1;
        sets.clear();
    }

    void releaseMemory() {
        std::vector<std::vector<int32_t>> freshBlocks;
        freshBlocks.emplace_back(static_cast<std::size_t>(blockLength));
        blocks.swap(freshBlocks);
        std::vector<BlockIndexLength>().swap(sets);
        tailBlockSize = 0;
        activeBlocks = 1;
    }

    // Adds a new (empty) point set to the end.
    void grow() {
        if (tailBlockSize >= blockLength) {
            tailBlockSize = 0;
            if (activeBlocks == static_cast<int32_t>(blocks.size())) {
                blocks.emplace_back();
                blocks.back().resize(static_cast<std::size_t>(blockLength));
            }
            activeBlocks++;
        }

        BlockIndexLength s;
        s.block = activeBlocks - 1;
        s.start = tailBlockSize;
        s.length = 0;
        sets.push_back(s);
    }

    // Removes the current point set from the end.
    void removeTail() {
        BlockIndexLength& tail = sets.back();
        while (activeBlocks - 1 != tail.block)
            activeBlocks--;
        tailBlockSize = tail.start;
        sets.pop_back();
    }

    // Adds a point to the tail point set.
    void addPointToTail(int32_t x, int32_t y) {
        if (sets.empty()) {
            grow();
        }
        BlockIndexLength& tail = sets.back();
        int32_t index = tail.start + tail.length * 2;

        int32_t blockIndex = tail.block + index / blockLength;
        std::vector<int32_t>* block;
        if (blockIndex == activeBlocks) {
            tailBlockSize = 0;
            if (activeBlocks == static_cast<int32_t>(blocks.size())) {
                blocks.emplace_back();
                blocks.back().resize(static_cast<std::size_t>(blockLength));
            }
            block = &blocks[static_cast<std::size_t>(activeBlocks)];
            activeBlocks++;
        } else {
            block = &blocks[static_cast<std::size_t>(blockIndex)];
        }
        tailBlockSize += 2;
        index %= blockLength;

        (*block)[static_cast<std::size_t>(index)] = x;
        (*block)[static_cast<std::size_t>(index + 1)] = y;
        tail.length += 1;
    }

    // Number of point sets.
    int32_t size() const { return static_cast<int32_t>(sets.size()); }

    // Size/length of a point set.
    int32_t sizeOfSet(int32_t which) const {
        return sets[static_cast<std::size_t>(which)].length;
    }

    // Returns the size of the set at the tail, or 0 if no tail exists.
    int32_t sizeOfTail() const {
        return sets.empty() ? 0 : sets.back().length;
    }

    void appendSetTo(int32_t which, std::vector<cv::Point2i>& output) const {
        const BlockIndexLength& set = sets[static_cast<std::size_t>(which)];
        int32_t remaining = set.length;
        int32_t blockIndex = set.block;
        int32_t index = set.start;

        while (remaining > 0) {
            const std::vector<int32_t>& block =
                blocks[static_cast<std::size_t>(blockIndex)];
            int32_t pointsInBlock = std::min(remaining, (blockLength - index) / 2);
            for (int32_t i = 0; i < pointsInBlock; i++) {
                output.emplace_back(block[static_cast<std::size_t>(index)],
                                    block[static_cast<std::size_t>(index + 1)]);
                index += 2;
            }
            remaining -= pointsInBlock;
            blockIndex++;
            index = 0;
        }
    }

    // Iterator over the points in a particular set without making a copy.
    // Mirrors PackedSetsPoint2D_I32.SetIterator. Only `setup`, `hasNext`,
    // `next` are exposed because that's all the JUnit test calls.
    class SetIterator {
    public:
        explicit SetIterator(const PackedSetsPoint2D_I32* parent) : parent_(parent) {}

        void setup(int32_t whichSet) {
            set_ = parent_->sets[static_cast<std::size_t>(whichSet)];
            pointIndex_ = 0;
        }

        bool hasNext() const { return pointIndex_ < set_.length; }

        cv::Point2i next() {
            int32_t index = set_.start + pointIndex_ * 2;
            int32_t blockIndex = set_.block + index / parent_->blockLength;
            index %= parent_->blockLength;

            const std::vector<int32_t>& block =
                parent_->blocks[static_cast<std::size_t>(blockIndex)];
            cv::Point2i p(block[static_cast<std::size_t>(index)],
                          block[static_cast<std::size_t>(index + 1)]);
            pointIndex_++;
            return p;
        }

    private:
        const PackedSetsPoint2D_I32* parent_;
        BlockIndexLength set_{};
        int32_t pointIndex_ = 0;
    };

    SetIterator createIterator() const { return SetIterator(this); }

private:
    // maximum number of elements that can be in a block (always even).
    int32_t blockLength = 0;
    // arrays which store the points.
    std::vector<std::vector<int32_t>> blocks;
    // number of allocated blocks currently participating in the logical tail.
    int32_t activeBlocks = 1;
    // describes where the data for each set is stored.
    std::vector<BlockIndexLength> sets;
    // length/size of the last block.
    int32_t tailBlockSize = 0;

    friend class SetIterator;
};

}  // namespace boofcv_qr

#endif  // BOOFCV_QR_BINARY_PACKED_SETS_POINT2D_I32_HPP
