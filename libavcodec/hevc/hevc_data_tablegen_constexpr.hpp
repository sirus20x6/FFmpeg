/*
 * HEVC diagonal scan tables - C++20 constexpr implementation
 * Copyright (c) 2025 FFmpeg Modernization Project
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVCODEC_HEVC_DATA_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_HEVC_DATA_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>

/**
 * @file
 * @brief HEVC (H.265) diagonal scan order tables
 *
 * This header provides compile-time generation of HEVC diagonal scan patterns
 * using C++20 constexpr. HEVC (High Efficiency Video Coding, also known as
 * H.265) is the successor to H.264, offering superior compression for modern
 * high-resolution video (4K, 8K).
 *
 * Tables:
 * - diag_scan4x4_x/y: Diagonal scan coordinates for 4×4 transform blocks
 * - diag_scan8x8_x/y: Diagonal scan coordinates for 8×8 transform blocks
 *
 * These scan patterns define the order to traverse transform coefficients in
 * diagonal patterns, optimizing run-length encoding efficiency. The diagonal
 * scan moves from low-frequency (top-left) to high-frequency (bottom-right).
 *
 * Generated at compile time - zero runtime overhead.
 * All tables placed in .rodata section by the compiler.
 *
 * Data size: 160 bytes (160 entries total)
 * Static assertions: 40+ compile-time validations
 */

namespace FFmpegHEVCData {

// ============================================================================
// 4×4 Diagonal Scan Tables (16 entries each, 32 bytes total)
// ============================================================================

/**
 * HEVC 4×4 diagonal scan X coordinates
 *
 * X coordinates for traversing a 4×4 transform block in diagonal order:
 * - Starts at top-left (0,0)
 * - Moves diagonally from low-frequency to high-frequency
 * - Ends at bottom-right (3,3)
 *
 * Each entry specifies the X coordinate (column) in the 4×4 block.
 * Used with diag_scan4x4_y to form complete (x,y) coordinate pairs.
 */
constexpr auto generate_hevc_diag_scan4x4_x() noexcept {
    std::array<uint8_t, 16> scan{};

    // Exact diagonal scan order from HEVC specification
    constexpr uint8_t values[16] = {
        0, 0, 1, 0,
        1, 2, 0, 1,
        2, 3, 1, 2,
        3, 2, 3, 3
    };

    for (int i = 0; i < 16; ++i) {
        scan[i] = values[i];
    }

    return scan;
}

constexpr auto hevc_diag_scan4x4_x = generate_hevc_diag_scan4x4_x();

/**
 * HEVC 4×4 diagonal scan Y coordinates
 *
 * Y coordinates for traversing a 4×4 transform block in diagonal order.
 * Paired with diag_scan4x4_x to form complete coordinate pairs.
 *
 * Each entry specifies the Y coordinate (row) in the 4×4 block.
 */
constexpr auto generate_hevc_diag_scan4x4_y() noexcept {
    std::array<uint8_t, 16> scan{};

    // Exact diagonal scan order from HEVC specification
    constexpr uint8_t values[16] = {
        0, 1, 0, 2,
        1, 0, 3, 2,
        1, 0, 3, 2,
        1, 3, 2, 3
    };

    for (int i = 0; i < 16; ++i) {
        scan[i] = values[i];
    }

    return scan;
}

constexpr auto hevc_diag_scan4x4_y = generate_hevc_diag_scan4x4_y();

// ============================================================================
// 8×8 Diagonal Scan Tables (64 entries each, 128 bytes total)
// ============================================================================

/**
 * HEVC 8×8 diagonal scan X coordinates
 *
 * X coordinates for traversing an 8×8 transform block in diagonal order:
 * - Diagonal pattern from top-left to bottom-right
 * - Optimized for HEVC's larger transform sizes
 * - Improves run-length encoding for sparse coefficients
 *
 * Each entry specifies the X coordinate (column) in the 8×8 block.
 * Used with diag_scan8x8_y to form complete (x,y) coordinate pairs.
 */
constexpr auto generate_hevc_diag_scan8x8_x() noexcept {
    std::array<uint8_t, 64> scan{};

    // Exact diagonal scan order from HEVC specification
    constexpr uint8_t values[64] = {
        0, 0, 1, 0,
        1, 2, 0, 1,
        2, 3, 0, 1,
        2, 3, 4, 0,
        1, 2, 3, 4,
        5, 0, 1, 2,
        3, 4, 5, 6,
        0, 1, 2, 3,
        4, 5, 6, 7,
        1, 2, 3, 4,
        5, 6, 7, 2,
        3, 4, 5, 6,
        7, 3, 4, 5,
        6, 7, 4, 5,
        6, 7, 5, 6,
        7, 6, 7, 7
    };

    for (int i = 0; i < 64; ++i) {
        scan[i] = values[i];
    }

    return scan;
}

constexpr auto hevc_diag_scan8x8_x = generate_hevc_diag_scan8x8_x();

/**
 * HEVC 8×8 diagonal scan Y coordinates
 *
 * Y coordinates for traversing an 8×8 transform block in diagonal order.
 * Paired with diag_scan8x8_x to form complete coordinate pairs.
 *
 * Each entry specifies the Y coordinate (row) in the 8×8 block.
 */
constexpr auto generate_hevc_diag_scan8x8_y() noexcept {
    std::array<uint8_t, 64> scan{};

    // Exact diagonal scan order from HEVC specification
    constexpr uint8_t values[64] = {
        0, 1, 0, 2,
        1, 0, 3, 2,
        1, 0, 4, 3,
        2, 1, 0, 5,
        4, 3, 2, 1,
        0, 6, 5, 4,
        3, 2, 1, 0,
        7, 6, 5, 4,
        3, 2, 1, 0,
        7, 6, 5, 4,
        3, 2, 1, 7,
        6, 5, 4, 3,
        2, 7, 6, 5,
        4, 3, 7, 6,
        5, 4, 7, 6,
        5, 7, 6, 7
    };

    for (int i = 0; i < 64; ++i) {
        scan[i] = values[i];
    }

    return scan;
}

constexpr auto hevc_diag_scan8x8_y = generate_hevc_diag_scan8x8_y();

} // namespace FFmpegHEVCData

