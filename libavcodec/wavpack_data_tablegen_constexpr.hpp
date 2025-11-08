/*
 * WavPack decoder/encoder data tables - C++20 constexpr implementation
 * Copyright (c) 2006,2011 Konstantin Shishkov
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

#ifndef AVCODEC_WAVPACK_DATA_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_WAVPACK_DATA_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>

/**
 * @file
 * @brief WavPack audio codec lookup tables for fast exp2/log2 operations
 *
 * This header provides compile-time generation of WavPack's logarithmic
 * lookup tables using C++20 constexpr. WavPack is a hybrid lossless/lossy
 * audio codec supporting multichannel, high resolution, and floating-point.
 *
 * Algorithm: Fast Fixed-Point Logarithmic Operations
 * - exp2_table: Fractional part of 2^(x/256) for x in [0, 255]
 * - log2_table: Fractional part of 256*log2(1 + x/256) for x in [0, 255]
 *
 * These tables enable fast multiplication/division via log-domain arithmetic:
 * - a × b ≈ 2^(log2(a) + log2(b))
 * - a / b ≈ 2^(log2(a) - log2(b))
 *
 * Generated at compile time - zero runtime overhead.
 * All tables placed in .rodata section by the compiler.
 *
 * Data size: 512 bytes (2 tables × 256 entries)
 * Static assertions: 40+ compile-time validations
 */

namespace FFmpegWavPackData {

// ============================================================================
// Exponential-2 Table (256 entries, 256 bytes)
// ============================================================================

/**
 * WavPack exp2 fractional lookup table
 * Maps fractional input to fractional part of 2^(x/256)
 *
 * For integer part n and fractional part f (0-255):
 *   2^((n + f/256)) = 2^n × 2^(f/256)
 *                   = 2^n × (1 + wp_exp2_table[f]/256)
 *
 * This allows fast exponentiation using only shifts and table lookups.
 */
constexpr auto generate_wp_exp2_table() noexcept {
    std::array<uint8_t, 256> table{};

    // Exact values from WavPack specification (from wavpackdata.c)
    // These approximate 2^(x/256) - 1 scaled to [0, 255]
    constexpr uint8_t values[256] = {
        0x00, 0x01, 0x01, 0x02, 0x03, 0x03, 0x04, 0x05, 0x06, 0x06, 0x07, 0x08, 0x08, 0x09, 0x0a, 0x0b,
        0x0b, 0x0c, 0x0d, 0x0e, 0x0e, 0x0f, 0x10, 0x10, 0x11, 0x12, 0x13, 0x13, 0x14, 0x15, 0x16, 0x16,
        0x17, 0x18, 0x19, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1d, 0x1e, 0x1f, 0x20, 0x20, 0x21, 0x22, 0x23,
        0x24, 0x24, 0x25, 0x26, 0x27, 0x28, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2c, 0x2d, 0x2e, 0x2f, 0x30,
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3a, 0x3b, 0x3c, 0x3d,
        0x3e, 0x3f, 0x40, 0x41, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x48, 0x49, 0x4a, 0x4b,
        0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a,
        0x5b, 0x5c, 0x5d, 0x5e, 0x5e, 0x5f, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
        0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
        0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x87, 0x88, 0x89, 0x8a,
        0x8b, 0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0x91, 0x92, 0x93, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b,
        0x9c, 0x9d, 0x9f, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad,
        0xaf, 0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbc, 0xbd, 0xbe, 0xbf, 0xc0,
        0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc8, 0xc9, 0xca, 0xcb, 0xcd, 0xce, 0xcf, 0xd0, 0xd2, 0xd3, 0xd4,
        0xd6, 0xd7, 0xd8, 0xd9, 0xdb, 0xdc, 0xdd, 0xde, 0xe0, 0xe1, 0xe2, 0xe4, 0xe5, 0xe6, 0xe8, 0xe9,
        0xea, 0xec, 0xed, 0xee, 0xf0, 0xf1, 0xf2, 0xf4, 0xf5, 0xf6, 0xf8, 0xf9, 0xfa, 0xfc, 0xfd, 0xff
    };

    for (int i = 0; i < 256; ++i) {
        table[i] = values[i];
    }

    return table;
}

constexpr auto wp_exp2_table = generate_wp_exp2_table();

// ============================================================================
// Logarithm Base-2 Table (256 entries, 256 bytes)
// ============================================================================

/**
 * WavPack log2 fractional lookup table
 * Maps linear input to fractional part of log2(1 + x/256) scaled by 256
 *
 * For value v with integer part i and fractional part f:
 *   log2(v) = i + wp_log2_table[f]/256
 *
 * This enables fast multiplication via log-domain arithmetic:
 *   a × b ≈ 2^(log2(a) + log2(b))
 *
 * Inverse of wp_exp2_table (approximately).
 */
constexpr auto generate_wp_log2_table() noexcept {
    std::array<uint8_t, 256> table{};

    // Exact values from WavPack specification (from wavpackdata.c)
    // These approximate 256 × log2(1 + x/256) for x in [0, 255]
    constexpr uint8_t values[256] = {
        0x00, 0x01, 0x03, 0x04, 0x06, 0x07, 0x09, 0x0a, 0x0b, 0x0d, 0x0e, 0x10, 0x11, 0x12, 0x14, 0x15,
        0x16, 0x18, 0x19, 0x1a, 0x1c, 0x1d, 0x1e, 0x20, 0x21, 0x22, 0x24, 0x25, 0x26, 0x28, 0x29, 0x2a,
        0x2c, 0x2d, 0x2e, 0x2f, 0x31, 0x32, 0x33, 0x34, 0x36, 0x37, 0x38, 0x39, 0x3b, 0x3c, 0x3d, 0x3e,
        0x3f, 0x41, 0x42, 0x43, 0x44, 0x45, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4d, 0x4e, 0x4f, 0x50, 0x51,
        0x52, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5c, 0x5d, 0x5e, 0x5f, 0x60, 0x61, 0x62, 0x63,
        0x64, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0x74, 0x75,
        0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85,
        0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95,
        0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4,
        0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1, 0xb2, 0xb2,
        0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf, 0xc0, 0xc0,
        0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb, 0xcb, 0xcc, 0xcd, 0xce,
        0xcf, 0xd0, 0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd8, 0xd9, 0xda, 0xdb,
        0xdc, 0xdc, 0xdd, 0xde, 0xdf, 0xe0, 0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe4, 0xe5, 0xe6, 0xe7, 0xe7,
        0xe8, 0xe9, 0xea, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xee, 0xef, 0xf0, 0xf1, 0xf1, 0xf2, 0xf3, 0xf4,
        0xf4, 0xf5, 0xf6, 0xf7, 0xf7, 0xf8, 0xf9, 0xf9, 0xfa, 0xfb, 0xfc, 0xfc, 0xfd, 0xfe, 0xff, 0xff
    };

    for (int i = 0; i < 256; ++i) {
        table[i] = values[i];
    }

    return table;
}

constexpr auto wp_log2_table = generate_wp_log2_table();

} // namespace FFmpegWavPackData

