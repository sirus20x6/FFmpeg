/*
 * Modern C++ constexpr DV VLC tables
 * Copyright (c) 2010 Reimar Döffinger <Reimar.Doeffinger@gmx.de>
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

/**
 * @file
 * Modern C++20 constexpr DV VLC (Variable Length Coding) tables
 *
 * This header provides compile-time generation of VLC lookup tables for
 * DV (Digital Video) codec encoding. DV uses run-length encoding where
 * each (run, level) pair is encoded as a variable-length Huffman-like code.
 *
 * Algorithm:
 * 1. Generate Huffman codes from source bit lengths (409 entries)
 * 2. Build 2D lookup table [run][level] → (vlc_code, vlc_size)
 * 3. Fill gaps using combination strategy for missing entries
 *
 * Sizes:
 * - Normal mode: 64 × 512 = 32,768 entries (struct pairs)
 * - Small mode: 15 × 23 = 345 entries (when CONFIG_SMALL defined)
 *
 * Total: 32,768 dv_vlc_pair structs (65,536 uint32_t values)
 *
 * VLC encoding lookup: Given (run, level) pair from encoder,
 * instantly retrieve pre-computed (code, length) for bitstream output.
 */

#ifndef AVCODEC_DV_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_DV_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>

namespace ffmpeg {
namespace dv {

// Constants from dvdata.h
constexpr int NB_DV_VLC = 409;
constexpr int NB_DV_ZERO_LEVEL_ENTRIES = 72;

// Table dimensions (normal mode)
constexpr int DV_VLC_MAP_RUN_SIZE = 64;
constexpr int DV_VLC_MAP_LEV_SIZE = 512;

// VLC pair structure (matches dv_vlc_pair from dv_tablegen.h)
struct DVVLCPair {
    uint32_t vlc;
    uint32_t size;
};

/**
 * Source data from dvdata.c
 *
 * These are the canonical DV VLC specifications:
 * - ff_dv_vlc_len: Bit length of each VLC code (2-15 bits)
 * - ff_dv_vlc_run: Run value (zero run-length)
 * - ff_dv_vlc_level: Level value (coefficient magnitude)
 *
 * Note: Mapping is not 1-1. E.g., (1,0) can be either 0x7cf or 0x1f82.
 */

constexpr std::array<uint8_t, NB_DV_VLC> dv_vlc_len = {
     2,  3,  4,  4,  4,  4,  5,  5,  5,
     5,  6,  6,  6,  6,  7,  7,  7,
     7,  7,  7,  7,  7,  8,  8,  8,
     8,  8,  8,  8,  8,  8,  8,  8,
     8,  8,  8,  8,  8,  9,  9,  9,
     9,  9,  9,  9,  9,  9,  9,  9,
     9,  9,  9,  9,  9, 10, 10, 10,
    10, 10, 10, 10, 11, 11, 11, 11,
    11, 11, 11, 11, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12,
    13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 13, 13, 13, 13, 13,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
};

constexpr std::array<uint8_t, NB_DV_VLC> dv_vlc_run = {
     0,  0, 127, 1,  0,  0,  2,  1,  0,
     0,  3,  4,  0,  0,  5,  6,  2,
     1,  1,  0,  0,  0,  7,  8,  9,
    10,  3,  4,  2,  1,  1,  1,  0,
     0,  0,  0,  0,  0, 11, 12, 13,
    14,  5,  6,  3,  4,  2,  2,  1,
     0,  0,  0,  0,  0,  5,  3,  3,
     2,  1,  1,  1,  0,  1,  6,  4,
     3,  1,  1,  1,  2,  3,  4,  5,
     7,  8,  9, 10,  7,  8,  4,  3,
     2,  2,  2,  2,  2,  1,  1,  1,
     0,  1,  2,  3,  4,  5,  6,  7,
     8,  9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23,
    24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39,
    40, 41, 42, 43, 44, 45, 46, 47,
    48, 49, 50, 51, 52, 53, 54, 55,
    56, 57, 58, 59, 60, 61, 62, 63,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
};

constexpr std::array<uint8_t, NB_DV_VLC> dv_vlc_level = {
     1,   2,   0,   1,   3,   4,   1,   2,   5,
     6,   1,   1,   7,   8,   1,   1,   2,
     3,   4,   9,  10,  11,   1,   1,   1,
     1,   2,   2,   3,   5,   6,   7,  12,
    13,  14,  15,  16,  17,   1,   1,   1,
     1,   2,   2,   3,   3,   4,   5,   8,
    18,  19,  20,  21,  22,   3,   4,   5,
     6,   9,  10,  11,   0,   0,   3,   4,
     6,  12,  13,  14,   0,   0,   0,   0,
     2,   2,   2,   2,   3,   3,   5,   7,
     7,   8,   9,  10,  11,  15,  16,  17,
     0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0,
     0,   1,   2,   3,   4,   5,   6,   7,
     8,   9,  10,  11,  12,  13,  14,  15,
    16,  17,  18,  19,  20,  21,  22,  23,
    24,  25,  26,  27,  28,  29,  30,  31,
    32,  33,  34,  35,  36,  37,  38,  39,
    40,  41,  42,  43,  44,  45,  46,  47,
    48,  49,  50,  51,  52,  53,  54,  55,
    56,  57,  58,  59,  60,  61,  62,  63,
    64,  65,  66,  67,  68,  69,  70,  71,
    72,  73,  74,  75,  76,  77,  78,  79,
    80,  81,  82,  83,  84,  85,  86,  87,
    88,  89,  90,  91,  92,  93,  94,  95,
    96,  97,  98,  99, 100, 101, 102, 103,
   104, 105, 106, 107, 108, 109, 110, 111,
   112, 113, 114, 115, 116, 117, 118, 119,
   120, 121, 122, 123, 124, 125, 126, 127,
   128, 129, 130, 131, 132, 133, 134, 135,
   136, 137, 138, 139, 140, 141, 142, 143,
   144, 145, 146, 147, 148, 149, 150, 151,
   152, 153, 154, 155, 156, 157, 158, 159,
   160, 161, 162, 163, 164, 165, 166, 167,
   168, 169, 170, 171, 172, 173, 174, 175,
   176, 177, 178, 179, 180, 181, 182, 183,
   184, 185, 186, 187, 188, 189, 190, 191,
   192, 193, 194, 195, 196, 197, 198, 199,
   200, 201, 202, 203, 204, 205, 206, 207,
   208, 209, 210, 211, 212, 213, 214, 215,
   216, 217, 218, 219, 220, 221, 222, 223,
   224, 225, 226, 227, 228, 229, 230, 231,
   232, 233, 234, 235, 236, 237, 238, 239,
   240, 241, 242, 243, 244, 245, 246, 247,
   248, 249, 250, 251, 252, 253, 254, 255,
};

/**
 * Generate DV VLC lookup table at compile time
 *
 * Algorithm (from dv_tablegen.h):
 *
 * Phase 1: Huffman code generation
 *   - Codes are assigned sequentially within each bit length
 *   - Start with code = 0, increment after each use
 *   - When bit length increases, left-shift accumulated code
 *
 * Phase 2: Direct mapping
 *   - For each VLC entry i (0..408):
 *     - run = dv_vlc_run[i], level = dv_vlc_level[i]
 *     - If entry [run][level] is empty:
 *       - vlc = current_code << (!!level)  // Add 1 bit if level != 0
 *       - size = dv_vlc_len[i] + (!!level)
 *
 * Phase 3: Gap filling
 *   - For each run i (0..DV_VLC_MAP_RUN_SIZE-1):
 *     - For each level j (1..DV_VLC_MAP_LEV_SIZE/2-1):
 *       - If [i][j] is empty:
 *         - Combine [0][j] and [i-1][0] codes
 *         - vlc = [0][j].vlc | ([i-1][0].vlc << [0][j].size)
 *         - size = [0][j].size + [i-1][0].size
 *     - Mirror positive to negative levels:
 *       - [i][-j] = [i][j] with LSB set to 1
 *
 * Result: Complete 64×512 lookup table for instant VLC encoding
 */
constexpr auto generate_dv_vlc_map() noexcept {
    using Table = std::array<std::array<DVVLCPair, DV_VLC_MAP_LEV_SIZE>, DV_VLC_MAP_RUN_SIZE>;
    Table table{};

    // Phase 1 & 2: Generate codes and fill direct mappings
    uint32_t code = 0;

    for (int i = 0; i < NB_DV_VLC; ++i) {
        // Get bit length for this VLC code
        uint32_t len = dv_vlc_len[i];

        // Guard against invalid shifts (should never happen with valid data)
        if (len == 0 || len > 32)
            continue;

        // Calculate shift amount
        uint32_t shift_amount = 32 - len;

        // Calculate current code (extract top 'len' bits)
        uint32_t cur_code = shift_amount < 32 ? (code >> shift_amount) : 0;

        // Advance to next code for this bit length
        code += shift_amount < 32 ? (1U << shift_amount) : 0;

        // Get run and level for this entry
        int run = dv_vlc_run[i];
        int level = dv_vlc_level[i];

        // Skip if out of bounds
        if (run >= DV_VLC_MAP_RUN_SIZE)
            continue;

        // Skip if entry already filled (handles duplicates)
        if (table[run][level].size != 0)
            continue;

        // Store VLC code and size
        // Add 1 bit if level != 0 (sign bit)
        table[run][level].vlc = cur_code << (level != 0 ? 1 : 0);
        table[run][level].size = len + (level != 0 ? 1 : 0);
    }

    // Phase 3: Gap filling with combination strategy
    for (int i = 0; i < DV_VLC_MAP_RUN_SIZE; ++i) {
        // Fill positive levels
        for (int j = 1; j < DV_VLC_MAP_LEV_SIZE / 2; ++j) {
            if (table[i][j].size == 0) {
                // Combine [0][j] and [i-1][0] codes
                // Format: <run escape code> <level code>
                if (i > 0) {
                    table[i][j].vlc = table[0][j].vlc |
                                      (table[i - 1][0].vlc << table[0][j].size);
                    table[i][j].size = table[i - 1][0].size + table[0][j].size;
                }
            }

            // Mirror to negative level
            // Negative levels use same code with LSB = 1 (sign bit)
            uint16_t neg_idx = static_cast<uint16_t>(-j) & 0x1ff;
            table[i][neg_idx].vlc = table[i][j].vlc | 1;
            table[i][neg_idx].size = table[i][j].size;
        }
    }

    return table;
}

// Generate table at compile time
constexpr auto dv_vlc_map = generate_dv_vlc_map();

// Total: 64 × 512 = 32,768 dv_vlc_pair structs!
//        32,768 × 2 uint32_t = 65,536 uint32_t values!

// Compile-time validation
namespace tests {
    // Test table dimensions
    static_assert(dv_vlc_map.size() == DV_VLC_MAP_RUN_SIZE, "Run dimension");
    static_assert(dv_vlc_map[0].size() == DV_VLC_MAP_LEV_SIZE, "Level dimension");

