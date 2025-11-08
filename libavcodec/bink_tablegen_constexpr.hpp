/*
 * Modern C++ constexpr Bink video codec quantization tables
 * Copyright (C) 2009 Konstantin Shishkov
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
 * Modern C++20 constexpr Bink video codec quantization tables
 *
 * Bink is a video codec developed by RAD Game Tools, widely used in video games
 * for cutscenes and cinematics. Version 'b' requires quantization tables for DCT
 * coefficient dequantization during decoding.
 *
 * Tables:
 * 1. binkb_intra_quant[16][64]: Intra-frame quantization (1,024 int32_t)
 * 2. binkb_inter_quant[16][64]: Inter-frame quantization (1,024 int32_t)
 *
 * Total: 2,048 int32_t entries (8,192 bytes)
 *
 * Algorithm:
 * For each quantization level j (0-15) and DCT coefficient i (0-63):
 *   k = inv_bink_scan[i]  (reorder via inverse scan pattern)
 *   intra_quant[j][k] = intra_seed[i] × s[i] × num[j] / (den[j] × (C>>12))
 *   inter_quant[j][k] = inter_seed[i] × s[i] × num[j] / (den[j] × (C>>12))
 *
 * Where:
 * - C = (1LL<<30) = 1,073,741,824 (fixed-point scaling constant)
 * - s[i]: DCT coefficient scaling factors (frequency weighting)
 * - seed: Base quantization values (different for intra vs inter)
 * - num/den: Rational quantization level multipliers
 *
 * Used by: Bink video decoder (RAD Game Tools, widely used in games)
 */

#ifndef AVCODEC_BINK_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_BINK_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>

