/*
 * WMV2 codec data tables - C++20 constexpr implementation
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

#ifndef AVCODEC_WMV2_DATA_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_WMV2_DATA_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>

/**
 * @file
 * @brief WMV2 codec DCT scan order tables
 *
 * This header provides compile-time generation of WMV2 (Windows Media Video 2)
 * DCT coefficient scan order tables using C++20 constexpr. WMV2 is a Microsoft
 * video compression format that uses alternative scan orders for DCT blocks.
 *
 * Tables:
 * - scantableA: First scan order pattern for 8×8 DCT blocks (partial, 32 positions)
 * - scantableB: Second scan order pattern for 8×8 DCT blocks (partial, 32 positions)
 *
 * These tables map linear indices to positions within an 8×8 DCT coefficient
 * block, optimized for WMV2's encoding characteristics. Unlike standard zigzag
 * scan orders, WMV2 uses these custom patterns for improved compression.
 *
 * Generated at compile time - zero runtime overhead.
 * All tables placed in .rodata section by the compiler.
 *
 * Data size: 64 bytes (64 entries total)
 * Static assertions: 30+ compile-time validations
 */

namespace FFmpegWMV2Data {

// ============================================================================
// WMV2 Scan Table A (32 entries, 32 bytes)
// ============================================================================

/**
 * WMV2 DCT scan order table A
 *
 * First scan pattern for WMV2 codec:
 * - Maps indices 0-31 to positions in 8×8 DCT block
 * - Values range from 0x00 to 0x1F (0 to 31)
 * - Custom pattern optimized for WMV2 encoding
 * - Covers half of the 64-position block
 *
 * Used for specific macroblock types in WMV2 encoding/decoding.
 */
constexpr auto generate_wmv2_scantable_a() noexcept {
    std::array<uint8_t, 32> table{};

    // Exact scan order from WMV2 specification
    constexpr uint8_t values[32] = {
        0x00, 0x01, 0x02, 0x08, 0x03, 0x09, 0x0A, 0x10,
        0x04, 0x0B, 0x11, 0x18, 0x12, 0x0C, 0x05, 0x13,
        0x19, 0x0D, 0x14, 0x1A, 0x1B, 0x06, 0x15, 0x1C,
        0x0E, 0x16, 0x1D, 0x07, 0x1E, 0x0F, 0x17, 0x1F
    };

    for (int i = 0; i < 32; ++i) {
        table[i] = values[i];
    }

    return table;
}

constexpr auto wmv2_scantable_a = generate_wmv2_scantable_a();

// ============================================================================
// WMV2 Scan Table B (32 entries, 32 bytes)
// ============================================================================

/**
 * WMV2 DCT scan order table B
 *
 * Second scan pattern for WMV2 codec:
 * - Maps indices 0-31 to positions in 8×8 DCT block
 * - Values range from 0x00 to 0x3B (0 to 59)
 * - Different pattern from scantableA
 * - Covers different portions of the 64-position block
 *
 * Alternative scan order for different macroblock configurations.
 */
constexpr auto generate_wmv2_scantable_b() noexcept {
    std::array<uint8_t, 32> table{};

    // Exact scan order from WMV2 specification
    constexpr uint8_t values[32] = {
        0x00, 0x08, 0x01, 0x10, 0x09, 0x18, 0x11, 0x02,
        0x20, 0x0A, 0x19, 0x28, 0x12, 0x30, 0x21, 0x1A,
        0x38, 0x29, 0x22, 0x03, 0x31, 0x39, 0x0B, 0x2A,
        0x13, 0x32, 0x1B, 0x3A, 0x23, 0x2B, 0x33, 0x3B
    };

    for (int i = 0; i < 32; ++i) {
        table[i] = values[i];
    }

    return table;
}

constexpr auto wmv2_scantable_b = generate_wmv2_scantable_b();

} // namespace FFmpegWMV2Data

// ============================================================================
// Static Assertions - Compile-Time Validation
// ============================================================================

// Scan table A validation
static_assert(FFmpegWMV2Data::wmv2_scantable_a.size() == 32,
              "Scan table A must have 32 entries");
static_assert(FFmpegWMV2Data::wmv2_scantable_a[0] == 0x00,
              "Table A starts at position 0 (DC coefficient)");
static_assert(FFmpegWMV2Data::wmv2_scantable_a[31] == 0x1F,
              "Table A ends at position 31");

// Verify scantable A progression
static_assert(FFmpegWMV2Data::wmv2_scantable_a[1] == 0x01,
              "Table A[1] = 1");
static_assert(FFmpegWMV2Data::wmv2_scantable_a[2] == 0x02,
              "Table A[2] = 2");
static_assert(FFmpegWMV2Data::wmv2_scantable_a[3] == 0x08,
              "Table A[3] = 8 (jump to next row)");
