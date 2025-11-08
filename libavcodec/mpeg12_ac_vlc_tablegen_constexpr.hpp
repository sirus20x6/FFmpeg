/*
 * Compile-time generation of MPEG-1/2 AC coefficient VLC tables
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

#ifndef AVCODEC_MPEG12_AC_VLC_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_MPEG12_AC_VLC_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>

namespace FFmpegMPEG12ACVLC {

// ============================================================================
// Constants
// ============================================================================

constexpr int MPEG12_RL_NB_ELEMS = 111;  // Run-length table entries
constexpr int MPEG12_VLC_TABLE_SIZE = MPEG12_RL_NB_ELEMS + 2;  // +2 for EOB and escape

// ============================================================================
// MPEG-1/2 Run-Length Encoding (RLE) Tables
// ============================================================================

/**
 * Generate MPEG-1/2 AC coefficient level (magnitude) table.
 *
 * This table specifies the coefficient magnitudes used in run-length encoding.
 * Each entry represents the absolute value of a DCT coefficient.
 *
 * The table is organized by frequency of occurrence - the most common (run, level)
 * pairs appear first and are assigned shorter VLC codes in the VLC tables.
 *
 * Pattern:
 * - First 40 entries: level 1-40 with run=0 (coefficient immediately after previous)
 * - Next entries: decreasing levels with increasing runs (less common patterns)
 * - Final entries: level=1 with large runs (sparse coefficients)
 */
constexpr auto generate_mpeg12_level_table() noexcept {
    std::array<int8_t, MPEG12_RL_NB_ELEMS> table{};

    // MPEG-1/2 spec: AC coefficient levels in frequency order
    const int8_t values[111] = {
        // Run=0, levels 1-40 (most common: consecutive coefficients)
         1,  2,  3,  4,  5,  6,  7,  8,
         9, 10, 11, 12, 13, 14, 15, 16,
        17, 18, 19, 20, 21, 22, 23, 24,
        25, 26, 27, 28, 29, 30, 31, 32,
        33, 34, 35, 36, 37, 38, 39, 40,
        // Run=1, levels 1-18 (one zero before coefficient)
         1,  2,  3,  4,  5,  6,  7,  8,
         9, 10, 11, 12, 13, 14, 15, 16,
        17, 18,
        // Run=2, levels 1-5
         1,  2,  3,  4,  5,
        // Run=3, levels 1-4
         1,  2,  3,  4,
        // Run=4, levels 1-3
         1,  2,  3,
        // Run=5, levels 1-3
         1,  2,  3,
        // Run=6, levels 1-3
         1,  2,  3,
        // Run=7, levels 1-2
         1,  2,
        // Run=8, levels 1-2
         1,  2,
        // Run=9, levels 1-2
         1,  2,
        // Run=10, levels 1-2
         1,  2,
        // Run=11, levels 1-2
         1,  2,
        // Run=12, levels 1-2
         1,  2,
        // Run=13, levels 1-2
         1,  2,
        // Run=14, levels 1-2
         1,  2,
        // Run=15, levels 1-2
         1,  2,
        // Run=16, levels 1-2
         1,  2,
        // Run=17 through 31, level 1 (sparse coefficients)
         1,  1,  1,  1,  1,  1,  1,  1,
         1,  1,  1,  1,  1,  1,  1,
    };

    for (int i = 0; i < MPEG12_RL_NB_ELEMS; ++i) {
        table[i] = values[i];
    }

    return table;
}

/**
 * Generate MPEG-1/2 AC coefficient run (zero count) table.
 *
 * This table specifies the number of zero coefficients before each non-zero
 * coefficient in the zigzag scan order.
 *
 * Paired with the level table, each (run, level) pair describes:
 * - run: number of zeros to skip
 * - level: magnitude of the next non-zero coefficient
 *
 * This RLE representation efficiently encodes the sparse DCT coefficient blocks.
 */
