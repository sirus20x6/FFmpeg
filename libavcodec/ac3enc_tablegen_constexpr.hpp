/*
 * Modern C++ constexpr AC3 encoder exponent grouping tables
 * Copyright (c) 2006-2010 Justin Ruggles
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
 * Modern C++20 constexpr AC-3 encoder exponent grouping tables
 *
 * AC-3 (Dolby Digital) is a perceptual audio codec used in cinema, broadcast,
 * and home theater applications. The encoder uses exponent grouping to efficiently
 * encode the spectral envelope of audio signals.
 *
 * Table:
 * - exponent_group_tab[2][3][256]: Number of exponent groups per strategy
 *
 * Dimensions:
 * - [coupling]: 0 = non-coupling, 1 = coupling channel
 * - [expstr]: Exponent strategy (0=D15, 1=D25, 2=D45)
 * - [ncoefs]: Number of coefficients (0-255)
 *
 * Exponent strategies determine grouping size:
 * - D15: grpsize = 3  (1 group per 3 coefficients)
 * - D25: grpsize = 6  (1 group per 6 coefficients)
 * - D45: grpsize = 12 (1 group per 12 coefficients)
 *
 * The strategy balances encoding precision vs. bitrate: smaller groups preserve
 * more detail but require more bits.
 *
 * Total: 2 × 3 × 256 = 1,536 uint8_t entries
 *
 * Used by: AC-3 (Dolby Digital) encoder (cinema/broadcast/home theater)
 */

#ifndef AVCODEC_AC3ENC_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_AC3ENC_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>

namespace ffmpeg {
namespace ac3enc {

// Constants
constexpr int COUPLING_MODES = 2;      // Non-coupling (0) and coupling (1)
constexpr int EXPONENT_STRATEGIES = 3; // D15, D25, D45
constexpr int MAX_COEFFICIENTS = 256;

// Exponent strategy enum values (from ac3defs.h)
constexpr int EXP_D15 = 1;
constexpr int EXP_D25 = 2;
constexpr int EXP_D45 = 3;

/**
 * Generate AC-3 exponent grouping table at compile time
 *
 * Algorithm (from ac3enc.c exponent_init):
 * ```c
 * for (expstr = EXP_D15-1; expstr <= EXP_D45-1; expstr++) {
 *     grpsize = 3 << expstr;  // 3, 6, or 12
 *     for (i = 12; i < 256; i++) {
 *         exponent_group_tab[0][expstr][i] = (i + grpsize - 4) / grpsize;
 *         exponent_group_tab[1][expstr][i] = i / grpsize;
 *     }
 * }
 * exponent_group_tab[0][0][7] = 2;  // LFE special case
 * ```
 *
 * Meaning:
 * - expstr 0 (D15): grpsize = 3,  divide coefficients into groups of 3
 * - expstr 1 (D25): grpsize = 6,  divide coefficients into groups of 6
 * - expstr 2 (D45): grpsize = 12, divide coefficients into groups of 12
 *
 * For non-coupling (coupling=0):
 * - Formula: (ncoefs + grpsize - 4) / grpsize
 * - Accounts for 4-coefficient offset in AC-3 spec
 *
 * For coupling (coupling=1):
 * - Formula: ncoefs / grpsize
 * - Simple division without offset
 *
 * Special case:
 * - LFE (Low Frequency Effects) channel: exponent_group_tab[0][0][7] = 2
 */
constexpr auto generate_exponent_group_tab() noexcept {
    std::array<std::array<std::array<uint8_t, MAX_COEFFICIENTS>, EXPONENT_STRATEGIES>, COUPLING_MODES> table{};

    // expstr: 0=D15, 1=D25, 2=D45
    for (int expstr = EXP_D15 - 1; expstr <= EXP_D45 - 1; ++expstr) {
        int grpsize = 3 << expstr;  // 3, 6, or 12

        for (int i = 12; i < MAX_COEFFICIENTS; ++i) {
            // Non-coupling: account for 4-coefficient offset
            table[0][expstr][i] = static_cast<uint8_t>((i + grpsize - 4) / grpsize);
            
            // Coupling: simple division
            table[1][expstr][i] = static_cast<uint8_t>(i / grpsize);
        }
    }

    // LFE special case
    table[0][0][7] = 2;

    return table;
}

// Generate table at compile time
constexpr auto exponent_group_tab = generate_exponent_group_tab();

// Total: 2 × 3 × 256 = 1,536 uint8_t entries!

// Compile-time validation
namespace tests {
    // Test table dimensions
    static_assert(exponent_group_tab.size() == COUPLING_MODES, "Coupling modes = 2");
    static_assert(exponent_group_tab[0].size() == EXPONENT_STRATEGIES, "Exponent strategies = 3");
    static_assert(exponent_group_tab[0][0].size() == MAX_COEFFICIENTS, "Max coefficients = 256");

    // Test LFE special case
    static_assert(exponent_group_tab[0][0][7] == 2, "LFE special case");

    // Test D15 (grpsize = 3) calculations for non-coupling
    // For i=12: (12 + 3 - 4) / 3 = 11 / 3 = 3
    static_assert(exponent_group_tab[0][0][12] == 3, "D15 noncpl i=12");
    // For i=15: (15 + 3 - 4) / 3 = 14 / 3 = 4
    static_assert(exponent_group_tab[0][0][15] == 4, "D15 noncpl i=15");
    // For i=30: (30 + 3 - 4) / 3 = 29 / 3 = 9
    static_assert(exponent_group_tab[0][0][30] == 9, "D15 noncpl i=30");

