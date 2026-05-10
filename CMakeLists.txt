cmake_minimum_required(VERSION 3.16)
project(qr-boofcv-cpp
    VERSION 0.1.0
    DESCRIPTION "C++17 port of BoofCV's QR code detection/decoding (boofcv.alg.fiducial.qrcode)"
    LANGUAGES CXX
)

# ---------------------------------------------------------------------------
# Toolchain — C++17 strictly per project CLAUDE.md.
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(BOOFCV_QR_BUILD_TESTS "Build the GoogleTest unit-test suite" ON)

# OpenCV becomes required at step 5 (perspective grid sampler) — provides
# cv::Matx33d, cv::Mat, cv::getPerspectiveTransform, cv::findHomography,
# cv::perspectiveTransform. Image I/O and contour processing come in
# later steps.
# Hint Homebrew's keg-only opencv4 path on macOS.
if(APPLE AND NOT DEFINED OpenCV_DIR)
    foreach(_p
        /opt/homebrew/lib/cmake/opencv4
        /opt/homebrew/Cellar/opencv/4.13.0_10/lib/cmake/opencv4
        /usr/local/lib/cmake/opencv4)
        if(EXISTS "${_p}")
            set(OpenCV_DIR "${_p}" CACHE PATH "OpenCV cmake config dir")
            break()
        endif()
    endforeach()
endif()
find_package(OpenCV REQUIRED COMPONENTS core calib3d imgproc imgcodecs)

# ---------------------------------------------------------------------------
# Core library — populated stage-by-stage as the port progresses (see
# CLAUDE.md "Porting order"). Step 1 (Galois) only.
# ---------------------------------------------------------------------------
add_library(boofcv_qr STATIC
    src/galois/galois.cpp
    src/galois/galois_table_ops.cpp
    src/reed_solomon/reed_solomon.cpp
    src/decoder/eci_encoding.cpp
    src/decoder/qr_codec_bits_utils.cpp
    src/decoder/qr_code.cpp
    src/decoder/qr_code_mask_pattern.cpp
    src/decoder/qr_code_polynomial_math.cpp
    src/decoder/qr_code_decoder_bits.cpp
    src/decoder/qr_code_decoder_image.cpp
    src/sampler/qr_code_codeword_locations.cpp
    src/sampler/qr_code_binary_grid_to_pixel.cpp
    src/sampler/qr_code_binary_grid_reader.cpp
    src/binary/threshold_block_otsu.cpp
    src/binary/contour_tracer.cpp
    src/binary/linear_contour_label_chang2004.cpp
    src/polygon/square_node.cpp
    src/polygon/square_graph.cpp
    src/polygon/polyline_split_merge.cpp
    src/polygon/detect_polygon_from_contour.cpp
    src/polygon/image_line_integral.cpp
    src/polygon/snap_to_line_edge.cpp
    src/polygon/refine_polygon_to_gray.cpp
    src/polygon/detect_polygon_binary_gray_refine.cpp
    src/finder/square_locator_pattern_detector_base.cpp
    src/finder/qr_code_position_pattern_detector.cpp
    src/finder/qr_code_position_pattern_graph_generator.cpp
    src/alignment/qr_code_alignment_pattern_locator.cpp
)

target_include_directories(boofcv_qr
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(boofcv_qr PUBLIC cxx_std_17)

target_link_libraries(boofcv_qr PUBLIC opencv_core opencv_calib3d opencv_imgproc)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(boofcv_qr PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
    )
endif()

# ---------------------------------------------------------------------------
# CLI binary — `qr_scan` runs the full detection pipeline on either a
# single image (stdout) or a directory of images (mirrors the
# `tools/java_reference/Baseline.java` output for `tests/regression/score.py`).
# Per CLAUDE.md the CLI may use <filesystem>; the core lib must not.
# ---------------------------------------------------------------------------
option(BOOFCV_QR_BUILD_CLI "Build the qr_scan CLI binary" ON)
if(BOOFCV_QR_BUILD_CLI)
    add_executable(qr_scan tools/cli/qr_scan.cpp)
    target_link_libraries(qr_scan PRIVATE
        boofcv_qr
        opencv_imgcodecs
    )
    target_compile_features(qr_scan PRIVATE cxx_std_17)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(qr_scan PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
        )
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(BOOFCV_QR_BUILD_TESTS)
    enable_testing()

    include(FetchContent)
    set(FETCHCONTENT_QUIET OFF)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
        URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(boofcv_qr_tests
        tests/unit/test_galois.cpp
        tests/unit/test_galois_table_ops.cpp
        tests/unit/test_reed_solomon.cpp
        tests/unit/test_packed_bits.cpp
        tests/unit/test_eci_encoding.cpp
        tests/unit/test_qr_codec_bits_utils.cpp
        tests/unit/test_qr_code.cpp
        tests/unit/test_qr_code_mask_pattern.cpp
        tests/unit/test_qr_code_polynomial_math.cpp
        tests/unit/test_qr_code_decoder_bits.cpp
        tests/unit/test_qr_code_codeword_locations.cpp
        tests/unit/test_qr_code_binary_grid_to_pixel.cpp
        tests/unit/test_threshold_block_otsu.cpp
        tests/unit/test_linear_contour_label_chang2004.cpp
        tests/unit/test_square_node.cpp
        tests/unit/test_square_graph.cpp
        tests/unit/test_polyline_split_merge.cpp
        tests/unit/test_detect_polygon_from_contour.cpp
        tests/unit/test_refine_polygon_to_gray.cpp
        tests/unit/test_square_locator_pattern_detector_base.cpp
        tests/unit/test_qr_code_position_pattern_detector.cpp
        tests/unit/test_qr_code_position_pattern_graph_generator.cpp
        tests/unit/test_qr_code_alignment_pattern_locator.cpp
        tests/unit/test_qr_code_decoder_image.cpp
    )
    target_link_libraries(boofcv_qr_tests
        PRIVATE
            boofcv_qr
            opencv_imgcodecs
            GTest::gtest_main
    )
    target_compile_features(boofcv_qr_tests PRIVATE cxx_std_17)
    # Step-9 orchestrator tests load PNG fixtures from the source tree.
    target_compile_definitions(boofcv_qr_tests PRIVATE
        BOOFCV_QR_FIXTURE_DIR_DEFINE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/qr"
    )

    include(GoogleTest)
    gtest_discover_tests(boofcv_qr_tests)
endif()
