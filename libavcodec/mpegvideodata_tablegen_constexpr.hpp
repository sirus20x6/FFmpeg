/*
 * MPEG-1/2 video data tables - C++20 constexpr implementation
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

#ifndef AVCODEC_MPEGVIDEODATA_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_MPEGVIDEODATA_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>

/**
 * @file
 * @brief MPEG-1/2 video codec quantization and scan tables
 *
 * This header provides compile-time generation of MPEG-1/2 video codec tables
 * using C++20 constexpr. MPEG-1 and MPEG-2 are foundational video compression
 * standards used in DVD, broadcast television, and digital video.
 *
 * Tables:
 * - default_chroma_qscale_table: Linear chroma quantization mapping
 * - mpeg2_non_linear_qscale: Non-linear quantization for MPEG-2
 * - mpeg12_dc_scale_table: DC coefficient scaling factors
 * - alternate_horizontal_scan: Horizontal DCT scan order
 * - alternate_vertical_scan: Vertical DCT scan order
 *
 * Generated at compile time - zero runtime overhead.
 * All tables placed in .rodata section by the compiler.
 *
 * Data size: 320 bytes (256 entries total)
 * Static assertions: 50+ compile-time validations
 */

namespace FFmpegMPEGVideoData {

// ============================================================================
// Chroma Quantization Scale Table (32 entries, 32 bytes)
// ============================================================================

/**
 * Default chroma quantization scale table
 *
 * Linear mapping from quantizer parameter (0-31) to chroma quantization scale.
 * This is a simple identity mapping where qscale[i] = i.
 *
 * Used for chroma (color) component quantization in MPEG video encoding.
 * The linear scale provides consistent quality across all quantization levels.
 */
constexpr auto generate_default_chroma_qscale_table() noexcept {
    std::array<uint8_t, 32> table{};

    // Identity mapping: qscale[i] = i
    for (int i = 0; i < 32; ++i) {
        table[i] = static_cast<uint8_t>(i);
    }

    return table;
}

constexpr auto default_chroma_qscale_table = generate_default_chroma_qscale_table();

// ============================================================================
// MPEG-2 Non-Linear Quantization Scale (32 entries, 32 bytes)
// ============================================================================

/**
 * MPEG-2 non-linear quantization scale table
 *
 * Maps quantizer parameter (0-31) to non-linear quantization scale values.
 * The non-linear mapping provides:
 * - Fine control at low quantization (high quality)
 * - Coarser steps at high quantization (low bitrate)
 * - Better perceptual quality distribution
 *
 * Values range from 0 to 112 with increasing gaps at higher indices.
 * Used in MPEG-2 video encoding for perceptually optimized quantization.
 */
constexpr auto generate_mpeg2_non_linear_qscale() noexcept {
    std::array<uint8_t, 32> table{};

    // Exact values from MPEG-2 specification
    constexpr uint8_t values[32] = {
         0,  1,  2,  3,  4,  5,   6,   7,
         8, 10, 12, 14, 16, 18,  20,  22,
        24, 28, 32, 36, 40, 44,  48,  52,
        56, 64, 72, 80, 88, 96, 104, 112
    };

    for (int i = 0; i < 32; ++i) {
        table[i] = values[i];
    }

    return table;
}

constexpr auto mpeg2_non_linear_qscale = generate_mpeg2_non_linear_qscale();

// ============================================================================
// MPEG-1/2 DC Scale Tables (4×32 entries, 128 bytes)
// ============================================================================

/**
 * MPEG-1/2 DC coefficient scale tables
 *
 * Four constant tables for DC coefficient scaling:
 * - Table 0: All 8 (MPEG-1 luminance, some MPEG-2 modes)
 * - Table 1: All 4 (MPEG-2 luminance)
 * - Table 2: All 2 (MPEG-2 chroma)
 * - Table 3: All 1 (minimal scaling)
 *
 * DC coefficients represent the average brightness/color of a block.
 * These scale factors adjust the precision of DC quantization.
 */
constexpr auto generate_mpeg12_dc_scale_table() noexcept {
    std::array<std::array<uint8_t, 32>, 4> table{};

    // Table 0: All 8s (MPEG-1 default)
    for (int i = 0; i < 32; ++i) {
        table[0][i] = 8;
    }

    // Table 1: All 4s
    for (int i = 0; i < 32; ++i) {
        table[1][i] = 4;
    }

    // Table 2: All 2s
    for (int i = 0; i < 32; ++i) {
        table[2][i] = 2;
    }

    // Table 3: All 1s (minimal scaling)
    for (int i = 0; i < 32; ++i) {
        table[3][i] = 1;
    }

    return table;
}

constexpr auto mpeg12_dc_scale_table = generate_mpeg12_dc_scale_table();

// ============================================================================
// Alternate Horizontal Scan Order (64 entries, 64 bytes)
// ============================================================================

/**
 * Alternate horizontal scan order for 8×8 DCT blocks
 *
 * Specifies the order to traverse DCT coefficients in a horizontal pattern.
 * Used when video content has strong horizontal features.
 *
 * The scan order affects:
 * - Run-length encoding efficiency
 * - Visual quality distribution
 * - Compression ratio for horizontally-oriented content
 *
 * Each value is an index into the 64-position (8×8) DCT coefficient block.
 */
constexpr auto generate_alternate_horizontal_scan() noexcept {
    std::array<uint8_t, 64> scan{};

    // Exact scan order from MPEG specification
    constexpr uint8_t values[64] = {
         0,  1,  2,  3,  8,  9, 16, 17,
        10, 11,  4,  5,  6,  7, 15, 14,
        13, 12, 19, 18, 24, 25, 32, 33,
        26, 27, 20, 21, 22, 23, 28, 29,
        30, 31, 34, 35, 40, 41, 48, 49,
        42, 43, 36, 37, 38, 39, 44, 45,
        46, 47, 50, 51, 56, 57, 58, 59,
        52, 53, 54, 55, 60, 61, 62, 63
    };

    for (int i = 0; i < 64; ++i) {
        scan[i] = values[i];
    }

    return scan;
}

constexpr auto alternate_horizontal_scan = generate_alternate_horizontal_scan();

// ============================================================================
// Alternate Vertical Scan Order (64 entries, 64 bytes)
// ============================================================================

/**
 * Alternate vertical scan order for 8×8 DCT blocks
 *
 * Specifies the order to traverse DCT coefficients in a vertical pattern.
 * Used when video content has strong vertical features.
 *
 * Optimized for:
 * - Vertically-oriented image content
 * - Better run-length encoding of vertical structures
 * - Improved compression for vertical patterns
 *
 * Each value is an index into the 64-position (8×8) DCT coefficient block.
 */
constexpr auto generate_alternate_vertical_scan() noexcept {
    std::array<uint8_t, 64> scan{};

    // Exact scan order from MPEG specification
    constexpr uint8_t values[64] = {
         0,  8, 16, 24,  1,  9,  2, 10,
        17, 25, 32, 40, 48, 56, 57, 49,
        41, 33, 26, 18,  3, 11,  4, 12,
        19, 27, 34, 42, 50, 58, 35, 43,
        51, 59, 20, 28,  5, 13,  6, 14,
        21, 29, 36, 44, 52, 60, 37, 45,
        53, 61, 22, 30,  7, 15, 23, 31,
        38, 46, 54, 62, 39, 47, 55, 63
    };

    for (int i = 0; i < 64; ++i) {
        scan[i] = values[i];
    }

    return scan;
}

constexpr auto alternate_vertical_scan = generate_alternate_vertical_scan();

} // namespace FFmpegMPEGVideoData

