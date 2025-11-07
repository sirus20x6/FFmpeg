/*
 * Modern C++ constexpr VIMA prediction tables
 * Copyright (c) 2012 Paul B Mahol
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
 * Modern C++20 constexpr VIMA prediction tables
 *
 * VIMA (LucasArts SMUSH VIMA audio) uses ADPCM-style prediction with
 * specialized lookup tables for fast decoding.
 *
 * Algorithm:
 * The predict_table is derived from ADPCM step table by performing bit-weighted
 * accumulation. For each 6-bit start_pos and each step table entry, compute
 * a prediction value by summing step_value >> bit_position for each set bit.
 *
 * Formula for predict_table[start_pos + table_pos * 64]:
 *   result = sum over bits i where (start_pos & (1 << i)) != 0:
 *              (ff_adpcm_step_table[table_pos] >> (5 - i))
 *
 * This transforms ADPCM step values into position-specific prediction deltas.
 *
 * Size: 5,696 uint16_t entries (89 ADPCM steps × 64 start positions)
 * Dimensions: 64 start positions × 89 ADPCM steps
 *
 * Used by: VIMA audio decoder (LucasArts SMUSH games)
 */

#ifndef AVCODEC_VIMA_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_VIMA_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>

namespace ffmpeg {
namespace vima {

// Constants
constexpr int ADPCM_STEP_TABLE_SIZE = 89;
constexpr int START_POS_COUNT = 64;  // 2^6 possible start positions
// Table size: max index is (63 + 88*64) = 5695, so need 5696 entries
constexpr int PREDICT_TABLE_SIZE = ADPCM_STEP_TABLE_SIZE * START_POS_COUNT;  // 5,696

/**
 * ADPCM step table (from adpcm_data.c)
 *
 * This is the standard IMA/DVI ADPCM step table used for quantization.
 * Values range exponentially from 7 to 32767.
 */
constexpr std::array<int16_t, ADPCM_STEP_TABLE_SIZE> adpcm_step_table = {
        7,     8,     9,    10,    11,    12,    13,    14,    16,    17,
       19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
       50,    55,    60,    66,    73,    80,    88,    97,   107,   118,
      130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
      337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
      876,   963,  1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
     2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
     5894,  6484,  7132,  7845,  8630,  9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

/**
 * Generate VIMA prediction table at compile time
 *
 * Algorithm (from vima.c predict_table_init):
 *
 * for start_pos = 0 to 63:
 *     for table_pos = 0 to 88 (ADPCM steps):
 *         dest_pos = start_pos + table_pos * 64
 *         put = 0
 *         table_value = adpcm_step_table[table_pos]
 *
 *         // Bit-weighted accumulation
 *         for each bit position (32, 16, 8, 4, 2, 1):
 *             if (start_pos & bit):
 *                 put += table_value
 *             table_value >>= 1
 *
 *         predict_table[dest_pos] = put
 *
 * The algorithm performs a dot product between:
 * - Binary representation of start_pos (6 bits)
 * - Right-shifted versions of step_value (6 shifts)
 *
 * Example: start_pos = 0b100101 (37), step = 1000
 *   Bit 5 (32): set → add 1000 >> 0 = 1000
 *   Bit 4 (16): clear
 *   Bit 3 (8):  clear
 *   Bit 2 (4):  set → add 1000 >> 3 = 125
 *   Bit 1 (2):  clear
 *   Bit 0 (1):  set → add 1000 >> 5 = 31
 *   Total: 1000 + 125 + 31 = 1156
 */
constexpr auto generate_vima_predict_table() noexcept {
    std::array<uint16_t, PREDICT_TABLE_SIZE> table{};

    for (int start_pos = 0; start_pos < START_POS_COUNT; ++start_pos) {
        for (int table_pos = 0; table_pos < ADPCM_STEP_TABLE_SIZE; ++table_pos) {
            int dest_pos = start_pos + table_pos * START_POS_COUNT;

            int put = 0;
            int table_value = adpcm_step_table[table_pos];

            // Bit-weighted accumulation (6 bits: 32, 16, 8, 4, 2, 1)
            for (int count = 32; count != 0; count >>= 1) {
                if (start_pos & count) {
                    put += table_value;
                }
                table_value >>= 1;
            }

            table[dest_pos] = static_cast<uint16_t>(put);
        }
    }

    return table;
}

// Generate table at compile time
constexpr auto vima_predict_table = generate_vima_predict_table();

// Total: 89 × 64 = 5,696 uint16_t entries!

// Compile-time validation
namespace tests {
    // Test table size
    static_assert(vima_predict_table.size() == PREDICT_TABLE_SIZE,
                  "Table size = 5,696");

    // Test that index 0 (start_pos=0) gives 0 (no bits set)
    static_assert(vima_predict_table[0] == 0, "Table[0] = 0");

    // Test start_pos=1 (only bit 0 set): should be step_value >> 5
    // For table_pos=0 (step=7): 7 >> 5 = 0
    static_assert(vima_predict_table[1] == 0, "start_pos=1, step=7");

    // Test start_pos=32 (only bit 5 set): should equal step_value
    // For table_pos=0 (step=7, dest=32): result = 7
    static_assert(vima_predict_table[32] == 7, "start_pos=32, step=7");

    // Test start_pos=0, table_pos=1 (step=8, dest=64)
    static_assert(vima_predict_table[64] == 0, "start_pos=0, step=8");

    // Test start_pos=32, table_pos=1 (step=8, dest=96)
    static_assert(vima_predict_table[96] == 8, "start_pos=32, step=8");

    // Test start_pos=63 (all bits set: 111111b)
    // For table_pos=0 (step=7):
    //   bit 5: 7 >> 0 = 7
    //   bit 4: 7 >> 1 = 3
    //   bit 3: 7 >> 2 = 1
    //   bit 2: 7 >> 3 = 0
    //   bit 1: 7 >> 4 = 0
    //   bit 0: 7 >> 5 = 0
    //   Total: 7 + 3 + 1 = 11
    static_assert(vima_predict_table[63] == 11, "start_pos=63, step=7");

    // Test start_pos=63, table_pos=10 (step=19, dest=63+10*64=703)
    // step=19:
    //   19 >> 0 = 19
    //   19 >> 1 = 9
    //   19 >> 2 = 4
    //   19 >> 3 = 2
    //   19 >> 4 = 1
    //   19 >> 5 = 0
    //   Total: 19 + 9 + 4 + 2 + 1 = 35
    static_assert(vima_predict_table[63 + 10 * 64] == 35,
                  "start_pos=63, step=19");

    // Test with larger step value
    // start_pos=63, table_pos=20 (step=50, dest=63+20*64=1343)
    // step=50:
    //   50 >> 0 = 50
    //   50 >> 1 = 25
    //   50 >> 2 = 12
    //   50 >> 3 = 6
    //   50 >> 4 = 3
    //   50 >> 5 = 1
    //   Total: 50 + 25 + 12 + 6 + 3 + 1 = 97
    static_assert(vima_predict_table[63 + 20 * 64] == 97,
                  "start_pos=63, step=50");

    // Test maximum table value
    // start_pos=63, table_pos=88 (step=32767, dest=63+88*64=5695)
    // step=32767 (0x7FFF):
    //   32767 >> 0 = 32767
    //   32767 >> 1 = 16383
    //   32767 >> 2 = 8191
    //   32767 >> 3 = 4095
    //   32767 >> 4 = 2047
    //   32767 >> 5 = 1023
    //   Total: 32767 + 16383 + 8191 + 4095 + 2047 + 1023 = 64506
    static_assert(vima_predict_table[63 + 88 * 64] == 64506,
                  "Maximum value at start_pos=63, step=32767");

    // Test source ADPCM step table
    static_assert(adpcm_step_table.size() == ADPCM_STEP_TABLE_SIZE,
                  "ADPCM step table size = 89");
    static_assert(adpcm_step_table[0] == 7, "First step = 7");
    static_assert(adpcm_step_table[88] == 32767, "Last step = 32767");
    static_assert(adpcm_step_table[44] == 494, "Middle step value");

    // Test constants
    static_assert(PREDICT_TABLE_SIZE == 5696, "Table size constant");
    static_assert(START_POS_COUNT == 64, "Start pos count");
    static_assert(ADPCM_STEP_TABLE_SIZE == 89, "ADPCM steps");

    // Test monotonicity within same start_pos (values should generally increase)
    static_assert(vima_predict_table[32] < vima_predict_table[32 + 10 * 64],
                  "Monotonic increase");
    static_assert(vima_predict_table[32 + 10 * 64] < vima_predict_table[32 + 50 * 64],
                  "Monotonic increase 2");

    // Test that different start_pos give different results (for same table_pos)
    static_assert(vima_predict_table[0 + 10 * 64] != vima_predict_table[32 + 10 * 64],
                  "Different start_pos differ");

    // Test specific bit patterns
    // start_pos=1 (000001b): only bit 0, for step=32767
    // 32767 >> 5 = 1023
    static_assert(vima_predict_table[1 + 88 * 64] == 1023,
                  "start_pos=1, max step");

    // start_pos=16 (010000b): only bit 4, for step=32767
    // 32767 >> 1 = 16383
    static_assert(vima_predict_table[16 + 88 * 64] == 16383,
                  "start_pos=16, max step");

    // Test coverage: multiple table positions
    static_assert(vima_predict_table[10 * 64] == 0, "Coverage check 1");
    static_assert(vima_predict_table[20 * 64] == 0, "Coverage check 2");
    static_assert(vima_predict_table[50 * 64] == 0, "Coverage check 3");

    // Test that all-bits-set gives largest value for any step
    static_assert(vima_predict_table[63 + 50 * 64] > vima_predict_table[32 + 50 * 64],
                  "All bits > single bit");
    static_assert(vima_predict_table[63 + 50 * 64] > vima_predict_table[1 + 50 * 64],
                  "All bits > LSB only");
}

/**
 * Constexpr accessor with bounds checking
 */
constexpr uint16_t get_vima_predict_value(int start_pos, int table_pos) noexcept {
    if (start_pos < 0 || start_pos >= START_POS_COUNT ||
        table_pos < 0 || table_pos >= ADPCM_STEP_TABLE_SIZE) {
        return 0;
    }

    int index = start_pos + table_pos * START_POS_COUNT;
    return vima_predict_table[index];
}

/**
 * Get raw table pointer for C interop
 */
constexpr const uint16_t* get_vima_predict_table() noexcept {
    return vima_predict_table.data();
}

} // namespace vima
} // namespace ffmpeg

#endif // AVCODEC_VIMA_TABLEGEN_CONSTEXPR_HPP