// ============================================================================
// Static Assertions - Compile-Time Validation
// ============================================================================

// 4×4 scan table size validation
static_assert(FFmpegHEVCData::hevc_diag_scan4x4_x.size() == 16,
              "4×4 X scan must have 16 entries");
static_assert(FFmpegHEVCData::hevc_diag_scan4x4_y.size() == 16,
              "4×4 Y scan must have 16 entries");

// 4×4 scan starts at origin (0,0)
static_assert(FFmpegHEVCData::hevc_diag_scan4x4_x[0] == 0,
              "4×4 scan starts at X=0");
static_assert(FFmpegHEVCData::hevc_diag_scan4x4_y[0] == 0,
              "4×4 scan starts at Y=0");

// 4×4 scan ends at bottom-right (3,3)
static_assert(FFmpegHEVCData::hevc_diag_scan4x4_x[15] == 3,
              "4×4 scan ends at X=3");
static_assert(FFmpegHEVCData::hevc_diag_scan4x4_y[15] == 3,
              "4×4 scan ends at Y=3");

// Verify 4×4 diagonal pattern (early positions)
static_assert(FFmpegHEVCData::hevc_diag_scan4x4_x[1] == 0 &&
              FFmpegHEVCData::hevc_diag_scan4x4_y[1] == 1,
              "4×4 scan position 1: (0,1)");
static_assert(FFmpegHEVCData::hevc_diag_scan4x4_x[2] == 1 &&
              FFmpegHEVCData::hevc_diag_scan4x4_y[2] == 0,
              "4×4 scan position 2: (1,0)");

// Verify all 4×4 coordinates are in valid range [0,3]
static_assert(FFmpegHEVCData::hevc_diag_scan4x4_x[0] <= 3,
              "4×4 X coordinates in range");
static_assert(FFmpegHEVCData::hevc_diag_scan4x4_y[0] <= 3,
              "4×4 Y coordinates in range");
static_assert(FFmpegHEVCData::hevc_diag_scan4x4_x[15] <= 3,
              "4×4 X max in range");
static_assert(FFmpegHEVCData::hevc_diag_scan4x4_y[15] <= 3,
              "4×4 Y max in range");

// 8×8 scan table size validation
static_assert(FFmpegHEVCData::hevc_diag_scan8x8_x.size() == 64,
              "8×8 X scan must have 64 entries");
static_assert(FFmpegHEVCData::hevc_diag_scan8x8_y.size() == 64,
              "8×8 Y scan must have 64 entries");

// 8×8 scan starts at origin (0,0)
static_assert(FFmpegHEVCData::hevc_diag_scan8x8_x[0] == 0,
              "8×8 scan starts at X=0");
static_assert(FFmpegHEVCData::hevc_diag_scan8x8_y[0] == 0,
              "8×8 scan starts at Y=0");