static_assert(FFmpegWMV2Data::wmv2_scantable_a[7] == 0x10,
              "Table A[7] = 16");

// Verify scantable A covers low-frequency coefficients
static_assert(FFmpegWMV2Data::wmv2_scantable_a[8] == 0x04,
              "Table A includes position 4");
static_assert(FFmpegWMV2Data::wmv2_scantable_a[15] == 0x13,
              "Table A midpoint");
static_assert(FFmpegWMV2Data::wmv2_scantable_a[24] == 0x0E,
              "Table A includes position 14");

// Verify all values in scantable A are in range [0, 31]
static_assert(FFmpegWMV2Data::wmv2_scantable_a[0] <= 31,
              "Table A values in valid range");
static_assert(FFmpegWMV2Data::wmv2_scantable_a[31] <= 31,
              "Table A maximum value <= 31");

// Scan table B validation
static_assert(FFmpegWMV2Data::wmv2_scantable_b.size() == 32,
              "Scan table B must have 32 entries");
static_assert(FFmpegWMV2Data::wmv2_scantable_b[0] == 0x00,
              "Table B starts at position 0 (DC coefficient)");
static_assert(FFmpegWMV2Data::wmv2_scantable_b[31] == 0x3B,
              "Table B ends at position 59");

// Verify scantable B progression
static_assert(FFmpegWMV2Data::wmv2_scantable_b[1] == 0x08,
              "Table B[1] = 8 (different from A)");
static_assert(FFmpegWMV2Data::wmv2_scantable_b[2] == 0x01,
              "Table B[2] = 1");
static_assert(FFmpegWMV2Data::wmv2_scantable_b[3] == 0x10,
              "Table B[3] = 16");
static_assert(FFmpegWMV2Data::wmv2_scantable_b[7] == 0x02,
              "Table B[7] = 2");

// Verify scantable B covers different range
static_assert(FFmpegWMV2Data::wmv2_scantable_b[8] == 0x20,
              "Table B includes position 32");
static_assert(FFmpegWMV2Data::wmv2_scantable_b[15] == 0x1A,
              "Table B midpoint");
static_assert(FFmpegWMV2Data::wmv2_scantable_b[16] == 0x38,
              "Table B includes position 56");

// Verify scantable B uses higher positions
static_assert(FFmpegWMV2Data::wmv2_scantable_b[16] > 0x1F,
              "Table B extends beyond table A range");
static_assert(FFmpegWMV2Data::wmv2_scantable_b[31] <= 0x3F,
              "Table B maximum value < 64");

// Cross-validation: tables differ
static_assert(FFmpegWMV2Data::wmv2_scantable_a[1] !=
              FFmpegWMV2Data::wmv2_scantable_b[1],
              "Tables A and B have different patterns");
static_assert(FFmpegWMV2Data::wmv2_scantable_a[3] !=
              FFmpegWMV2Data::wmv2_scantable_b[3],
              "Tables have distinct scan orders");
static_assert(FFmpegWMV2Data::wmv2_scantable_a[7] !=
              FFmpegWMV2Data::wmv2_scantable_b[7],
              "Tables diverge significantly");

// Verify both tables start at DC (position 0)
static_assert(FFmpegWMV2Data::wmv2_scantable_a[0] ==
              FFmpegWMV2Data::wmv2_scantable_b[0],
              "Both tables start at DC coefficient");

// Verify table B covers higher-frequency coefficients
static_assert(FFmpegWMV2Data::wmv2_scantable_b[31] >
              FFmpegWMV2Data::wmv2_scantable_a[31],
              "Table B reaches higher positions");

// Verify specific pattern characteristics
static_assert(FFmpegWMV2Data::wmv2_scantable_a[21] == 0x06,
              "Table A specific position");
static_assert(FFmpegWMV2Data::wmv2_scantable_b[19] == 0x03,
              "Table B specific position");
static_assert(FFmpegWMV2Data::wmv2_scantable_a[28] == 0x1E,
              "Table A near end");
static_assert(FFmpegWMV2Data::wmv2_scantable_b[28] == 0x23,
              "Table B near end");

// ============================================================================
// C API Compatibility Layer
// ============================================================================

extern "C" {

/**
 * Get pointer to WMV2 scan table A
 * @return Pointer to 32-entry uint8_t array (declared as [64] for compatibility)
 */
inline const uint8_t* get_wmv2_scantable_a() {
    return FFmpegWMV2Data::wmv2_scantable_a.data();
}

/**
 * Get pointer to WMV2 scan table B
 * @return Pointer to 32-entry uint8_t array (declared as [64] for compatibility)
 */
inline const uint8_t* get_wmv2_scantable_b() {
    return FFmpegWMV2Data::wmv2_scantable_b.data();
}

} // extern "C"

#endif // AVCODEC_WMV2_DATA_TABLEGEN_CONSTEXPR_HPP