namespace ffmpeg {
namespace bink {

// Constants
constexpr int QUANT_LEVELS = 16;
constexpr int DCT_BLOCK_SIZE = 64;
constexpr int64_t C = (1LL << 30);  // 1,073,741,824

// Bink DCT block scan order (from binkdata.h)
constexpr std::array<uint8_t, DCT_BLOCK_SIZE> bink_scan = {{
     0,  1,  8,  9,  2,  3, 10, 11,
     4,  5, 12, 13,  6,  7, 14, 15,
    20, 21, 28, 29, 22, 23, 30, 31,
    16, 17, 24, 25, 32, 33, 40, 41,
    34, 35, 42, 43, 48, 49, 56, 57,
    50, 51, 58, 59, 18, 19, 26, 27,
    36, 37, 44, 45, 38, 39, 46, 47,
    52, 53, 60, 61, 54, 55, 62, 63
}};

// DCT coefficient scaling factors (from binkb_calc_quant in bink.c)
constexpr std::array<int32_t, DCT_BLOCK_SIZE> s = {{
    1073741824, 1489322693, 1402911301, 1262586814, 1073741824,  843633538,  581104888,  296244703,
    1489322693, 2065749918, 1945893874, 1751258219, 1489322693, 1170153332,  806015634,  410903207,
    1402911301, 1945893874, 1832991949, 1649649171, 1402911301, 1102260336,  759250125,  387062357,
    1262586814, 1751258219, 1649649171, 1484645031, 1262586814,  992008094,  683307060,  348346918,
    1073741824, 1489322693, 1402911301, 1262586814, 1073741824,  843633538,  581104888,  296244703,
     843633538, 1170153332, 1102260336,  992008094,  843633538,  662838617,  456571181,  232757969,
     581104888,  806015634,  759250125,  683307060,  581104888,  456571181,  314491699,  160326478,
     296244703,  410903207,  387062357,  348346918,  296244703,  232757969,  160326478,   81733730,
}};

// Intra-frame quantization seed values (from binkdata.h)
constexpr std::array<uint8_t, DCT_BLOCK_SIZE> binkb_intra_seed = {{
    16, 16, 16, 19, 16, 19, 22, 22,
    22, 22, 26, 24, 26, 22, 22, 27,
    27, 27, 26, 26, 26, 29, 29, 29,
    27, 27, 27, 26, 34, 34, 34, 29,
    29, 29, 27, 27, 37, 34, 34, 32,
    32, 29, 29, 38, 37, 35, 35, 34,
    35, 40, 40, 40, 38, 38, 48, 48,
    46, 46, 58, 56, 56, 69, 69, 83,
}};

// Inter-frame quantization seed values (from binkdata.h)
constexpr std::array<uint8_t, DCT_BLOCK_SIZE> binkb_inter_seed = {{
    16, 17, 17, 18, 18, 18, 19, 19,
    19, 19, 20, 20, 20, 20, 20, 21,
    21, 21, 21, 21, 21, 22, 22, 22,
    22, 22, 22, 22, 23, 23, 23, 23,
    23, 23, 23, 23, 24, 24, 24, 25,
    24, 24, 24, 25, 26, 26, 26, 26,
    25, 27, 27, 27, 27, 27, 28, 28,
    28, 28, 30, 30, 30, 31, 31, 33,
}};

// Quantization level numerators (from binkdata.h)
constexpr std::array<uint8_t, QUANT_LEVELS> binkb_num = {{
    1, 4, 5, 2, 7, 8, 3, 7, 4, 9, 5, 6, 7, 8, 9, 10
}};

// Quantization level denominators (from binkdata.h)
constexpr std::array<uint8_t, QUANT_LEVELS> binkb_den = {{
    1, 3, 3, 1, 3, 3, 1, 2, 1, 2, 1, 1, 1, 1, 1, 1
}};

/**
 * Generate inverse scan table at compile time
 *
 * The inverse scan maps from sequential order back to scan order.
 * For each position i, inv_bink_scan[bink_scan[i]] = i
 */
constexpr auto generate_inv_bink_scan() noexcept {
    std::array<uint8_t, DCT_BLOCK_SIZE> inv{};
    for (int i = 0; i < DCT_BLOCK_SIZE; ++i) {
        inv[bink_scan[i]] = static_cast<uint8_t>(i);
    }
    return inv;
}

constexpr auto inv_bink_scan = generate_inv_bink_scan();

/**
 * Generate Bink intra-frame quantization table at compile time
 *
 * Formula: intra_quant[j][k] = intra_seed[i] × s[i] × num[j] / (den[j] × (C>>12))
 *
 * Process:
 * 1. For each quantization level j (0-15)
 * 2. For each DCT coefficient i (0-63)
 * 3. Get reordered index k = inv_bink_scan[i]
 * 4. Calculate: seed × scaling × (num/den) / fixed_point_divisor
 *
 * The division by (C>>12) = 262144 converts from fixed-point back to integer.
 * This balances the s[i] scaling factors which are in fixed-point format.
 */
constexpr auto generate_binkb_intra_quant() noexcept {
    std::array<std::array<int32_t, DCT_BLOCK_SIZE>, QUANT_LEVELS> table{};

    for (int j = 0; j < QUANT_LEVELS; ++j) {
        for (int i = 0; i < DCT_BLOCK_SIZE; ++i) {
            int k = inv_bink_scan[i];
            
            // Calculate: seed[i] * s[i] * num[j] / (den[j] * (C>>12))
            int64_t numerator = static_cast<int64_t>(binkb_intra_seed[i]) *
                                static_cast<int64_t>(s[i]) *
                                static_cast<int64_t>(binkb_num[j]);
            int64_t denominator = static_cast<int64_t>(binkb_den[j]) * (C >> 12);
            
            table[j][k] = static_cast<int32_t>(numerator / denominator);
        }
    }

    return table;
}

/**
 * Generate Bink inter-frame quantization table at compile time
 *
 * Formula: inter_quant[j][k] = inter_seed[i] × s[i] × num[j] / (den[j] × (C>>12))
 *
 * Same algorithm as intra, but uses inter_seed instead of intra_seed.
 * Inter-frame coding uses different quantization to account for temporal
 * prediction differences.
 */
constexpr auto generate_binkb_inter_quant() noexcept {
    std::array<std::array<int32_t, DCT_BLOCK_SIZE>, QUANT_LEVELS> table{};

    for (int j = 0; j < QUANT_LEVELS; ++j) {
        for (int i = 0; i < DCT_BLOCK_SIZE; ++i) {
            int k = inv_bink_scan[i];
            
            // Calculate: seed[i] * s[i] * num[j] / (den[j] * (C>>12))
            int64_t numerator = static_cast<int64_t>(binkb_inter_seed[i]) *
                                static_cast<int64_t>(s[i]) *
                                static_cast<int64_t>(binkb_num[j]);
            int64_t denominator = static_cast<int64_t>(binkb_den[j]) * (C >> 12);
            
            table[j][k] = static_cast<int32_t>(numerator / denominator);
        }
    }

    return table;
}

// Generate tables at compile time
constexpr auto binkb_intra_quant = generate_binkb_intra_quant();
constexpr auto binkb_inter_quant = generate_binkb_inter_quant();

// Total: 1,024 + 1,024 = 2,048 int32_t entries (8,192 bytes)!

// Compile-time validation
namespace tests {
    // Test table dimensions
    static_assert(binkb_intra_quant.size() == QUANT_LEVELS, "Intra quant levels = 16");
    static_assert(binkb_intra_quant[0].size() == DCT_BLOCK_SIZE, "Intra DCT size = 64");
    static_assert(binkb_inter_quant.size() == QUANT_LEVELS, "Inter quant levels = 16");
    static_assert(binkb_inter_quant[0].size() == DCT_BLOCK_SIZE, "Inter DCT size = 64");