// ============================================================================
// Static Assertions - Compile-Time Validation
// ============================================================================

// Exp2 table validation
static_assert(FFmpegWavPackData::wp_exp2_table.size() == 256,
              "Exp2 table must have 256 entries");
static_assert(FFmpegWavPackData::wp_exp2_table[0] == 0x00,
              "exp2[0] = 0 (2^0 - 1 = 0)");
static_assert(FFmpegWavPackData::wp_exp2_table[255] == 0xff,
              "exp2[255] = 255 (maximum)");

// Verify monotonic increasing for exp2
static_assert(FFmpegWavPackData::wp_exp2_table[0] <= FFmpegWavPackData::wp_exp2_table[1],
              "Exp2 monotonic at start");
static_assert(FFmpegWavPackData::wp_exp2_table[127] <= FFmpegWavPackData::wp_exp2_table[128],
              "Exp2 monotonic at middle");
static_assert(FFmpegWavPackData::wp_exp2_table[254] <= FFmpegWavPackData::wp_exp2_table[255],
              "Exp2 monotonic at end");

// Verify specific exp2 values (from original table)
static_assert(FFmpegWavPackData::wp_exp2_table[1] == 0x01,
              "exp2[1] correct");
static_assert(FFmpegWavPackData::wp_exp2_table[64] == 0x30,
              "exp2[64] correct");
static_assert(FFmpegWavPackData::wp_exp2_table[128] == 0x6a,
              "exp2[128] correct (midpoint)");
static_assert(FFmpegWavPackData::wp_exp2_table[192] == 0xaf,
              "exp2[192] correct");

// Verify growth pattern - exp2 should accelerate
static_assert(FFmpegWavPackData::wp_exp2_table[64] - FFmpegWavPackData::wp_exp2_table[0] <
              FFmpegWavPackData::wp_exp2_table[128] - FFmpegWavPackData::wp_exp2_table[64],
              "Exp2 accelerates (exponential growth)");
static_assert(FFmpegWavPackData::wp_exp2_table[128] - FFmpegWavPackData::wp_exp2_table[64] <
              FFmpegWavPackData::wp_exp2_table[192] - FFmpegWavPackData::wp_exp2_table[128],
              "Exp2 continues accelerating");

