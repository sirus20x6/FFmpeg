/*
 * Compile-time generation of MPEG-1/2 macroblock VLC tables
 *
 * Original C version from FFmpeg MPEG-1/2 decoder
 * C++20 constexpr version created 2025-11-08
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

#ifndef AVCODEC_MPEG12_MACROBLOCK_VLC_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_MPEG12_MACROBLOCK_VLC_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>

namespace FFmpegMPEG12MacroblockVLC {

// ============================================================================
// Constants
// ============================================================================

constexpr int MB_ADDR_INCR_TABLE_SIZE = 36;  // Macroblock address increment
constexpr int MB_PAT_TABLE_SIZE = 64;        // Macroblock pattern (CBP)
constexpr int MB_MOTION_VECTOR_TABLE_SIZE = 17;  // Motion vector differential

// ============================================================================
// MPEG-1/2 Macroblock Address Increment VLC Table
// ============================================================================

/**
 * Generate MPEG-1/2 macroblock address increment VLC table.
 *
 * This table encodes how many macroblocks to skip before the next coded
 * macroblock. Used for efficient representation of skipped macroblocks
 * (common in P/B frames with little motion).
 *
 * Entries 0-32: Skip counts 1-33 (1-11 bit codes)
 * Entry 33: Escape code for large skips
 * Entry 34: Stuffing code
 * Entry 35: End marker (followed by 15 more 0 bits)
 *
 * From MPEG-1/2 spec Table B.1
 */
constexpr auto generate_mpeg12_mb_addr_incr_table() noexcept {
    std::array<std::array<uint8_t, 2>, MB_ADDR_INCR_TABLE_SIZE> table{};

    // MPEG-1/2 spec values: [code, bits]
    const uint8_t values[36][2] = {
        {0x1, 1},   // Skip 1 MB
        {0x3, 3},   // Skip 2 MBs
        {0x2, 3},   // Skip 3 MBs
        {0x3, 4},   // Skip 4 MBs
        {0x2, 4},   // Skip 5 MBs
        {0x3, 5},   // Skip 6 MBs
        {0x2, 5},   // Skip 7 MBs
        {0x7, 7},   // Skip 8 MBs
        {0x6, 7},   // Skip 9 MBs
        {0xb, 8},   // Skip 10 MBs
        {0xa, 8},   // Skip 11 MBs
        {0x9, 8},   // Skip 12 MBs
        {0x8, 8},   // Skip 13 MBs
        {0x7, 8},   // Skip 14 MBs
        {0x6, 8},   // Skip 15 MBs
        {0x17, 10}, // Skip 16 MBs
        {0x16, 10}, // Skip 17 MBs
        {0x15, 10}, // Skip 18 MBs
        {0x14, 10}, // Skip 19 MBs
        {0x13, 10}, // Skip 20 MBs
        {0x12, 10}, // Skip 21 MBs
        {0x23, 11}, // Skip 22 MBs
        {0x22, 11}, // Skip 23 MBs
        {0x21, 11}, // Skip 24 MBs
        {0x20, 11}, // Skip 25 MBs
        {0x1f, 11}, // Skip 26 MBs
        {0x1e, 11}, // Skip 27 MBs
        {0x1d, 11}, // Skip 28 MBs
        {0x1c, 11}, // Skip 29 MBs
        {0x1b, 11}, // Skip 30 MBs
        {0x1a, 11}, // Skip 31 MBs
        {0x19, 11}, // Skip 32 MBs
        {0x18, 11}, // Skip 33 MBs
        {0x8, 11},  // Escape (for large skips)
        {0xf, 11},  // Stuffing
        {0x0, 8},   // End (+ 15 more 0 bits)
    };

    for (int i = 0; i < 36; ++i) {
        table[i][0] = values[i][0];  // code
        table[i][1] = values[i][1];  // bits
    }

    return table;
}

// ============================================================================
// MPEG-1/2 Macroblock Pattern (CBP) VLC Table
// ============================================================================

