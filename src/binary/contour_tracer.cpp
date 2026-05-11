// Port of boofcv.alg.filter.binary.ContourTracer (BoofCV v1.3.0).
//
// Verbatim per CLAUDE.md "Verbatim vs idiomize" — this is algorithmic
// core. Loop structure, variable names, and unrolled `searchOne8`
// mirror Java line-for-line. The only deliberate idiomization is C++
// row-pointer access through `cv::Mat::data` (one-time cache in
// setInputs) rather than Java's `byte[] data` field reach-through.

#include "boofcv_qr/binary/contour_tracer.hpp"

#include <stdexcept>

namespace boofcv_qr {

namespace {

void setOffsets8(std::array<int32_t, 8>& offsets, int32_t stride) {
    int32_t s = stride;
    offsets[0] = 1;      // x =  1 y =  0
    offsets[1] = 1 + s;  // x =  1 y =  1
    offsets[2] = s;      // x =  0 y =  1
    offsets[3] = -1 + s; // x = -1 y =  1
    offsets[4] = -1;     // x = -1 y =  0
    offsets[5] = -1 - s; // x = -1 y = -1
    offsets[6] = -s;     // x =  0 y = -1
    offsets[7] = 1 - s;  // x =  1 y = -1
}

void setOffsets4(std::array<int32_t, 8>& offsets, int32_t stride) {
    int32_t s = stride;
    offsets[0] = 1;   // x =  1 y =  0
    offsets[1] = s;   // x =  0 y =  1
    offsets[2] = -1;  // x = -1 y =  0
    offsets[3] = -s;  // x =  0 y = -1
}

void setCoordinateOffsets8(std::array<int32_t, 8>& offsetX,
                           std::array<int32_t, 8>& offsetY) {
    offsetX[0] =  1; offsetY[0] =  0;
    offsetX[1] =  1; offsetY[1] =  1;
    offsetX[2] =  0; offsetY[2] =  1;
    offsetX[3] = -1; offsetY[3] =  1;
    offsetX[4] = -1; offsetY[4] =  0;
    offsetX[5] = -1; offsetY[5] = -1;
    offsetX[6] =  0; offsetY[6] = -1;
    offsetX[7] =  1; offsetY[7] = -1;
}

void setCoordinateOffsets4(std::array<int32_t, 8>& offsetX,
                           std::array<int32_t, 8>& offsetY) {
    offsetX[0] =  1; offsetY[0] =  0;
    offsetX[1] =  0; offsetY[1] =  1;
    offsetX[2] = -1; offsetY[2] =  0;
    offsetX[3] =  0; offsetY[3] = -1;
}

}  // namespace

ContourTracer::ContourTracer(ConnectRule rule_)
    : rule(rule_),
      ruleN(rule_ == ConnectRule::EIGHT ? 8 : 4) {

    if (ConnectRule::EIGHT == rule) {
        // start the next search +2 away from the square it came from
        // the square it came from is the opposite from the previous 'dir'
        for (int32_t i = 0; i < 8; i++)
            nextDirection[static_cast<std::size_t>(i)] = ((i + 4) % 8 + 2) % 8;
        setCoordinateOffsets8(offsetsX, offsetsY);
    } else if (ConnectRule::FOUR == rule) {
        for (int32_t i = 0; i < 4; i++)
            nextDirection[static_cast<std::size_t>(i)] = ((i + 2) % 4 + 1) % 4;
        setCoordinateOffsets4(offsetsX, offsetsY);
    } else {
        throw std::invalid_argument("Connectivity rule must be 4 or 8");
    }
}

void ContourTracer::setInputs(cv::Mat& binary_, cv::Mat& labeled_,
                              PackedSetsPoint2D_I32& storagePoints_) {
    this->binary = &binary_;
    this->labeled = &labeled_;
    this->storagePoints = &storagePoints_;

    // Cache row-pointer base + stride. `cv::Mat::step1()` gives the
    // number of elements per row; for CV_8UC1 that equals the byte
    // stride, for CV_32SC1 it equals the 32-bit-element stride.
    binaryData = binary_.data;
    binaryStride = static_cast<int32_t>(binary_.step1());
    binaryStartIndex = 0;

    labeledData = reinterpret_cast<int32_t*>(labeled_.data);
    labeledStride = static_cast<int32_t>(labeled_.step1());
    labeledStartIndex = 0;

    if (rule == ConnectRule::EIGHT) {
        setOffsets8(offsetsBinary, binaryStride);
        setOffsets8(offsetsLabeled, labeledStride);
    } else {
        setOffsets4(offsetsBinary, binaryStride);
        setOffsets4(offsetsLabeled, labeledStride);
    }
}

void ContourTracer::trace(int32_t label_, int32_t initialX, int32_t initialY,
                          bool external) {
    int32_t initialDir;
    if (rule == ConnectRule::EIGHT)
        initialDir = external ? 7 : 3;
    else
        initialDir = external ? 0 : 2;

    this->label = label_;
    this->dir = initialDir;
    x = initialX;
    y = initialY;

    // index of pixels in the image array
    // binary has a 1 pixel border which labeled lacks, hence the -1,-1 for labeled
    indexBinary = binaryStartIndex + y * binaryStride + x;
    indexLabel = labeledStartIndex + (y - 1) * labeledStride + (x - 1);
    add(x, y);

    // find the next one pixel. handle case where its an isolated point
    if (!searchOne()) {
        return;
    } else {
        initialDir = dir;
        moveToNext();
        dir = nextDirection[static_cast<std::size_t>(dir)];
    }

    for (; ; ) {
        // search in clockwise direction around the current pixel for next black pixel
        searchOne();
        if (x == initialX && y == initialY && dir == initialDir) {
            // returned to the initial state again. search is finished
            return;
        } else {
            add(x, y);
            moveToNext();
            dir = nextDirection[static_cast<std::size_t>(dir)];
        }
    }
}

bool ContourTracer::searchOne() {
    // Unrolling here results in about a 10% speed up (per Java comment).
    if (ruleN == 4)
        return searchOne4();
    else
        return searchOne8();
}

bool ContourTracer::searchOne4() {
    if (checkOne(indexBinary + offsetsBinary[static_cast<std::size_t>(dir)]))
        return true;
    dir = (dir + 1) & 3;
    if (checkOne(indexBinary + offsetsBinary[static_cast<std::size_t>(dir)]))
        return true;
    dir = (dir + 1) & 3;
    if (checkOne(indexBinary + offsetsBinary[static_cast<std::size_t>(dir)]))
        return true;
    dir = (dir + 1) & 3;
    if (checkOne(indexBinary + offsetsBinary[static_cast<std::size_t>(dir)]))
        return true;
    dir = (dir + 1) & 3;
    return false;
}

bool ContourTracer::searchOne8() {
    if (checkOne(indexBinary + offsetsBinary[static_cast<std::size_t>(dir)]))
        return true;
    dir = (dir + 1) & 7;
    if (checkOne(indexBinary + offsetsBinary[static_cast<std::size_t>(dir)]))
        return true;
    dir = (dir + 1) & 7;
    if (checkOne(indexBinary + offsetsBinary[static_cast<std::size_t>(dir)]))
        return true;
    dir = (dir + 1) & 7;
    if (checkOne(indexBinary + offsetsBinary[static_cast<std::size_t>(dir)]))
        return true;
    dir = (dir + 1) & 7;
    if (checkOne(indexBinary + offsetsBinary[static_cast<std::size_t>(dir)]))
        return true;
    dir = (dir + 1) & 7;
    if (checkOne(indexBinary + offsetsBinary[static_cast<std::size_t>(dir)]))
        return true;
    dir = (dir + 1) & 7;
    if (checkOne(indexBinary + offsetsBinary[static_cast<std::size_t>(dir)]))
        return true;
    dir = (dir + 1) & 7;
    if (checkOne(indexBinary + offsetsBinary[static_cast<std::size_t>(dir)]))
        return true;
    dir = (dir + 1) & 7;
    return false;
}

bool ContourTracer::checkOne(int32_t index) {
    // Java stores `-1` (signed byte) into already-searched cells; C++
    // uses `0xFF` (same bit pattern when reinterpreted as unsigned).
    // The downstream test `data[index] == 1` is bit-identical between
    // the two; we never do arithmetic or signed comparisons on this
    // marker, so the choice of unsigned storage is safe.
    if (binaryData[index] == 1) {
        return true;
    } else {
        // mark this pixel with a not-one value so it isn't searched again
        binaryData[index] = static_cast<uint8_t>(0xFF);
        return false;
    }
}

void ContourTracer::moveToNext() {
    // move to the next pixel using the precomputed pixel index offsets
    indexBinary += offsetsBinary[static_cast<std::size_t>(dir)];
    indexLabel += offsetsLabeled[static_cast<std::size_t>(dir)];
    x += offsetsX[static_cast<std::size_t>(dir)];
    y += offsetsY[static_cast<std::size_t>(dir)];
}

void ContourTracer::add(int32_t x_, int32_t y_) {
    labeledData[indexLabel] = label;
    if (storagePoints->sizeOfTail() < maxContourSize) {
        storagePoints->addPointToTail(x_ - 1, y_ - 1);
    }
}

}  // namespace boofcv_qr