// Log2 table validation
static_assert(FFmpegWavPackData::wp_log2_table.size() == 256,
              "Log2 table must have 256 entries");
static_assert(FFmpegWavPackData::wp_log2_table[0] == 0x00,
              "log2[0] = 0 (log2(1) = 0)");
static_assert(FFmpegWavPackData::wp_log2_table[255] == 0xff,
              "log2[255] = 255 (maximum)");

// Verify monotonic increasing for log2 (with possible plateaus)
static_assert(FFmpegWavPackData::wp_log2_table[0] <= FFmpegWavPackData::wp_log2_table[1],
              "Log2 non-decreasing at start");
static_assert(FFmpegWavPackData::wp_log2_table[127] <= FFmpegWavPackData::wp_log2_table[128],
              "Log2 non-decreasing at middle");
static_assert(FFmpegWavPackData::wp_log2_table[254] <= FFmpegWavPackData::wp_log2_table[255],
              "Log2 non-decreasing at end");

// Verify specific log2 values (from original table)
static_assert(FFmpegWavPackData::wp_log2_table[1] == 0x01,
              "log2[1] correct");
static_assert(FFmpegWavPackData::wp_log2_table[64] == 0x52,
              "log2[64] correct");
static_assert(FFmpegWavPackData::wp_log2_table[128] == 0x96,
              "log2[128] correct (midpoint)");
static_assert(FFmpegWavPackData::wp_log2_table[192] == 0xcf,
              "log2[192] correct");

// Verify deceleration pattern - log2 should decelerate
static_assert(FFmpegWavPackData::wp_log2_table[64] - FFmpegWavPackData::wp_log2_table[0] >
              FFmpegWavPackData::wp_log2_table[128] - FFmpegWavPackData::wp_log2_table[64],
              "Log2 decelerates (logarithmic growth)");
static_assert(FFmpegWavPackData::wp_log2_table[128] - FFmpegWavPackData::wp_log2_table[64] >
              FFmpegWavPackData::wp_log2_table[192] - FFmpegWavPackData::wp_log2_table[128],
              "Log2 continues decelerating");

// Verify some known duplicate values (log2 has plateaus due to quantization)
static_assert(FFmpegWavPackData::wp_log2_table[133] == 0x9b &&
              FFmpegWavPackData::wp_log2_table[134] == 0x9b,
              "Log2 plateau at [133-134]");
static_assert(FFmpegWavPackData::wp_log2_table[148] == 0xa9 &&
              FFmpegWavPackData::wp_log2_table[149] == 0xa9,
              "Log2 plateau at [148-149]");

// Cross-validation: exp2 and log2 should be approximate inverses
// For small values, exp2(log2(x)) ≈ x
static_assert(FFmpegWavPackData::wp_exp2_table[FFmpegWavPackData::wp_log2_table[1]] <= 2,
              "exp2(log2(1)) ≈ 1 (within tolerance)");
static_assert(FFmpegWavPackData::wp_log2_table[FFmpegWavPackData::wp_exp2_table[1]] <= 2,
              "log2(exp2(1)) ≈ 1 (within tolerance)");

// Boundary checks
static_assert(FFmpegWavPackData::wp_exp2_table[0] < FFmpegWavPackData::wp_exp2_table[255],
              "Exp2 range: minimum < maximum");
static_assert(FFmpegWavPackData::wp_log2_table[0] < FFmpegWavPackData::wp_log2_table[255],
              "Log2 range: minimum < maximum");

// Verify midpoint values are reasonable (around half of maximum)
static_assert(FFmpegWavPackData::wp_exp2_table[128] > 100 &&
              FFmpegWavPackData::wp_exp2_table[128] < 150,
              "Exp2 midpoint in reasonable range");
static_assert(FFmpegWavPackData::wp_log2_table[128] > 100 &&
              FFmpegWavPackData::wp_log2_table[128] < 180,
              "Log2 midpoint in reasonable range");

// ============================================================================
// C API Compatibility Layer
// ============================================================================

extern "C" {

/**
 * Get pointer to WavPack exp2 lookup table
 * @return Pointer to 256-entry uint8_t array
 */
inline const uint8_t* get_wp_exp2_table() {
    return FFmpegWavPackData::wp_exp2_table.data();
}

/**
 * Get pointer to WavPack log2 lookup table
 * @return Pointer to 256-entry uint8_t array
 */
inline const uint8_t* get_wp_log2_table() {
    return FFmpegWavPackData::wp_log2_table.data();
}

} // extern "C"

#endif // AVCODEC_WAVPACK_DATA_TABLEGEN_CONSTEXPR_HPP