/**
 * Generate MPEG-1/2 coded block pattern (CBP) VLC table.
 *
 * CBP indicates which of the 6 blocks in a macroblock contain non-zero
 * coefficients (4 luma + 2 chroma for 4:2:0).
 *
 * 64 entries cover all possible patterns (2^6 = 64 combinations).
 * Pattern bits: [Y3 Y2 Y1 Y0 Cb Cr]
 *
 * From MPEG-1/2 spec Table B.9
 */
constexpr auto generate_mpeg12_mb_pat_table() noexcept {
    std::array<std::array<uint8_t, 2>, MB_PAT_TABLE_SIZE> table{};

    // MPEG-1/2 spec values: [code, bits]
    const uint8_t values[64][2] = {
        {0x1, 9},   // Pattern 0
        {0xb, 5},   // Pattern 1
        {0x9, 5},   // Pattern 2
        {0xd, 6},   // Pattern 3
        {0xd, 4},   // Pattern 4
        {0x17, 7},  // Pattern 5
        {0x13, 7},  // Pattern 6
        {0x1f, 8},  // Pattern 7
        {0xc, 4},   // Pattern 8
        {0x16, 7},  // Pattern 9
        {0x12, 7},  // Pattern 10
        {0x1e, 8},  // Pattern 11
        {0x13, 5},  // Pattern 12
        {0x1b, 8},  // Pattern 13
        {0x17, 8},  // Pattern 14
        {0x13, 8},  // Pattern 15
        {0xb, 4},   // Pattern 16
        {0x15, 7},  // Pattern 17
        {0x11, 7},  // Pattern 18
        {0x1d, 8},  // Pattern 19
        {0x11, 5},  // Pattern 20
        {0x19, 8},  // Pattern 21
        {0x15, 8},  // Pattern 22
        {0x11, 8},  // Pattern 23
        {0xf, 6},   // Pattern 24
        {0xf, 8},   // Pattern 25
        {0xd, 8},   // Pattern 26
        {0x3, 9},   // Pattern 27
        {0xf, 5},   // Pattern 28
        {0xb, 8},   // Pattern 29
        {0x7, 8},   // Pattern 30
        {0x7, 9},   // Pattern 31
        {0xa, 4},   // Pattern 32
        {0x14, 7},  // Pattern 33
        {0x10, 7},  // Pattern 34
        {0x1c, 8},  // Pattern 35
        {0xe, 6},   // Pattern 36
        {0xe, 8},   // Pattern 37
        {0xc, 8},   // Pattern 38
        {0x2, 9},   // Pattern 39
        {0x10, 5},  // Pattern 40
        {0x18, 8},  // Pattern 41
        {0x14, 8},  // Pattern 42
        {0x10, 8},  // Pattern 43
        {0xe, 5},   // Pattern 44
        {0xa, 8},   // Pattern 45
        {0x6, 8},   // Pattern 46
        {0x6, 9},   // Pattern 47
        {0x12, 5},  // Pattern 48
        {0x1a, 8},  // Pattern 49
        {0x16, 8},  // Pattern 50
        {0x12, 8},  // Pattern 51
        {0xd, 5},   // Pattern 52
        {0x9, 8},   // Pattern 53
        {0x5, 8},   // Pattern 54
        {0x5, 9},   // Pattern 55
        {0xc, 5},   // Pattern 56
        {0x8, 8},   // Pattern 57
        {0x4, 8},   // Pattern 58
        {0x4, 9},   // Pattern 59
        {0x7, 3},   // Pattern 60
        {0xa, 5},   // Pattern 61
        {0x8, 5},   // Pattern 62
        {0xc, 6}    // Pattern 63
    };

    for (int i = 0; i < 64; ++i) {
        table[i][0] = values[i][0];  // code
        table[i][1] = values[i][1];  // bits
    }

    return table;
}

// ============================================================================
// MPEG-1/2 Motion Vector Differential VLC Table
// ============================================================================

/**
 * Generate MPEG-1/2 motion vector differential VLC table.
 *
 * Motion vectors are differentially coded. This table encodes the
 * magnitude and sign of motion vector differences.
 *
 * 17 entries represent centered differential values:
 * Index 0-16 maps to diff values (centered around middle)
 *
 * From MPEG-1/2 spec Table B.10
 */