    // Test inverse scan generation
    // inv_bink_scan[bink_scan[i]] should equal i for all i
    static_assert(inv_bink_scan[bink_scan[0]] == 0, "Inv scan check 0");
    static_assert(inv_bink_scan[bink_scan[10]] == 10, "Inv scan check 10");
    static_assert(inv_bink_scan[bink_scan[32]] == 32, "Inv scan check 32");
    static_assert(inv_bink_scan[bink_scan[63]] == 63, "Inv scan check 63");

    // Test that tables are populated (non-zero for most entries)
    static_assert(binkb_intra_quant[0][0] != 0, "Intra table populated");
    static_assert(binkb_inter_quant[0][0] != 0, "Inter table populated");

    // Test that intra and inter differ (different seed values)
    // Note: [0][0] happens to be equal since both seeds start at 16
    // But most other entries differ
    static_assert(binkb_intra_quant[0][63] != binkb_inter_quant[0][63],
                  "Intra differs from inter at high freq");
    static_assert(binkb_intra_quant[5][32] != binkb_inter_quant[5][32],
                  "Intra differs from inter mid-band");

    // Test quantization level variations
    // Higher quantization levels should generally have higher values (coarser quantization)
    static_assert(binkb_intra_quant[0][0] < binkb_intra_quant[15][0],
                  "Quant increases with level");

    // Test that scaling is reasonable (not overflowing)
    static_assert(binkb_intra_quant[5][32] > 0, "Positive quant value");
    static_assert(binkb_inter_quant[5][32] > 0, "Positive quant value 2");

    // Test seed table differences
    static_assert(binkb_intra_seed[0] == 16, "Intra seed start");
    static_assert(binkb_inter_seed[0] == 16, "Inter seed start");
    static_assert(binkb_intra_seed[63] == 83, "Intra seed end");
    static_assert(binkb_inter_seed[63] == 33, "Inter seed end");
    static_assert(binkb_intra_seed[63] != binkb_inter_seed[63],
                  "Seeds differ at end");

    // Test num/den tables
    static_assert(binkb_num[0] == 1, "Num first entry");
    static_assert(binkb_den[0] == 1, "Den first entry");
    static_assert(binkb_num[15] == 10, "Num last entry");
    static_assert(binkb_den[15] == 1, "Den last entry");

    // Test s[] scaling factors
    static_assert(s[0] == 1073741824, "s[0] = C");
    static_assert(s[1] > s[0], "s[1] > s[0]");

    // Verify C constant
    static_assert(C == (1LL << 30), "C = 2^30");
    static_assert((C >> 12) == 262144, "C>>12 = 262144");
}

/**
 * Constexpr accessors with bounds checking
 */
constexpr int32_t get_intra_quant(int level, int coef) noexcept {
    if (level < 0 || level >= QUANT_LEVELS || coef < 0 || coef >= DCT_BLOCK_SIZE) {
        return 0;
    }
    return binkb_intra_quant[level][coef];
}

constexpr int32_t get_inter_quant(int level, int coef) noexcept {
    if (level < 0 || level >= QUANT_LEVELS || coef < 0 || coef >= DCT_BLOCK_SIZE) {
        return 0;
    }
    return binkb_inter_quant[level][coef];
}

/**
 * Get raw table pointers for C interop
 */
inline const int32_t (*get_binkb_intra_quant())[DCT_BLOCK_SIZE] {
    return reinterpret_cast<const int32_t(*)[DCT_BLOCK_SIZE]>(binkb_intra_quant.data());
}

inline const int32_t (*get_binkb_inter_quant())[DCT_BLOCK_SIZE] {
    return reinterpret_cast<const int32_t(*)[DCT_BLOCK_SIZE]>(binkb_inter_quant.data());
}

} // namespace bink
} // namespace ffmpeg

#endif // AVCODEC_BINK_TABLEGEN_CONSTEXPR_HPP