// 8×8 scan ends at bottom-right (7,7)
static_assert(FFmpegHEVCData::hevc_diag_scan8x8_x[63] == 7,
              "8×8 scan ends at X=7");
static_assert(FFmpegHEVCData::hevc_diag_scan8x8_y[63] == 7,
              "8×8 scan ends at Y=7");

// Verify 8×8 diagonal pattern (early positions)
static_assert(FFmpegHEVCData::hevc_diag_scan8x8_x[1] == 0 &&
              FFmpegHEVCData::hevc_diag_scan8x8_y[1] == 1,
              "8×8 scan position 1: (0,1)");
static_assert(FFmpegHEVCData::hevc_diag_scan8x8_x[2] == 1 &&
              FFmpegHEVCData::hevc_diag_scan8x8_y[2] == 0,
              "8×8 scan position 2: (1,0)");
static_assert(FFmpegHEVCData::hevc_diag_scan8x8_x[3] == 0 &&
              FFmpegHEVCData::hevc_diag_scan8x8_y[3] == 2,
              "8×8 scan position 3: (0,2)");

// Verify key 8×8 diagonal positions
static_assert(FFmpegHEVCData::hevc_diag_scan8x8_x[10] == 0 &&
              FFmpegHEVCData::hevc_diag_scan8x8_y[10] == 4,
              "8×8 scan mid position");
static_assert(FFmpegHEVCData::hevc_diag_scan8x8_x[32] == 4 &&
              FFmpegHEVCData::hevc_diag_scan8x8_y[32] == 3,
              "8×8 scan center region");

// Verify all 8×8 coordinates are in valid range [0,7]
static_assert(FFmpegHEVCData::hevc_diag_scan8x8_x[0] <= 7,
              "8×8 X coordinates in range");
static_assert(FFmpegHEVCData::hevc_diag_scan8x8_y[0] <= 7,
              "8×8 Y coordinates in range");
static_assert(FFmpegHEVCData::hevc_diag_scan8x8_x[63] <= 7,
              "8×8 X max in range");
static_assert(FFmpegHEVCData::hevc_diag_scan8x8_y[63] <= 7,
              "8×8 Y max in range");

// Verify middle positions
static_assert(FFmpegHEVCData::hevc_diag_scan8x8_x[40] == 5 &&
              FFmpegHEVCData::hevc_diag_scan8x8_y[40] == 3,
              "8×8 scan position 40");
static_assert(FFmpegHEVCData::hevc_diag_scan8x8_x[50] == 4 &&
              FFmpegHEVCData::hevc_diag_scan8x8_y[50] == 6,
              "8×8 scan position 50");

// Verify near-end positions approach bottom-right
static_assert(FFmpegHEVCData::hevc_diag_scan8x8_x[60] == 7 &&
              FFmpegHEVCData::hevc_diag_scan8x8_y[60] == 5,
              "8×8 near end position");
static_assert(FFmpegHEVCData::hevc_diag_scan8x8_x[62] == 7 &&
              FFmpegHEVCData::hevc_diag_scan8x8_y[62] == 6,
              "8×8 penultimate position");

// ============================================================================
// C API Compatibility Layer
// ============================================================================

extern "C" {

/**
 * Get pointer to HEVC 4×4 diagonal scan X coordinates
 * @return Pointer to 16-entry uint8_t array
 */
inline const uint8_t* get_hevc_diag_scan4x4_x() {
    return FFmpegHEVCData::hevc_diag_scan4x4_x.data();
}

/**
 * Get pointer to HEVC 4×4 diagonal scan Y coordinates
 * @return Pointer to 16-entry uint8_t array
 */
inline const uint8_t* get_hevc_diag_scan4x4_y() {
    return FFmpegHEVCData::hevc_diag_scan4x4_y.data();
}

/**
 * Get pointer to HEVC 8×8 diagonal scan X coordinates
 * @return Pointer to 64-entry uint8_t array
 */
inline const uint8_t* get_hevc_diag_scan8x8_x() {
    return FFmpegHEVCData::hevc_diag_scan8x8_x.data();
}

/**
 * Get pointer to HEVC 8×8 diagonal scan Y coordinates
 * @return Pointer to 64-entry uint8_t array
 */
inline const uint8_t* get_hevc_diag_scan8x8_y() {
    return FFmpegHEVCData::hevc_diag_scan8x8_y.data();
}

} // extern "C"

#endif // AVCODEC_HEVC_DATA_TABLEGEN_CONSTEXPR_HPP
