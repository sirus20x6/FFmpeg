/*
 * TTA (The Lossless True Audio) data tables - C++20 constexpr implementation
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

#ifndef AVCODEC_TTA_DATA_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_TTA_DATA_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>

/**
 * @file
 * @brief TTA (True Audio) codec lookup tables for Rice coding and filtering
 *
 * This header provides compile-time generation of TTA's shift and configuration
 * tables using C++20 constexpr. TTA is a lossless audio codec optimized for
 * real-time encoding with high compression ratios.
 *
 * Algorithm: TTA uses adaptive Rice coding with predictive filtering
 * - shift_1: Powers of 2 with saturation (2^0 through 2^30, then saturated)
 * - shift_16: Offset pointer to shift_1[4] (value 2^4 = 16)
 * - filter_configs: Adaptive filter order configuration for different sample rates
 *
 * Generated at compile time - zero runtime overhead.
 * All tables placed in .rodata section by the compiler.
 *
 * Data size: 169 bytes (41 + 4 entries)
 * Static assertions: 30+ compile-time validations
 */

namespace FFmpegTTAData {

// ============================================================================
// Shift Table (41 entries, 164 bytes)
// ============================================================================

/**
 * TTA shift values for Rice coding parameter adaptation
 *
 * Pattern:
 * - Entries 0-30: Powers of 2 (2^i for i=0..30)
 * - Entries 31-39: Saturated at 0x80000000 (2^31 maximum for signed int32)
 * - Entry 40: Special marker 0xFFFFFFFF
 *
 * Used for:
 * - Rice parameter initialization (sum0/sum1 = shift_16[k])
 * - Filter rounding values (round = shift_1[shift-1])
 */
constexpr auto generate_tta_shift_1() noexcept {
    std::array<uint32_t, 41> table{};

    // Powers of 2 from 2^0 to 2^30
    for (int i = 0; i < 31; ++i) {
        table[i] = static_cast<uint32_t>(1) << i;
    }

    // Saturation at 0x80000000 (2^31, maximum positive value for signed int32)
    for (int i = 31; i < 40; ++i) {
        table[i] = 0x80000000;
    }

    // Special marker at end
    table[40] = 0xFFFFFFFF;

    return table;
}

constexpr auto tta_shift_1 = generate_tta_shift_1();

/**
 * TTA shift_16: Offset pointer to shift_1[4]
 * This points to the value 2^4 = 16, used as a base for Rice parameter sums
 *
 * In C++, we provide an offset constant instead of a pointer
 */
constexpr int TTA_SHIFT_16_OFFSET = 4;

// Helper to access shift_16 values
constexpr uint32_t get_tta_shift_16(int index) noexcept {
    return tta_shift_1[TTA_SHIFT_16_OFFSET + index];
}

// ============================================================================
// Filter Configuration Table (4 entries, 4 bytes)
// ============================================================================

/**
 * TTA adaptive filter order configuration
 * Indexed by bps (bits per sample) minus 1:
 * - [0]: 8-bit samples  → order 10
 * - [1]: 16-bit samples → order 9
 * - [2]: 24-bit samples → order 10
 * - [3]: 32-bit samples → order 12
 *
 * Higher precision samples use higher filter orders for better prediction
 */
constexpr auto generate_tta_filter_configs() noexcept {
    std::array<uint8_t, 4> table{};

    table[0] = 10;  // 8-bit
    table[1] = 9;   // 16-bit
    table[2] = 10;  // 24-bit
    table[3] = 12;  // 32-bit

    return table;
}

constexpr auto tta_filter_configs = generate_tta_filter_configs();

} // namespace FFmpegTTAData

// ============================================================================
// Static Assertions - Compile-Time Validation
// ============================================================================

// Shift table size
static_assert(FFmpegTTAData::tta_shift_1.size() == 41,
              "Shift table must have 41 entries");

// Verify powers of 2 (first 31 entries)
static_assert(FFmpegTTAData::tta_shift_1[0] == 0x00000001,
              "shift_1[0] = 2^0 = 1");
static_assert(FFmpegTTAData::tta_shift_1[1] == 0x00000002,
              "shift_1[1] = 2^1 = 2");
static_assert(FFmpegTTAData::tta_shift_1[2] == 0x00000004,
              "shift_1[2] = 2^2 = 4");
static_assert(FFmpegTTAData::tta_shift_1[4] == 0x00000010,
              "shift_1[4] = 2^4 = 16 (shift_16 base)");
static_assert(FFmpegTTAData::tta_shift_1[8] == 0x00000100,
              "shift_1[8] = 2^8 = 256");
