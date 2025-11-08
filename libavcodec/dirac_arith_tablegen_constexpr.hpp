/*
 * Modern C++ constexpr Dirac arithmetic coder tables
 * Copyright (C) 2007 Marco Gerards
 * Copyright (C) 2009 David Conrad
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
 * Modern C++20 constexpr Dirac arithmetic coder probability tables
 *
 * Dirac is a professional video codec developed by BBC Research for high-quality
 * video compression. It uses arithmetic coding with adaptive probability models
 * for efficient entropy coding.
 *
 * Tables:
 * 1. dirac_prob[256]: Base probability lookup table (uint16_t)
 * 2. dirac_prob_branchless[256][2]: Branchless conditional probabilities (int16_t)
 *
 * The branchless table provides two values per index:
 * - [i][0] = dirac_prob[255-i]  (reversed probability)
 * - [i][1] = -dirac_prob[i]     (negated probability)
 *
 * This design enables branch-free conditional operations in the arithmetic decoder,
 * improving performance on modern pipelined CPUs by avoiding conditional branches.
 *
 * Total: 256 + 512 = 768 int16_t entries
 *
 * Used by: Dirac video codec (professional broadcast quality)
 */

#ifndef AVCODEC_DIRAC_ARITH_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_DIRAC_ARITH_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>

namespace ffmpeg {
namespace dirac_arith {

// Constants
constexpr int PROB_TABLE_SIZE = 256;

/**
 * Base probability table from original Dirac specification
 *
 * This table defines the probability model for arithmetic coding.
 * Values range from 0 to 2072 (representing probabilities scaled by 2048).
 *
 * The table has a characteristic shape:
 * - Starts at 0 and increases to a peak around index 160 (value ~2072)
 * - Symmetric decrease back to 255 at the end
 * - Represents cumulative probability distribution
 *
 * Directly from dirac_arith.c (BBC Research specification).
 */
constexpr std::array<uint16_t, PROB_TABLE_SIZE> dirac_prob = {{
    0,    2,    5,    8,    11,   15,   20,   24,
    29,   35,   41,   47,   53,   60,   67,   74,
    82,   89,   97,   106,  114,  123,  132,  141,
    150,  160,  170,  180,  190,  201,  211,  222,
    233,  244,  256,  267,  279,  291,  303,  315,
    327,  340,  353,  366,  379,  392,  405,  419,
    433,  447,  461,  475,  489,  504,  518,  533,
    548,  563,  578,  593,  609,  624,  640,  656,
    672,  688,  705,  721,  738,  754,  771,  788,
    805,  822,  840,  857,  875,  892,  910,  928,
    946,  964,  983,  1001, 1020, 1038, 1057, 1076,
    1095, 1114, 1133, 1153, 1172, 1192, 1211, 1231,
    1251, 1271, 1291, 1311, 1332, 1352, 1373, 1393,
    1414, 1435, 1456, 1477, 1498, 1520, 1541, 1562,
    1584, 1606, 1628, 1649, 1671, 1694, 1716, 1738,
    1760, 1783, 1806, 1828, 1851, 1874, 1897, 1920,
    1935, 1942, 1949, 1955, 1961, 1968, 1974, 1980,
    1985, 1991, 1996, 2001, 2006, 2011, 2016, 2021,
    2025, 2029, 2033, 2037, 2040, 2044, 2047, 2050,
    2053, 2056, 2058, 2061, 2063, 2065, 2066, 2068,
    2069, 2070, 2071, 2072, 2072, 2072, 2072, 2072,
    2072, 2071, 2070, 2069, 2068, 2066, 2065, 2063,
    2060, 2058, 2055, 2052, 2049, 2045, 2042, 2038,
    2033, 2029, 2024, 2019, 2013, 2008, 2002, 1996,
    1989, 1982, 1975, 1968, 1960, 1952, 1943, 1934,
    1925, 1916, 1906, 1896, 1885, 1874, 1863, 1851,
    1839, 1827, 1814, 1800, 1786, 1772, 1757, 1742,
    1727, 1710, 1694, 1676, 1659, 1640, 1622, 1602,
    1582, 1561, 1540, 1518, 1495, 1471, 1447, 1422,
    1396, 1369, 1341, 1312, 1282, 1251, 1219, 1186,
    1151, 1114, 1077, 1037, 995,  952,  906,  857,
    805,  750,  690,  625,  553,  471,  376,  255
}};

/**
 * Generate branchless probability table at compile time
 *
 * Algorithm (from dirac_arith.c ff_dirac_init_arith_tables):
 * - [i][0] = dirac_prob[255-i]  (reversed index lookup)
 * - [i][1] = -dirac_prob[i]     (negated value)
 *
 * Purpose: Branch-free conditional execution in arithmetic decoder
 *
 * Usage pattern in decoder:
 * ```c
 * // Instead of: value = (condition) ? dirac_prob[x] : -dirac_prob[y];
 * // Use: value = dirac_prob_branchless[index][condition];
 * ```
 *
 * The reversed index ([i][0]) and negated value ([i][1]) enable selection
 * between two probability values without conditional branches, improving
 * performance on modern pipelined CPUs.
 *
 * Mathematical meaning:
 * - Column 0: Provides probability from opposite end of table
 * - Column 1: Provides negated probability for sign inversion
 *
 * This branchless design is critical for arithmetic coding performance,
 * as the decoder processes millions of symbols per frame.
 */
constexpr auto generate_dirac_prob_branchless() noexcept {
    std::array<std::array<int16_t, 2>, PROB_TABLE_SIZE> table{};

    for (int i = 0; i < PROB_TABLE_SIZE; ++i) {
        // Column 0: Reversed index lookup
        table[i][0] = static_cast<int16_t>(dirac_prob[255 - i]);
        
        // Column 1: Negated probability
        table[i][1] = static_cast<int16_t>(-static_cast<int16_t>(dirac_prob[i]));
    }

    return table;
}

// Generate branchless table at compile time
constexpr auto dirac_prob_branchless = generate_dirac_prob_branchless();

// Total: 256 + 512 = 768 int16_t entries!

// Compile-time validation
namespace tests {
    // Test table sizes
    static_assert(dirac_prob.size() == PROB_TABLE_SIZE, "dirac_prob size = 256");
    static_assert(dirac_prob_branchless.size() == PROB_TABLE_SIZE, "branchless size = 256");
    static_assert(dirac_prob_branchless[0].size() == 2, "branchless has 2 columns");