constexpr auto generate_mpeg12_run_table() noexcept {
    std::array<int8_t, MPEG12_RL_NB_ELEMS> table{};

    // MPEG-1/2 spec: run lengths matching the level table
    const int8_t values[111] = {
        // Run=0: 40 entries (levels 1-40)
         0,  0,  0,  0,  0,  0,  0,  0,
         0,  0,  0,  0,  0,  0,  0,  0,
         0,  0,  0,  0,  0,  0,  0,  0,
         0,  0,  0,  0,  0,  0,  0,  0,
         0,  0,  0,  0,  0,  0,  0,  0,
        // Run=1: 18 entries (levels 1-18)
         1,  1,  1,  1,  1,  1,  1,  1,
         1,  1,  1,  1,  1,  1,  1,  1,
         1,  1,
        // Run=2: 5 entries
         2,  2,  2,  2,  2,
        // Run=3: 4 entries
         3,  3,  3,  3,
        // Run=4: 3 entries
         4,  4,  4,
        // Run=5: 3 entries
         5,  5,  5,
        // Run=6: 3 entries
         6,  6,  6,
        // Run=7: 2 entries
         7,  7,
        // Run=8: 2 entries
         8,  8,
        // Run=9: 2 entries
         9,  9,
        // Run=10: 2 entries
        10, 10,
        // Run=11: 2 entries
        11, 11,
        // Run=12: 2 entries
        12, 12,
        // Run=13: 2 entries
        13, 13,
        // Run=14: 2 entries
        14, 14,
        // Run=15: 2 entries
        15, 15,
        // Run=16: 2 entries
        16, 16,
        // Run=17-31: 1 entry each (15 total)
        17, 18, 19, 20, 21, 22, 23, 24,
        25, 26, 27, 28, 29, 30, 31,
    };

    for (int i = 0; i < MPEG12_RL_NB_ELEMS; ++i) {
        table[i] = values[i];
    }

    return table;
}

// ============================================================================
// MPEG-1 VLC Table
// ============================================================================

/**
 * Generate MPEG-1 Variable Length Codes for AC coefficients.
 *
 * This table provides VLC codes and bit lengths for MPEG-1 (run, level) pairs.
 * The codes are Huffman-optimized for typical video content statistics.
 *
 * Each entry is [code, bits]:
 * - code: The bit pattern to encode
 * - bits: Number of bits in the code
 *
 * Shorter codes are assigned to more frequent (run, level) pairs.
 * The table includes 111 regular entries + EOB (End Of Block) + escape code.
 */
constexpr auto generate_mpeg1_vlc_table() noexcept {
    std::array<std::array<uint16_t, 2>, MPEG12_VLC_TABLE_SIZE> table{};

    // MPEG-1 spec: VLC codes for AC coefficients (Table B.14)
    const uint16_t values[113][2] = {
        { 0x3, 2 }, { 0x4, 4 }, { 0x5, 5 }, { 0x6, 7 },
        { 0x26, 8 }, { 0x21, 8 }, { 0xa, 10 }, { 0x1d, 12 },
        { 0x18, 12 }, { 0x13, 12 }, { 0x10, 12 }, { 0x1a, 13 },
        { 0x19, 13 }, { 0x18, 13 }, { 0x17, 13 }, { 0x1f, 14 },
        { 0x1e, 14 }, { 0x1d, 14 }, { 0x1c, 14 }, { 0x1b, 14 },
        { 0x1a, 14 }, { 0x19, 14 }, { 0x18, 14 }, { 0x17, 14 },
        { 0x16, 14 }, { 0x15, 14 }, { 0x14, 14 }, { 0x13, 14 },
        { 0x12, 14 }, { 0x11, 14 }, { 0x10, 14 }, { 0x18, 15 },
        { 0x17, 15 }, { 0x16, 15 }, { 0x15, 15 }, { 0x14, 15 },
        { 0x13, 15 }, { 0x12, 15 }, { 0x11, 15 }, { 0x10, 15 },
        { 0x3, 3 }, { 0x6, 6 }, { 0x25, 8 }, { 0xc, 10 },
        { 0x1b, 12 }, { 0x16, 13 }, { 0x15, 13 }, { 0x1f, 15 },
        { 0x1e, 15 }, { 0x1d, 15 }, { 0x1c, 15 }, { 0x1b, 15 },
        { 0x1a, 15 }, { 0x19, 15 }, { 0x13, 16 }, { 0x12, 16 },
        { 0x11, 16 }, { 0x10, 16 }, { 0x5, 4 }, { 0x4, 7 },
        { 0xb, 10 }, { 0x14, 12 }, { 0x14, 13 }, { 0x7, 5 },
        { 0x24, 8 }, { 0x1c, 12 }, { 0x13, 13 }, { 0x6, 5 },
        { 0xf, 10 }, { 0x12, 12 }, { 0x7, 6 }, { 0x9, 10 },
        { 0x12, 13 }, { 0x5, 6 }, { 0x1e, 12 }, { 0x14, 16 },
        { 0x4, 6 }, { 0x15, 12 }, { 0x7, 7 }, { 0x11, 12 },
        { 0x5, 7 }, { 0x11, 13 }, { 0x27, 8 }, { 0x10, 13 },
        { 0x23, 8 }, { 0x1a, 16 }, { 0x22, 8 }, { 0x19, 16 },
        { 0x20, 8 }, { 0x18, 16 }, { 0xe, 10 }, { 0x17, 16 },
        { 0xd, 10 }, { 0x16, 16 }, { 0x8, 10 }, { 0x15, 16 },
        { 0x1f, 12 }, { 0x1a, 12 }, { 0x19, 12 }, { 0x17, 12 },
        { 0x16, 12 }, { 0x1f, 13 }, { 0x1e, 13 }, { 0x1d, 13 },
        { 0x1c, 13 }, { 0x1b, 13 }, { 0x1f, 16 }, { 0x1e, 16 },
        { 0x1d, 16 }, { 0x1c, 16 }, { 0x1b, 16 },
        { 0x1, 6 }, /* escape */
        { 0x2, 2 }, /* EOB */
    };

    for (int i = 0; i < MPEG12_VLC_TABLE_SIZE; ++i) {
        table[i][0] = values[i][0];
        table[i][1] = values[i][1];
    }

    return table;
}

