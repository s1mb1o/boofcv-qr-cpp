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

# ---------------------------------------------------------------------------
# Core library — populated stage-by-stage as the port progresses (see
# CLAUDE.md "Porting order"). Step 1 (Galois) only.
# ---------------------------------------------------------------------------
add_library(boofcv_qr STATIC
    src/galois/galois.cpp
)

target_include_directories(boofcv_qr
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(boofcv_qr PUBLIC cxx_std_17)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(boofcv_qr PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
    )
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
    )
    target_link_libraries(boofcv_qr_tests
        PRIVATE
            boofcv_qr
            GTest::gtest_main
    )
    target_compile_features(boofcv_qr_tests PRIVATE cxx_std_17)

    include(GoogleTest)
    gtest_discover_tests(boofcv_qr_tests)
endif()