    // Test base probability table properties
    // First value should be 0
    static_assert(dirac_prob[0] == 0, "dirac_prob[0] = 0");

    // Second value should be 2
    static_assert(dirac_prob[1] == 2, "dirac_prob[1] = 2");

    // Peak value around index 160-168 should be 2072
    static_assert(dirac_prob[164] == 2072, "Peak value is 2072");
    static_assert(dirac_prob[165] == 2072, "Peak sustained");

    // Last value should be 255
    static_assert(dirac_prob[255] == 255, "dirac_prob[255] = 255");

    // Test monotonicity in ascending region [0, 164]
    static_assert(dirac_prob[0] < dirac_prob[10], "Monotonic increase");
    static_assert(dirac_prob[10] < dirac_prob[50], "Monotonic increase 2");
    static_assert(dirac_prob[50] < dirac_prob[100], "Monotonic increase 3");
    static_assert(dirac_prob[100] < dirac_prob[150], "Monotonic increase 4");

    // Test monotonicity in descending region [168, 255]
    static_assert(dirac_prob[170] > dirac_prob[180], "Monotonic decrease");
    static_assert(dirac_prob[180] > dirac_prob[200], "Monotonic decrease 2");
    static_assert(dirac_prob[200] > dirac_prob[230], "Monotonic decrease 3");
    static_assert(dirac_prob[230] > dirac_prob[255], "Monotonic decrease 4");

    // Test range [0, 2072]
    static_assert(dirac_prob[50] >= 0 && dirac_prob[50] <= 2072, "Range check");
    static_assert(dirac_prob[150] >= 0 && dirac_prob[150] <= 2072, "Range check 2");
    static_assert(dirac_prob[200] >= 0 && dirac_prob[200] <= 2072, "Range check 3");

    // Test branchless table: Column 0 (reversed index)
    // branchless[0][0] should equal dirac_prob[255]
    static_assert(dirac_prob_branchless[0][0] == dirac_prob[255],
                  "branchless[0][0] = dirac_prob[255]");
    static_assert(dirac_prob_branchless[0][0] == 255,
                  "branchless[0][0] = 255");

    // branchless[255][0] should equal dirac_prob[0]
    static_assert(dirac_prob_branchless[255][0] == dirac_prob[0],
                  "branchless[255][0] = dirac_prob[0]");
    static_assert(dirac_prob_branchless[255][0] == 0,
                  "branchless[255][0] = 0");