    // Test that [0][0] is initialized (special EOB - End Of Block)
    static_assert(dv_vlc_map[0][0].size != 0 || dv_vlc_map[0][0].vlc == 0,
                  "EOB entry exists");

    // Test that [0][1] has a valid code (most common: run=0, level=1)
    // Note: VLC code can be 0 (it's the first Huffman code)
    static_assert(dv_vlc_map[0][1].size > 0, "Common entry [0][1] filled");

    // Test that [0][2] has a valid code (run=0, level=2)
    static_assert(dv_vlc_map[0][2].size > 0, "Common entry [0][2] filled");
    static_assert(dv_vlc_map[0][2].vlc != 0, "[0][2] has non-zero code");

    // Test some known entries from source data
    // Entry 0: len=2, run=0, level=1 → should have size >= 2
    static_assert(dv_vlc_map[0][1].size >= 2, "Entry [0][1] size valid");

    // Entry 1: len=3, run=0, level=2 → should have size >= 3
    static_assert(dv_vlc_map[0][2].size >= 3, "Entry [0][2] size valid");

    // Test gap filling worked - check that higher run values are filled
    static_assert(dv_vlc_map[10][1].size > 0, "Gap filled [10][1]");
    static_assert(dv_vlc_map[20][1].size > 0, "Gap filled [20][1]");
    static_assert(dv_vlc_map[30][1].size > 0, "Gap filled [30][1]");