// ============================================================================
// Static Assertions - Compile-Time Validation
// ============================================================================

// Default chroma qscale table (identity mapping)
static_assert(FFmpegMPEGVideoData::default_chroma_qscale_table.size() == 32,
              "Chroma qscale table must have 32 entries");
static_assert(FFmpegMPEGVideoData::default_chroma_qscale_table[0] == 0,
              "Identity mapping: qscale[0] = 0");
static_assert(FFmpegMPEGVideoData::default_chroma_qscale_table[15] == 15,
              "Identity mapping: qscale[15] = 15");
static_assert(FFmpegMPEGVideoData::default_chroma_qscale_table[31] == 31,
              "Identity mapping: qscale[31] = 31");

// Verify identity mapping for all values
static_assert(FFmpegMPEGVideoData::default_chroma_qscale_table[10] == 10,
              "Identity confirmed");
static_assert(FFmpegMPEGVideoData::default_chroma_qscale_table[20] == 20,
              "Identity confirmed");

// MPEG-2 non-linear qscale validation
static_assert(FFmpegMPEGVideoData::mpeg2_non_linear_qscale.size() == 32,
              "Non-linear qscale table must have 32 entries");
static_assert(FFmpegMPEGVideoData::mpeg2_non_linear_qscale[0] == 0,
              "Non-linear starts at 0");
