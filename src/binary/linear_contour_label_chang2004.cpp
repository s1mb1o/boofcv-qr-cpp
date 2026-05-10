// Port of boofcv.alg.filter.binary.LinearContourLabelChang2004
// (BoofCV v1.3.0).
//
// Verbatim per CLAUDE.md "Verbatim vs idiomize" — this is the
// algorithmic core. Loop structure, variable names, and the 3-step
// dispatch in the main scan-line loop mirror Java line-for-line. The
// only deliberate idiomization is C++ row-pointer access via the
// cached `binaryData`/`labeledData` pointers (the Java code reaches
// into `binary.data` / `labeled.data` directly).
//
// See `src/binary/linear_contour_label_chang2004.md` for the
// algorithm explanation.

#include "boofcv_qr/binary/linear_contour_label_chang2004.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace boofcv_qr {

namespace {

// Mirror of `ConfigLength.computeNegMaxI(totalLength)` — returns
// INT32_MAX on negative thresholds rather than throwing.
int32_t computeNegMaxI(const ConfigLength& cl, double totalLength) {
    double size = cl.compute(totalLength);
    if (size >= 0.0)
        return static_cast<int32_t>(std::lround(size));
    else
        return std::numeric_limits<int32_t>::max();
}

// Mirror of `ImageMiscOps.fillBorder(GrayU8 image, 0, 1)` for a single-
// channel CV_8UC1 image. Sets the outermost 1-pixel ring to zero.
void fillBorder1(cv::Mat& image, uint8_t value) {
    int32_t w = image.cols;
    int32_t h = image.rows;
    if (w == 0 || h == 0) return;
    image.row(0).setTo(value);
    image.row(h - 1).setTo(value);
    image.col(0).setTo(value);
    image.col(w - 1).setTo(value);
}

}  // namespace

LinearContourLabelChang2004::LinearContourLabelChang2004(ConnectRule rule)
    : tracer(std::make_unique<ContourTracer>(rule)) {}

void LinearContourLabelChang2004::process(const cv::Mat& binaryIn,
                                          cv::Mat& labeled) {
    // initialize data structures
    if (labeled.rows != binaryIn.rows || labeled.cols != binaryIn.cols ||
        labeled.type() != CV_32SC1) {
        labeled.create(binaryIn.rows, binaryIn.cols, CV_32SC1);
    }
    double diag = std::sqrt(static_cast<double>(binaryIn.cols) * binaryIn.rows);
    minContourLengthPixels = computeNegMaxI(minContourLength, diag);
    maxContourLengthPixels = computeNegMaxI(maxContourLength, diag);

    // ensure that the image border pixels are filled with zero by enlarging the image
    if (border.cols != binaryIn.cols + 2 || border.rows != binaryIn.rows + 2 ||
        border.type() != CV_8UC1) {
        border.create(binaryIn.rows + 2, binaryIn.cols + 2, CV_8UC1);
        fillBorder1(border, 0);
    }
    // border.subimage(1, 1, border.width - 1, border.height - 1, null).setTo(binary);
    binaryIn.copyTo(border(cv::Rect(1, 1, binaryIn.cols, binaryIn.rows)));

    // labeled image must initially be filled with zeros
    labeled.setTo(cv::Scalar(0));

    cv::Mat& binary = border;
    packedPoints.reset();
    contours.clear();
    tracer->setInputs(binary, labeled, packedPoints);

    // Cache row-pointer + stride for the scan-line loop and step3
    // lookups. binary is the 1-pixel-bordered buffer; labeled is the
    // raw input-sized buffer.
    uint8_t* binaryData = binary.data;
    int32_t binaryStride = static_cast<int32_t>(binary.step1());
    int32_t* labeledData = reinterpret_cast<int32_t*>(labeled.data);
    int32_t labeledStride = static_cast<int32_t>(labeled.step1());

    // Outside border is all zeros so it can be ignored
    int32_t endY = binary.rows - 1;
    int32_t enxX = binary.cols - 1;  // (sic: Java spells it "enxX")
    for (y = 1; y < endY; y++) {
        indexIn = /*binary.startIndex*/ 0 + y * binaryStride + 1;
        indexOut = /*labeled.startIndex*/ 0 + (y - 1) * labeledStride;

        x = 1;
        int32_t delta = scanForOne(binaryData, indexIn, indexIn + enxX - x) - indexIn;
        x += delta;
        indexIn += delta;
        indexOut += delta;
        while (x < enxX) {
            int32_t label = labeledData[indexOut];
            bool handled = false;
            if (label == 0 && binaryData[indexIn - binaryStride] != 1) {
                handleStep1();
                handled = true;
                label = static_cast<int32_t>(contours.size());
            }
            // could be an external and internal contour
            if (binaryData[indexIn + binaryStride] == 0) {
                handleStep2(labeled, label);
                handled = true;
            }
            if (!handled) {
                // Step 3: Must not be part of the contour but an inner pixel and the pixel to the left must be
                // labeled
                if (labeledData[indexOut] == 0)
                    labeledData[indexOut] = labeledData[indexOut - 1];
            }

            delta = scanForOne(binaryData, indexIn + 1, indexIn + enxX - x) - indexIn;
            x += delta;
            indexIn += delta;
            indexOut += delta;
        }
    }
}

int32_t LinearContourLabelChang2004::scanForOne(const uint8_t* data,
                                                int32_t index, int32_t end) {
    while (index < end && data[index] != 1) {
        index++;
    }
    return index;
}

void LinearContourLabelChang2004::handleStep1() {
    contours.emplace_back();
    ContourPacked& c = contours.back();
    c.reset();
    c.id = static_cast<int32_t>(contours.size());
    tracer->setMaxContourSize(maxContourLengthPixels);
    // save the set index for this contour and declare memory for it
    c.externalIndex = packedPoints.size();
    packedPoints.grow();
    c.internalIndexes.clear();
    tracer->trace(static_cast<int32_t>(contours.size()), x, y, true);

    // Keep track that this was a contour, but free up all the points used in defining it
    if (packedPoints.sizeOfTail() >= maxContourLengthPixels ||
        packedPoints.sizeOfTail() < minContourLengthPixels) {
        packedPoints.removeTail();
        packedPoints.grow();
    }
}

void LinearContourLabelChang2004::handleStep2(cv::Mat& labeled, int32_t label) {
    // if the blob is not labeled and in this state it cannot be against the left side of the image
    if (label == 0) {
        int32_t* labeledData = reinterpret_cast<int32_t*>(labeled.data);
        label = labeledData[indexOut - 1];
    }

    ContourPacked& c = contours[static_cast<std::size_t>(label - 1)];
    c.internalIndexes.push_back(packedPoints.size());
    packedPoints.grow();
    tracer->setMaxContourSize(saveInternalContours ? maxContourLengthPixels : 0);
    tracer->trace(label, x, y, false);

    // See if the inner contour exceeded the maximum or minimum size. If so free its points
    if (packedPoints.sizeOfTail() >= maxContourLengthPixels ||
        packedPoints.sizeOfTail() < minContourLengthPixels) {
        packedPoints.removeTail();
        packedPoints.grow();
    }
}

void LinearContourLabelChang2004::setConnectRule(ConnectRule rule) {
    if (rule != tracer->getConnectRule())
        tracer = std::make_unique<ContourTracer>(rule);
}

}  // namespace boofcv_qr