    // Test D25 (grpsize = 6) calculations for non-coupling
    // For i=12: (12 + 6 - 4) / 6 = 14 / 6 = 2
    static_assert(exponent_group_tab[0][1][12] == 2, "D25 noncpl i=12");
    // For i=30: (30 + 6 - 4) / 6 = 32 / 6 = 5
    static_assert(exponent_group_tab[0][1][30] == 5, "D25 noncpl i=30");
    // For i=60: (60 + 6 - 4) / 6 = 62 / 6 = 10
    static_assert(exponent_group_tab[0][1][60] == 10, "D25 noncpl i=60");

    // Test D45 (grpsize = 12) calculations for non-coupling
    // For i=12: (12 + 12 - 4) / 12 = 20 / 12 = 1
    static_assert(exponent_group_tab[0][2][12] == 1, "D45 noncpl i=12");
    // For i=24: (24 + 12 - 4) / 12 = 32 / 12 = 2
    static_assert(exponent_group_tab[0][2][24] == 2, "D45 noncpl i=24");
    // For i=120: (120 + 12 - 4) / 12 = 128 / 12 = 10
    static_assert(exponent_group_tab[0][2][120] == 10, "D45 noncpl i=120");

    // Test D15 (grpsize = 3) calculations for coupling
    // For i=12: 12 / 3 = 4
    static_assert(exponent_group_tab[1][0][12] == 4, "D15 cpl i=12");
    // For i=30: 30 / 3 = 10
    static_assert(exponent_group_tab[1][0][30] == 10, "D15 cpl i=30");
    // For i=99: 99 / 3 = 33
    static_assert(exponent_group_tab[1][0][99] == 33, "D15 cpl i=99");

    // Test D25 (grpsize = 6) calculations for coupling
    // For i=12: 12 / 6 = 2
    static_assert(exponent_group_tab[1][1][12] == 2, "D25 cpl i=12");
    // For i=60: 60 / 6 = 10
    static_assert(exponent_group_tab[1][1][60] == 10, "D25 cpl i=60");
    // For i=126: 126 / 6 = 21
    static_assert(exponent_group_tab[1][1][126] == 21, "D25 cpl i=126");

    // Test D45 (grpsize = 12) calculations for coupling
    // For i=12: 12 / 12 = 1
    static_assert(exponent_group_tab[1][2][12] == 1, "D45 cpl i=12");
    // For i=120: 120 / 12 = 10
    static_assert(exponent_group_tab[1][2][120] == 10, "D45 cpl i=120");
    // For i=252: 252 / 12 = 21
    static_assert(exponent_group_tab[1][2][252] == 21, "D45 cpl i=252");

    // Test that entries below i=12 are zero (not initialized in original code)
    static_assert(exponent_group_tab[0][0][0] == 0, "Below i=12 is zero");
    static_assert(exponent_group_tab[0][0][11] == 0, "i=11 is zero");
    static_assert(exponent_group_tab[1][0][0] == 0, "Coupling below i=12 zero");

    // Test monotonicity: as coefficients increase, groups should increase
    static_assert(exponent_group_tab[0][0][12] < exponent_group_tab[0][0][15],
                  "Groups increase with coeffs");
    static_assert(exponent_group_tab[0][0][15] < exponent_group_tab[0][0][30],
                  "Groups increase cont");
    static_assert(exponent_group_tab[0][0][30] < exponent_group_tab[0][0][60],
                  "Groups increase cont2");

    // Test coupling vs non-coupling difference
    // At i=12, non-coupling has offset: (12+3-4)/3=3, coupling has no offset: 12/3=4
    static_assert(exponent_group_tab[0][0][12] != exponent_group_tab[1][0][12],
                  "Coupling differs from non-coupling");
    static_assert(exponent_group_tab[0][0][12] == 3 &&
                  exponent_group_tab[1][0][12] == 4,
                  "Offset accounts for difference");

    // Test boundary values
    static_assert(exponent_group_tab[0][0][255] > 0, "Max coef non-zero");
    static_assert(exponent_group_tab[1][0][255] > 0, "Max coef cpl non-zero");
    static_assert(exponent_group_tab[0][2][255] > 0, "Max coef D45 non-zero");
}

/**
 * Constexpr accessor with bounds checking
 */
constexpr uint8_t get_exponent_groups(int coupling, int expstr, int ncoefs) noexcept {
    if (coupling < 0 || coupling >= COUPLING_MODES ||
        expstr < 0 || expstr >= EXPONENT_STRATEGIES ||
        ncoefs < 0 || ncoefs >= MAX_COEFFICIENTS) {
        return 0;
    }
    return exponent_group_tab[coupling][expstr][ncoefs];
}

/**
 * Get raw table pointer for C interop
 */
inline const uint8_t (*get_exponent_group_tab())[EXPONENT_STRATEGIES][MAX_COEFFICIENTS] {
    return reinterpret_cast<const uint8_t(*)[EXPONENT_STRATEGIES][MAX_COEFFICIENTS]>(
        exponent_group_tab.data());
}

} // namespace ac3enc
} // namespace ffmpeg

#endif // AVCODEC_AC3ENC_TABLEGEN_CONSTEXPR_HPP