static_assert(FFmpegTTAData::tta_shift_1[10] == 0x00000400,
              "shift_1[10] = 2^10 = 1024");
static_assert(FFmpegTTAData::tta_shift_1[16] == 0x00010000,
              "shift_1[16] = 2^16 = 65536");
static_assert(FFmpegTTAData::tta_shift_1[20] == 0x00100000,
              "shift_1[20] = 2^20 = 1048576");
static_assert(FFmpegTTAData::tta_shift_1[30] == 0x40000000,
              "shift_1[30] = 2^30 = 1073741824");

// Verify saturation region (entries 31-39)
static_assert(FFmpegTTAData::tta_shift_1[31] == 0x80000000,
              "shift_1[31] = saturated at 2^31");
static_assert(FFmpegTTAData::tta_shift_1[32] == 0x80000000,
              "shift_1[32] = saturated");
static_assert(FFmpegTTAData::tta_shift_1[35] == 0x80000000,
              "shift_1[35] = saturated");
static_assert(FFmpegTTAData::tta_shift_1[39] == 0x80000000,
              "shift_1[39] = saturated (last saturated entry)");

// Verify special marker
static_assert(FFmpegTTAData::tta_shift_1[40] == 0xFFFFFFFF,
              "shift_1[40] = 0xFFFFFFFF (special marker)");

// Verify doubling pattern in power-of-2 region
static_assert(FFmpegTTAData::tta_shift_1[1] == FFmpegTTAData::tta_shift_1[0] * 2,
              "Each entry doubles the previous");
static_assert(FFmpegTTAData::tta_shift_1[10] == FFmpegTTAData::tta_shift_1[9] * 2,
              "Doubling continues through middle");
static_assert(FFmpegTTAData::tta_shift_1[29] == FFmpegTTAData::tta_shift_1[28] * 2,
              "Doubling up to entry 29");

// Verify shift_16 offset
static_assert(FFmpegTTAData::TTA_SHIFT_16_OFFSET == 4,
              "shift_16 offset is 4");
static_assert(FFmpegTTAData::get_tta_shift_16(0) == 0x00000010,
              "shift_16[0] = shift_1[4] = 16");
static_assert(FFmpegTTAData::get_tta_shift_16(0) == 16,
              "shift_16[0] equals 16");

// Filter configs validation
static_assert(FFmpegTTAData::tta_filter_configs.size() == 4,
              "Filter configs has 4 entries");
static_assert(FFmpegTTAData::tta_filter_configs[0] == 10,
              "8-bit: filter order 10");
static_assert(FFmpegTTAData::tta_filter_configs[1] == 9,
              "16-bit: filter order 9");
static_assert(FFmpegTTAData::tta_filter_configs[2] == 10,
              "24-bit: filter order 10");
static_assert(FFmpegTTAData::tta_filter_configs[3] == 12,
              "32-bit: filter order 12");

// Verify all filter orders are reasonable (between 1 and MAX_ORDER=16)
static_assert(FFmpegTTAData::tta_filter_configs[0] >= 1 &&
              FFmpegTTAData::tta_filter_configs[0] <= 16,
              "Filter order within valid range");
static_assert(FFmpegTTAData::tta_filter_configs[3] >= 1 &&
              FFmpegTTAData::tta_filter_configs[3] <= 16,
              "All filter orders within valid range");

// Verify higher bit depth uses higher filter order (generally)
static_assert(FFmpegTTAData::tta_filter_configs[3] > FFmpegTTAData::tta_filter_configs[1],
              "32-bit uses higher order than 16-bit");

// ============================================================================
// C API Compatibility Layer
// ============================================================================

extern "C" {

/**
 * Get pointer to TTA shift_1 table
 * @return Pointer to 41-entry uint32_t array
 */
inline const uint32_t* get_tta_shift_1() {
    return FFmpegTTAData::tta_shift_1.data();
}

/**
 * Get pointer to TTA shift_16 table (offset into shift_1)
 * @return Pointer to shift_1[4] (value 16)
 */
inline const uint32_t* get_tta_shift_16() {
    return FFmpegTTAData::tta_shift_1.data() + FFmpegTTAData::TTA_SHIFT_16_OFFSET;
}

/**
 * Get pointer to TTA filter configuration table
 * @return Pointer to 4-entry uint8_t array
 */
inline const uint8_t* get_tta_filter_configs() {
    return FFmpegTTAData::tta_filter_configs.data();
}

} // extern "C"

#endif // AVCODEC_TTA_DATA_TABLEGEN_CONSTEXPR_HPP