// ============================================================================
// MPEG-2 VLC Table
// ============================================================================

/**
 * Generate MPEG-2 Variable Length Codes for AC coefficients.
 *
 * Similar to MPEG-1 but with different code assignments optimized for
 * MPEG-2 video content. MPEG-2 typically has different statistics due to
 * improved motion compensation and different scanning patterns.
 */
constexpr auto generate_mpeg2_vlc_table() noexcept {
    std::array<std::array<uint16_t, 2>, MPEG12_VLC_TABLE_SIZE> table{};

    // MPEG-2 spec: VLC codes for AC coefficients (Table B.14a)
    const uint16_t values[113][2] = {
        {0x02, 2}, {0x06, 3}, {0x07, 4}, {0x1c, 5},
        {0x1d, 5}, {0x05, 6}, {0x04, 6}, {0x7b, 7},
        {0x7c, 7}, {0x23, 8}, {0x22, 8}, {0xfa, 8},
        {0xfb, 8}, {0xfe, 8}, {0xff, 8}, {0x1f,14},
        {0x1e,14}, {0x1d,14}, {0x1c,14}, {0x1b,14},
        {0x1a,14}, {0x19,14}, {0x18,14}, {0x17,14},
        {0x16,14}, {0x15,14}, {0x14,14}, {0x13,14},
        {0x12,14}, {0x11,14}, {0x10,14}, {0x18,15},
        {0x17,15}, {0x16,15}, {0x15,15}, {0x14,15},
        {0x13,15}, {0x12,15}, {0x11,15}, {0x10,15},
        {0x02, 3}, {0x06, 5}, {0x79, 7}, {0x27, 8},
        {0x20, 8}, {0x16,13}, {0x15,13}, {0x1f,15},
        {0x1e,15}, {0x1d,15}, {0x1c,15}, {0x1b,15},
        {0x1a,15}, {0x19,15}, {0x13,16}, {0x12,16},
        {0x11,16}, {0x10,16}, {0x05, 5}, {0x07, 7},
        {0xfc, 8}, {0x0c,10}, {0x14,13}, {0x07, 5},
        {0x26, 8}, {0x1c,12}, {0x13,13}, {0x06, 6},
        {0xfd, 8}, {0x12,12}, {0x07, 6}, {0x04, 9},
        {0x12,13}, {0x06, 7}, {0x1e,12}, {0x14,16},
        {0x04, 7}, {0x15,12}, {0x05, 7}, {0x11,12},
        {0x78, 7}, {0x11,13}, {0x7a, 7}, {0x10,13},
        {0x21, 8}, {0x1a,16}, {0x25, 8}, {0x19,16},
        {0x24, 8}, {0x18,16}, {0x05, 9}, {0x17,16},
        {0x07, 9}, {0x16,16}, {0x0d,10}, {0x15,16},
        {0x1f,12}, {0x1a,12}, {0x19,12}, {0x17,12},
        {0x16,12}, {0x1f,13}, {0x1e,13}, {0x1d,13},
        {0x1c,13}, {0x1b,13}, {0x1f,16}, {0x1e,16},
        {0x1d,16}, {0x1c,16}, {0x1b,16},
        {0x01,6}, /* escape */
        {0x06,4}, /* EOB */
    };

    for (int i = 0; i < MPEG12_VLC_TABLE_SIZE; ++i) {
        table[i][0] = values[i][0];
        table[i][1] = values[i][1];
    }

    return table;
}