    // branchless[100][0] should equal dirac_prob[155]
    static_assert(dirac_prob_branchless[100][0] == dirac_prob[155],
                  "branchless[100][0] = dirac_prob[155]");

    // Test branchless table: Column 1 (negated)
    // branchless[0][1] should equal -dirac_prob[0] = 0
    static_assert(dirac_prob_branchless[0][1] == -static_cast<int16_t>(dirac_prob[0]),
                  "branchless[0][1] = -dirac_prob[0]");
    static_assert(dirac_prob_branchless[0][1] == 0,
                  "branchless[0][1] = 0");

    // branchless[1][1] should equal -dirac_prob[1] = -2
    static_assert(dirac_prob_branchless[1][1] == -2,
                  "branchless[1][1] = -2");

    // branchless[255][1] should equal -dirac_prob[255] = -255
    static_assert(dirac_prob_branchless[255][1] == -255,
                  "branchless[255][1] = -255");

    // branchless[164][1] should equal -2072 (negated peak)
    static_assert(dirac_prob_branchless[164][1] == -2072,
                  "branchless[164][1] = -2072");

    // Test symmetry property
    // For any i: branchless[i][0] + branchless[255-i][0] should equal dirac_prob[i] + dirac_prob[255-i]
    // Actually, branchless[i][0] = dirac_prob[255-i] and branchless[255-i][0] = dirac_prob[i]
    static_assert(dirac_prob_branchless[50][0] == dirac_prob[205],
                  "Symmetry check 1");
    static_assert(dirac_prob_branchless[205][0] == dirac_prob[50],
                  "Symmetry check 2");

    // Test negation property: branchless[i][1] = -branchless[i][0] only when i = 255-i (i.e., never except midpoint)
    // Actually: branchless[i][1] = -dirac_prob[i], branchless[i][0] = dirac_prob[255-i]
    // So they're not directly related except at midpoint

    // Test that values are non-zero (table is populated)
    static_assert(dirac_prob_branchless[100][0] != 0, "Non-zero entry");
    static_assert(dirac_prob_branchless[100][1] != 0, "Non-zero entry 2");
    static_assert(dirac_prob_branchless[200][0] != 0, "Non-zero entry 3");
    static_assert(dirac_prob_branchless[200][1] != 0, "Non-zero entry 4");

    // Test range for branchless table
    // Column 0: Should be in [0, 2072] (same as dirac_prob)
    static_assert(dirac_prob_branchless[50][0] >= 0 &&
                  dirac_prob_branchless[50][0] <= 2072,
                  "Branchless[0] range");

    // Column 1: Should be in [-2072, 0] (negated)
    static_assert(dirac_prob_branchless[50][1] >= -2072 &&
                  dirac_prob_branchless[50][1] <= 0,
                  "Branchless[1] range");
    static_assert(dirac_prob_branchless[164][1] >= -2072 &&
                  dirac_prob_branchless[164][1] <= 0,
                  "Branchless[1] range at peak");

    // Test specific known values from original table
    static_assert(dirac_prob[10] == 41, "Specific value check");
    static_assert(dirac_prob[50] == 461, "Specific value check 2");
    static_assert(dirac_prob[100] == 1332, "Specific value check 3");
    static_assert(dirac_prob[150] == 2047, "Specific value check 4");

    // Verify branchless derivations of specific values
    static_assert(dirac_prob_branchless[10][1] == -41, "Branchless negation");
    static_assert(dirac_prob_branchless[50][1] == -461, "Branchless negation 2");
    static_assert(dirac_prob_branchless[245][0] == 41, "Branchless reversal (255-245=10)");
}

/**
 * Constexpr accessors with bounds checking
 */
constexpr uint16_t get_dirac_prob(int index) noexcept {
    if (index < 0 || index >= PROB_TABLE_SIZE) return 0;
    return dirac_prob[index];
}

constexpr int16_t get_dirac_prob_branchless(int index, int column) noexcept {
    if (index < 0 || index >= PROB_TABLE_SIZE || column < 0 || column > 1) return 0;
    return dirac_prob_branchless[index][column];
}

/**
 * Get raw table pointers for C interop
 */
inline const uint16_t* get_dirac_prob_table() noexcept {
    return dirac_prob.data();
}

inline const int16_t (*get_dirac_prob_branchless_table())[2] {
    return reinterpret_cast<const int16_t(*)[2]>(dirac_prob_branchless.data());
}

} // namespace dirac_arith
} // namespace ffmpeg

#endif // AVCODEC_DIRAC_ARITH_TABLEGEN_CONSTEXPR_HPP