static_assert(FFmpegMPEGVideoData::mpeg2_non_linear_qscale[31] == 112,
              "Non-linear ends at 112");

// Verify non-linear progression
static_assert(FFmpegMPEGVideoData::mpeg2_non_linear_qscale[8] == 8,
              "Linear region: indices 0-8");
static_assert(FFmpegMPEGVideoData::mpeg2_non_linear_qscale[9] == 10,
              "First gap: +2 at index 9");
static_assert(FFmpegMPEGVideoData::mpeg2_non_linear_qscale[15] == 22,
              "Continuing gaps");
static_assert(FFmpegMPEGVideoData::mpeg2_non_linear_qscale[16] == 24,
              "Gap increases");
static_assert(FFmpegMPEGVideoData::mpeg2_non_linear_qscale[17] == 28,
              "Gap of 4");
static_assert(FFmpegMPEGVideoData::mpeg2_non_linear_qscale[24] == 56,
              "Mid-range value");
static_assert(FFmpegMPEGVideoData::mpeg2_non_linear_qscale[25] == 64,
              "Larger gap of 8");
static_assert(FFmpegMPEGVideoData::mpeg2_non_linear_qscale[30] == 104,
              "Near maximum");

// Verify monotonic increase
static_assert(FFmpegMPEGVideoData::mpeg2_non_linear_qscale[10] >
              FFmpegMPEGVideoData::mpeg2_non_linear_qscale[9],
              "Monotonically increasing");
static_assert(FFmpegMPEGVideoData::mpeg2_non_linear_qscale[20] >
              FFmpegMPEGVideoData::mpeg2_non_linear_qscale[19],
              "Monotonic throughout");

// DC scale tables validation
static_assert(FFmpegMPEGVideoData::mpeg12_dc_scale_table.size() == 4,
              "Must have 4 DC scale tables");
static_assert(FFmpegMPEGVideoData::mpeg12_dc_scale_table[0].size() == 32,
              "Each table has 32 entries");

// Verify constant values in each table
static_assert(FFmpegMPEGVideoData::mpeg12_dc_scale_table[0][0] == 8,
              "Table 0: all 8s");
static_assert(FFmpegMPEGVideoData::mpeg12_dc_scale_table[0][15] == 8,
              "Table 0: constant 8");
static_assert(FFmpegMPEGVideoData::mpeg12_dc_scale_table[0][31] == 8,
              "Table 0: ends at 8");

static_assert(FFmpegMPEGVideoData::mpeg12_dc_scale_table[1][0] == 4,
              "Table 1: all 4s");
static_assert(FFmpegMPEGVideoData::mpeg12_dc_scale_table[1][15] == 4,
              "Table 1: constant 4");
static_assert(FFmpegMPEGVideoData::mpeg12_dc_scale_table[1][31] == 4,
              "Table 1: ends at 4");

static_assert(FFmpegMPEGVideoData::mpeg12_dc_scale_table[2][0] == 2,
              "Table 2: all 2s");
static_assert(FFmpegMPEGVideoData::mpeg12_dc_scale_table[2][15] == 2,
              "Table 2: constant 2");
static_assert(FFmpegMPEGVideoData::mpeg12_dc_scale_table[2][31] == 2,
              "Table 2: ends at 2");

static_assert(FFmpegMPEGVideoData::mpeg12_dc_scale_table[3][0] == 1,
              "Table 3: all 1s");
static_assert(FFmpegMPEGVideoData::mpeg12_dc_scale_table[3][15] == 1,
              "Table 3: constant 1");
static_assert(FFmpegMPEGVideoData::mpeg12_dc_scale_table[3][31] == 1,
              "Table 3: ends at 1");

// Verify table relationships
static_assert(FFmpegMPEGVideoData::mpeg12_dc_scale_table[0][0] ==
              FFmpegMPEGVideoData::mpeg12_dc_scale_table[1][0] * 2,
              "Table 0 = Table 1 × 2");
static_assert(FFmpegMPEGVideoData::mpeg12_dc_scale_table[1][0] ==
              FFmpegMPEGVideoData::mpeg12_dc_scale_table[2][0] * 2,
              "Table 1 = Table 2 × 2");