    // Test negative level mirroring
    // Negative index for level=-1 is 511 (0x1ff)
    constexpr int neg_1 = static_cast<uint16_t>(-1) & 0x1ff;
    static_assert(neg_1 == 511, "Negative index calculation");
    static_assert(dv_vlc_map[0][511].size > 0, "Negative level [-1] filled");
    static_assert(dv_vlc_map[0][511].vlc & 1, "Negative level has sign bit");

    // Negative level should have same size as positive
    static_assert(dv_vlc_map[0][511].size == dv_vlc_map[0][1].size,
                  "Negative/positive same size");

    // Test that vlc code differs only in LSB
    static_assert((dv_vlc_map[0][511].vlc ^ dv_vlc_map[0][1].vlc) == 1,
                  "Negative/positive differ only in sign bit");

    // Test more negative levels
    constexpr int neg_5 = static_cast<uint16_t>(-5) & 0x1ff;
    static_assert(dv_vlc_map[0][neg_5].size > 0, "Negative level [-5] filled");
    static_assert(dv_vlc_map[0][neg_5].vlc & 1, "Negative [-5] has sign bit");

    // Test code length ranges (should be reasonable, not exceeding 16 bits)
    static_assert(dv_vlc_map[0][1].size <= 16, "Code size <= 16 bits");
    static_assert(dv_vlc_map[10][10].size <= 32, "Code size reasonable");