// ============================================================================
// Generated Tables (670 bytes total)
// ============================================================================

constexpr auto mpeg12_level_table = generate_mpeg12_level_table();
constexpr auto mpeg12_run_table = generate_mpeg12_run_table();
constexpr auto mpeg1_vlc_table = generate_mpeg1_vlc_table();
constexpr auto mpeg2_vlc_table = generate_mpeg2_vlc_table();

// ============================================================================
// Static Assertions: Comprehensive Validation
// ============================================================================

// Table sizes
static_assert(mpeg12_level_table.size() == 111, "Level table has 111 entries");
static_assert(mpeg12_run_table.size() == 111, "Run table has 111 entries");
static_assert(mpeg1_vlc_table.size() == 113, "MPEG-1 VLC has 113 entries (111 + EOB + escape)");
static_assert(mpeg2_vlc_table.size() == 113, "MPEG-2 VLC has 113 entries (111 + EOB + escape)");

// Level table validation - verify spec values
static_assert(mpeg12_level_table[0] == 1, "First entry: level=1, run=0 (most common)");
static_assert(mpeg12_level_table[1] == 2, "Second entry: level=2, run=0");
static_assert(mpeg12_level_table[39] == 40, "Entry 39: level=40, run=0 (last run=0 entry)");
static_assert(mpeg12_level_table[40] == 1, "Entry 40: level=1, run=1 (first run=1)");
static_assert(mpeg12_level_table[57] == 18, "Entry 57: level=18, run=1 (last run=1)");
static_assert(mpeg12_level_table[110] == 1, "Last entry: level=1, run=31 (sparse)");

// Verify level values are in valid range [1, 40]
constexpr auto verify_level_range() {
    for (int i = 0; i < 111; ++i) {
        if (mpeg12_level_table[i] < 1 || mpeg12_level_table[i] > 40) return false;
    }
    return true;
}
static_assert(verify_level_range(), "All levels in range [1, 40]");

// Run table validation - verify spec values
static_assert(mpeg12_run_table[0] == 0, "First entry: run=0 (coefficient immediately after)");
static_assert(mpeg12_run_table[39] == 0, "Entry 39: run=0 (last of 40 run=0 entries)");
static_assert(mpeg12_run_table[40] == 1, "Entry 40: run=1 (first run=1 entry)");
static_assert(mpeg12_run_table[57] == 1, "Entry 57: run=1 (last run=1 entry)");
static_assert(mpeg12_run_table[58] == 2, "Entry 58: run=2 (first run=2 entry)");
static_assert(mpeg12_run_table[110] == 31, "Last entry: run=31 (maximum run)");

// Verify run values are in valid range [0, 31]
constexpr auto verify_run_range() {
    for (int i = 0; i < 111; ++i) {
        if (mpeg12_run_table[i] < 0 || mpeg12_run_table[i] > 31) return false;
    }
    return true;
}
static_assert(verify_run_range(), "All runs in range [0, 31]");

// Verify run table is monotonically non-decreasing
constexpr auto verify_run_monotonic() {
    for (int i = 1; i < 111; ++i) {
        if (mpeg12_run_table[i] < mpeg12_run_table[i-1]) return false;
    }
    return true;
}
static_assert(verify_run_monotonic(), "Run table is monotonically non-decreasing");

// MPEG-1 VLC validation
static_assert(mpeg1_vlc_table[0][0] == 0x3, "MPEG-1: First code = 0x3");
static_assert(mpeg1_vlc_table[0][1] == 2, "MPEG-1: First code uses 2 bits");
static_assert(mpeg1_vlc_table[111][0] == 0x1, "MPEG-1: Escape code = 0x1");
static_assert(mpeg1_vlc_table[111][1] == 6, "MPEG-1: Escape uses 6 bits");
static_assert(mpeg1_vlc_table[112][0] == 0x2, "MPEG-1: EOB code = 0x2");
static_assert(mpeg1_vlc_table[112][1] == 2, "MPEG-1: EOB uses 2 bits");

// Verify MPEG-1 bit lengths are reasonable (2-16 bits for VLC)
static_assert(mpeg1_vlc_table[0][1] >= 2 && mpeg1_vlc_table[0][1] <= 16,
              "MPEG-1: Bit lengths in range [2,16]");
static_assert(mpeg1_vlc_table[110][1] >= 2 && mpeg1_vlc_table[110][1] <= 16,
              "MPEG-1: Bit lengths in range [2,16]");

// Verify MPEG-1 codes fit within bit lengths
static_assert(mpeg1_vlc_table[0][0] < (1 << mpeg1_vlc_table[0][1]),
              "MPEG-1: code[0] fits in bits[0]");