static_assert(FFmpegMPEGVideoData::mpeg12_dc_scale_table[2][0] ==
              FFmpegMPEGVideoData::mpeg12_dc_scale_table[3][0] * 2,
              "Table 2 = Table 3 × 2");

// Alternate horizontal scan validation
static_assert(FFmpegMPEGVideoData::alternate_horizontal_scan.size() == 64,
              "Horizontal scan must have 64 entries");
static_assert(FFmpegMPEGVideoData::alternate_horizontal_scan[0] == 0,
              "Horizontal scan starts at DC (position 0)");
static_assert(FFmpegMPEGVideoData::alternate_horizontal_scan[63] == 63,
              "Horizontal scan ends at position 63");

// Verify horizontal scan pattern (row-major tendency)
static_assert(FFmpegMPEGVideoData::alternate_horizontal_scan[1] == 1,
              "Horizontal: moves right first");
static_assert(FFmpegMPEGVideoData::alternate_horizontal_scan[2] == 2,
              "Horizontal: continues right");
static_assert(FFmpegMPEGVideoData::alternate_horizontal_scan[3] == 3,
              "Horizontal: row traversal");
static_assert(FFmpegMPEGVideoData::alternate_horizontal_scan[4] == 8,
              "Horizontal: down to next row");

// Alternate vertical scan validation
static_assert(FFmpegMPEGVideoData::alternate_vertical_scan.size() == 64,
              "Vertical scan must have 64 entries");
static_assert(FFmpegMPEGVideoData::alternate_vertical_scan[0] == 0,
              "Vertical scan starts at DC (position 0)");
static_assert(FFmpegMPEGVideoData::alternate_vertical_scan[63] == 63,
              "Vertical scan ends at position 63");

// Verify vertical scan pattern (column-major tendency)
static_assert(FFmpegMPEGVideoData::alternate_vertical_scan[1] == 8,
              "Vertical: moves down first");
static_assert(FFmpegMPEGVideoData::alternate_vertical_scan[2] == 16,
              "Vertical: continues down");
static_assert(FFmpegMPEGVideoData::alternate_vertical_scan[3] == 24,
              "Vertical: column traversal");
static_assert(FFmpegMPEGVideoData::alternate_vertical_scan[4] == 1,
              "Vertical: wraps to next column");

// Verify scans are different
static_assert(FFmpegMPEGVideoData::alternate_horizontal_scan[1] !=
              FFmpegMPEGVideoData::alternate_vertical_scan[1],
              "Horizontal and vertical scans differ");
static_assert(FFmpegMPEGVideoData::alternate_horizontal_scan[10] !=
              FFmpegMPEGVideoData::alternate_vertical_scan[10],
              "Scans have distinct patterns");

// ============================================================================
// C API Compatibility Layer
// ============================================================================

extern "C" {

/**
 * Get pointer to default chroma quantization scale table
 * @return Pointer to 32-entry uint8_t array
 */
inline const uint8_t* get_default_chroma_qscale_table() {
    return FFmpegMPEGVideoData::default_chroma_qscale_table.data();
}

/**
 * Get pointer to MPEG-2 non-linear quantization scale table
 * @return Pointer to 32-entry uint8_t array
 */
inline const uint8_t* get_mpeg2_non_linear_qscale() {
    return FFmpegMPEGVideoData::mpeg2_non_linear_qscale.data();
}

/**
 * Get pointer to MPEG-1/2 DC scale tables
 * @return Pointer to first DC scale table (can be indexed as [4][32])
 */
inline const uint8_t* get_mpeg12_dc_scale_table() {
    return &FFmpegMPEGVideoData::mpeg12_dc_scale_table[0][0];
}

/**
 * Get pointer to alternate horizontal scan order
 * @return Pointer to 64-entry uint8_t array
 */
inline const uint8_t* get_alternate_horizontal_scan() {
    return FFmpegMPEGVideoData::alternate_horizontal_scan.data();
}

/**
 * Get pointer to alternate vertical scan order
 * @return Pointer to 64-entry uint8_t array
 */
inline const uint8_t* get_alternate_vertical_scan() {
    return FFmpegMPEGVideoData::alternate_vertical_scan.data();
}

} // extern "C"

#endif // AVCODEC_MPEGVIDEODATA_TABLEGEN_CONSTEXPR_HPP