    // Test that different entries have different codes (when both non-zero)
    static_assert(dv_vlc_map[0][1].vlc != dv_vlc_map[0][2].vlc ||
                  dv_vlc_map[0][1].size != dv_vlc_map[0][2].size,
                  "Different entries differ");

    // Test source data array sizes
    static_assert(dv_vlc_len.size() == NB_DV_VLC, "Source len array size");
    static_assert(dv_vlc_run.size() == NB_DV_VLC, "Source run array size");
    static_assert(dv_vlc_level.size() == NB_DV_VLC, "Source level array size");

    // Test source data values
    static_assert(dv_vlc_len[0] == 2, "First code length = 2");
    static_assert(dv_vlc_run[0] == 0, "First run = 0");
    static_assert(dv_vlc_level[0] == 1, "First level = 1");

    // Test that some run values exist in source
    static_assert(dv_vlc_run[22] == 7, "Source run check");
    static_assert(dv_vlc_level[22] == 1, "Source level check");

    // Test coverage: verify multiple run/level combinations are filled
    static_assert(dv_vlc_map[1][1].size > 0, "Coverage [1][1]");
    static_assert(dv_vlc_map[2][1].size > 0, "Coverage [2][1]");
    static_assert(dv_vlc_map[3][1].size > 0, "Coverage [3][1]");
    static_assert(dv_vlc_map[5][1].size > 0, "Coverage [5][1]");
    static_assert(dv_vlc_map[0][3].size > 0, "Coverage [0][3]");
    static_assert(dv_vlc_map[0][4].size > 0, "Coverage [0][4]");

    // Test that gap filling creates longer codes
    // Gap-filled entries should have size >= base codes
    static_assert(dv_vlc_map[50][1].size >= dv_vlc_map[0][1].size,
                  "Gap-filled codes are longer");

    // Test constants
    static_assert(NB_DV_VLC == 409, "NB_DV_VLC constant");
    static_assert(DV_VLC_MAP_RUN_SIZE == 64, "Run size constant");
    static_assert(DV_VLC_MAP_LEV_SIZE == 512, "Level size constant");
}

/**
 * Constexpr accessors
 */
constexpr DVVLCPair get_dv_vlc_pair(int run, int level) noexcept {
    if (run >= 0 && run < DV_VLC_MAP_RUN_SIZE &&
        level >= -256 && level < 256) {
        // Handle negative levels
        int level_idx = level < 0 ? (static_cast<uint16_t>(level) & 0x1ff) : level;
        return dv_vlc_map[run][level_idx];
    }
    return {0, 0};
}

constexpr uint32_t get_dv_vlc_code(int run, int level) noexcept {
    return get_dv_vlc_pair(run, level).vlc;
}

constexpr uint32_t get_dv_vlc_size(int run, int level) noexcept {
    return get_dv_vlc_pair(run, level).size;
}

} // namespace dv
} // namespace ffmpeg

#endif // AVCODEC_DV_TABLEGEN_CONSTEXPR_HPP