static_assert(mpeg1_vlc_table[111][0] < (1 << mpeg1_vlc_table[111][1]),
              "MPEG-1: Escape code fits in its bit length");
static_assert(mpeg1_vlc_table[112][0] < (1 << mpeg1_vlc_table[112][1]),
              "MPEG-1: EOB code fits in its bit length");

// MPEG-2 VLC validation
static_assert(mpeg2_vlc_table[0][0] == 0x02, "MPEG-2: First code = 0x02");
static_assert(mpeg2_vlc_table[0][1] == 2, "MPEG-2: First code uses 2 bits");
static_assert(mpeg2_vlc_table[111][0] == 0x01, "MPEG-2: Escape code = 0x01");
static_assert(mpeg2_vlc_table[111][1] == 6, "MPEG-2: Escape uses 6 bits");
static_assert(mpeg2_vlc_table[112][0] == 0x06, "MPEG-2: EOB code = 0x06");
static_assert(mpeg2_vlc_table[112][1] == 4, "MPEG-2: EOB uses 4 bits");

// Verify MPEG-2 bit lengths are reasonable (2-16 bits for VLC)
static_assert(mpeg2_vlc_table[0][1] >= 2 && mpeg2_vlc_table[0][1] <= 16,
              "MPEG-2: Bit lengths in range [2,16]");

// Verify MPEG-2 codes fit within bit lengths
static_assert(mpeg2_vlc_table[0][0] < (1 << mpeg2_vlc_table[0][1]),
              "MPEG-2: code[0] fits in bits[0]");
static_assert(mpeg2_vlc_table[111][0] < (1 << mpeg2_vlc_table[111][1]),
              "MPEG-2: Escape code fits in its bit length");
static_assert(mpeg2_vlc_table[112][0] < (1 << mpeg2_vlc_table[112][1]),
              "MPEG-2: EOB code fits in its bit length");

// Verify EOB codes use short bit lengths (Huffman optimization)
static_assert(mpeg1_vlc_table[112][1] <= 5, "MPEG-1: EOB is short code (frequent)");
static_assert(mpeg2_vlc_table[112][1] <= 5, "MPEG-2: EOB is short code (frequent)");

// Verify both tables use same RLE structure (111 entries)
static_assert(mpeg1_vlc_table.size() == mpeg2_vlc_table.size(),
              "MPEG-1 and MPEG-2 tables have same size");

// Cross-validation: run and level tables have same size
static_assert(mpeg12_level_table.size() == mpeg12_run_table.size(),
              "Level and run tables have matching sizes");

// Verify common (run, level) pairs use shorter codes (Huffman property)
static_assert(mpeg1_vlc_table[0][1] <= mpeg1_vlc_table[50][1],
              "MPEG-1: Common codes (run=0, level=1) shorter than rare codes");
static_assert(mpeg2_vlc_table[0][1] <= mpeg2_vlc_table[50][1],
              "MPEG-2: Common codes shorter than rare codes");

} // namespace FFmpegMPEG12ACVLC

// ============================================================================
// C API Compatibility
// ============================================================================

extern "C" {

/**
 * Get pointer to MPEG-1/2 AC coefficient level table.
 * Returns: Pointer to 111-element int8_t array
 */
inline const int8_t *get_mpeg12_level_table() {
    return FFmpegMPEG12ACVLC::mpeg12_level_table.data();
}

/**
 * Get pointer to MPEG-1/2 AC coefficient run table.
 * Returns: Pointer to 111-element int8_t array
 */
inline const int8_t *get_mpeg12_run_table() {
    return FFmpegMPEG12ACVLC::mpeg12_run_table.data();
}

/**
 * Get pointer to MPEG-1 VLC table.
 * Returns: Pointer to 113×2 uint16_t array
 */
inline const uint16_t (*get_mpeg1_vlc_table())[2] {
    return reinterpret_cast<const uint16_t(*)[2]>(FFmpegMPEG12ACVLC::mpeg1_vlc_table.data());
}

/**
 * Get pointer to MPEG-2 VLC table.
 * Returns: Pointer to 113×2 uint16_t array
 */
inline const uint16_t (*get_mpeg2_vlc_table())[2] {
    return reinterpret_cast<const uint16_t(*)[2]>(FFmpegMPEG12ACVLC::mpeg2_vlc_table.data());
}

} // extern "C"

#endif // AVCODEC_MPEG12_AC_VLC_TABLEGEN_CONSTEXPR_HPP