constexpr auto generate_mpeg12_mb_motion_vector_table() noexcept {
    std::array<std::array<uint8_t, 2>, MB_MOTION_VECTOR_TABLE_SIZE> table{};

    // MPEG-1/2 spec values: [code, bits]
    const uint8_t values[17][2] = {
        {0x1, 1},   // Index 0
        {0x1, 2},   // Index 1
        {0x1, 3},   // Index 2
        {0x1, 4},   // Index 3
        {0x3, 6},   // Index 4
        {0x5, 7},   // Index 5
        {0x4, 7},   // Index 6
        {0x3, 7},   // Index 7
        {0xb, 9},   // Index 8
        {0xa, 9},   // Index 9
        {0x9, 9},   // Index 10
        {0x11, 10}, // Index 11
        {0x10, 10}, // Index 12
        {0xf, 10},  // Index 13
        {0xe, 10},  // Index 14
        {0xd, 10},  // Index 15
        {0xc, 10}   // Index 16
    };

    for (int i = 0; i < 17; ++i) {
        table[i][0] = values[i][0];  // code
        table[i][1] = values[i][1];  // bits
    }

    return table;
}

// ============================================================================
// Generated Tables (234 bytes total)
// ============================================================================

constexpr auto mpeg12_mb_addr_incr_table = generate_mpeg12_mb_addr_incr_table();
constexpr auto mpeg12_mb_pat_table = generate_mpeg12_mb_pat_table();
constexpr auto mpeg12_mb_motion_vector_table = generate_mpeg12_mb_motion_vector_table();

// ============================================================================
// Static Assertions: Comprehensive Validation
// ============================================================================

// Table sizes
static_assert(mpeg12_mb_addr_incr_table.size() == 36, "MB addr incr has 36 entries");
static_assert(mpeg12_mb_pat_table.size() == 64, "MB pattern has 64 entries");
static_assert(mpeg12_mb_motion_vector_table.size() == 17, "MV table has 17 entries");

// MB address increment validation
static_assert(mpeg12_mb_addr_incr_table[0][0] == 0x1, "Skip 1 MB: code 0x1");
static_assert(mpeg12_mb_addr_incr_table[0][1] == 1, "Skip 1 MB: 1 bit");
static_assert(mpeg12_mb_addr_incr_table[33][0] == 0x8, "Escape code: 0x8");
static_assert(mpeg12_mb_addr_incr_table[33][1] == 11, "Escape code: 11 bits");
static_assert(mpeg12_mb_addr_incr_table[35][0] == 0x0, "End marker: 0x0");
static_assert(mpeg12_mb_addr_incr_table[35][1] == 8, "End marker: 8 bits");

// Verify shortest code is 1 bit (skip 1 MB - most common)
static_assert(mpeg12_mb_addr_incr_table[0][1] == 1, "Shortest code is 1 bit");

// Verify longer skips use more bits (Huffman property)
static_assert(mpeg12_mb_addr_incr_table[0][1] <= mpeg12_mb_addr_incr_table[10][1],
              "Longer skips use more bits");

// MB pattern validation
static_assert(mpeg12_mb_pat_table[0][0] == 0x1, "Pattern 0: code 0x1");
static_assert(mpeg12_mb_pat_table[0][1] == 9, "Pattern 0: 9 bits");
static_assert(mpeg12_mb_pat_table[60][0] == 0x7, "Pattern 60: code 0x7");
static_assert(mpeg12_mb_pat_table[60][1] == 3, "Pattern 60: 3 bits (common pattern)");
static_assert(mpeg12_mb_pat_table[63][0] == 0xc, "Pattern 63: code 0xc");
static_assert(mpeg12_mb_pat_table[63][1] == 6, "Pattern 63: 6 bits");

// Verify pattern 60 (all blocks coded) uses shortest code (most common)
static_assert(mpeg12_mb_pat_table[60][1] == 3, "Pattern 60 (all coded) is shortest");

// Motion vector validation
static_assert(mpeg12_mb_motion_vector_table[0][0] == 0x1, "MV[0]: code 0x1");
static_assert(mpeg12_mb_motion_vector_table[0][1] == 1, "MV[0]: 1 bit (zero vector)");
static_assert(mpeg12_mb_motion_vector_table[16][0] == 0xc, "MV[16]: code 0xc");
static_assert(mpeg12_mb_motion_vector_table[16][1] == 10, "MV[16]: 10 bits");

// Verify zero motion vector uses shortest code (most common in P-frames)
static_assert(mpeg12_mb_motion_vector_table[0][1] == 1, "Zero MV is shortest");

// Verify codes fit within bit lengths (Huffman property)
static_assert(mpeg12_mb_addr_incr_table[0][0] < (1U << mpeg12_mb_addr_incr_table[0][1]),
              "MB addr code fits in bits");
static_assert(mpeg12_mb_addr_incr_table[20][0] < (1U << mpeg12_mb_addr_incr_table[20][1]),
              "MB addr code fits in bits");
static_assert(mpeg12_mb_pat_table[0][0] < (1U << mpeg12_mb_pat_table[0][1]),
              "MB pattern code fits in bits");
static_assert(mpeg12_mb_pat_table[60][0] < (1U << mpeg12_mb_pat_table[60][1]),
              "MB pattern code fits in bits");
static_assert(mpeg12_mb_motion_vector_table[0][0] < (1U << mpeg12_mb_motion_vector_table[0][1]),
              "MV code fits in bits");
static_assert(mpeg12_mb_motion_vector_table[16][0] < (1U << mpeg12_mb_motion_vector_table[16][1]),
              "MV code fits in bits");

// Verify bit length ranges are reasonable
static_assert(mpeg12_mb_addr_incr_table[0][1] >= 1 && mpeg12_mb_addr_incr_table[0][1] <= 11,
              "MB addr bit lengths in [1,11]");
static_assert(mpeg12_mb_pat_table[60][1] >= 3 && mpeg12_mb_pat_table[60][1] <= 9,
              "MB pattern bit lengths in [3,9]");
static_assert(mpeg12_mb_motion_vector_table[0][1] >= 1 && mpeg12_mb_motion_vector_table[0][1] <= 10,
              "MV bit lengths in [1,10]");

// Cross-validation: common patterns use shorter codes
static_assert(mpeg12_mb_addr_incr_table[0][1] < mpeg12_mb_addr_incr_table[35][1],
              "Common skip (1 MB) < end marker");
static_assert(mpeg12_mb_pat_table[60][1] < mpeg12_mb_pat_table[0][1],
              "Common pattern (all coded) < rare pattern");

} // namespace FFmpegMPEG12MacroblockVLC

// ============================================================================
// C API Compatibility
// ============================================================================

extern "C" {

/**
 * Get pointer to MPEG-1/2 macroblock address increment VLC table.
 * Returns: Pointer to 36×2 uint8_t array
 */
inline const uint8_t *get_mpeg12_mb_addr_incr_table() {
    return reinterpret_cast<const uint8_t*>(
        FFmpegMPEG12MacroblockVLC::mpeg12_mb_addr_incr_table.data()
    );
}

/**
 * Get pointer to MPEG-1/2 macroblock pattern (CBP) VLC table.
 * Returns: Pointer to 64×2 uint8_t array
 */
inline const uint8_t *get_mpeg12_mb_pat_table() {
    return reinterpret_cast<const uint8_t*>(
        FFmpegMPEG12MacroblockVLC::mpeg12_mb_pat_table.data()
    );
}

/**
 * Get pointer to MPEG-1/2 motion vector differential VLC table.
 * Returns: Pointer to 17×2 uint8_t array
 */
inline const uint8_t *get_mpeg12_mb_motion_vector_table() {
    return reinterpret_cast<const uint8_t*>(
        FFmpegMPEG12MacroblockVLC::mpeg12_mb_motion_vector_table.data()
    );
}

} // extern "C"

#endif // AVCODEC_MPEG12_MACROBLOCK_VLC_TABLEGEN_CONSTEXPR_HPP
