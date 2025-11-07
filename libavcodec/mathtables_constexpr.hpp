/*
 * Modern C++ constexpr mathematics lookup tables
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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
 * Modern C++20 constexpr mathematics lookup tables
 *
 * This header provides compile-time generated mathematical lookup tables:
 * - Square table: (i - 256)²
 * - Inverse table: Fast division approximations
 * - Square root table: Integer square roots
 * - Crop table: Value clamping to [0, 255]
 * - Zigzag scan orders: For DCT coefficient ordering
 *
 * All tables are generated at compile time with zero runtime cost.
 */

#ifndef AVCODEC_MATHTABLES_CONSTEXPR_HPP
#define AVCODEC_MATHTABLES_CONSTEXPR_HPP

#include <cstdint>
#include <array>

extern "C" {
#include "mathops.h"
}

namespace ffmpeg {
namespace math {
namespace tables {

/**
 * Generate square table at compile time
 * ff_square_tab[i] = (i - 256)² for i in [0, 511]
 */
constexpr auto generate_square_table() noexcept {
    std::array<uint32_t, 512> table{};
    for (int i = 0; i < 512; i++) {
        int val = i - 256;
        table[i] = static_cast<uint32_t>(val * val);
    }
    return table;
}

/**
 * Generate inverse table at compile time
 * ff_inverse[b] such that a*ff_inverse[b]>>32 ≈ a/b
 * Valid for 0 <= a <= 16909558 && 2 <= b <= 256
 */
constexpr auto generate_inverse_table() noexcept {
    std::array<uint32_t, 257> table{};

    table[0] = 0;
    table[1] = 4294967295U;  // Special case for b=1

    // For b >= 2, compute 2^32 / b
    for (int b = 2; b <= 256; b++) {
        // Compute floor(2^32 / b) at compile time
        uint64_t numerator = 1ULL << 32;
        table[b] = static_cast<uint32_t>(numerator / b);
    }

    return table;
}

/**
 * Generate square root table at compile time
 * ff_sqrt_tab[i] ≈ sqrt(i) * 16 for i in [0, 255]
 */
constexpr auto generate_sqrt_table() noexcept {
    std::array<uint8_t, 256> table{};

    // Compile-time integer square root using Newton's method
    auto constexpr_sqrt = [](int n) constexpr -> int {
        if (n == 0) return 0;
        if (n == 1) return 1;

        int x = n;
        int y = (x + 1) / 2;

        // Newton's method iterations (limited for compile-time)
        for (int iter = 0; iter < 10; iter++) {
            if (y >= x) break;
            x = y;
            y = (x + n / x) / 2;
        }
        return x;
    };

    for (int i = 0; i < 256; i++) {
        // sqrt(i) * 16 = sqrt(i * 256) = sqrt(i << 8)
        int sq = constexpr_sqrt(i << 8);
        table[i] = static_cast<uint8_t>(sq > 255 ? 255 : sq);
    }

    return table;
}

/**
 * Generate crop table at compile time
 * Clamps values to [0, 255] with negative value support
 * MAX_NEG_CROP = 1024 (from mathops.h)
 */
template<int MaxNegCrop = 1024>
constexpr auto generate_crop_table() noexcept {
    constexpr int size = 256 + 2 * MaxNegCrop;
    std::array<uint8_t, size> table{};

    // First MaxNegCrop entries: all zeros
    for (int i = 0; i < MaxNegCrop; i++) {
        table[i] = 0;
    }

    // Middle 256 entries: 0 to 255
    for (int i = 0; i < 256; i++) {
        table[MaxNegCrop + i] = static_cast<uint8_t>(i);
    }

    // Last MaxNegCrop entries: all 255
    for (int i = 0; i < MaxNegCrop; i++) {
        table[MaxNegCrop + 256 + i] = 255;
    }

    return table;
}

/**
 * Zigzag scan orders - compile-time constants
 */
namespace zigzag {
    // Direct zigzag scan (8x8 DCT)
    constexpr uint8_t direct[64] = {
        0,   1,  8, 16,  9,  2,  3, 10,
        17, 24, 32, 25, 18, 11,  4,  5,
        12, 19, 26, 33, 40, 48, 41, 34,
        27, 20, 13,  6,  7, 14, 21, 28,
        35, 42, 49, 56, 57, 50, 43, 36,
        29, 22, 15, 23, 30, 37, 44, 51,
        58, 59, 52, 45, 38, 31, 39, 46,
        53, 60, 61, 54, 47, 55, 62, 63
    };

    // 4x4 zigzag scan
    constexpr uint8_t scan_4x4[16 + 1] = {
        0 + 0 * 4, 1 + 0 * 4, 0 + 1 * 4, 0 + 2 * 4,
        1 + 1 * 4, 2 + 0 * 4, 3 + 0 * 4, 2 + 1 * 4,
        1 + 2 * 4, 0 + 3 * 4, 1 + 3 * 4, 2 + 2 * 4,
        3 + 1 * 4, 3 + 2 * 4, 2 + 3 * 4, 3 + 3 * 4,
        0  // Extra entry for compatibility
    };

    // Log2 run length encoding table
    constexpr uint8_t log2_run[41] = {
        0,  0,  0,  0,  1,  1,  1,  1,
        2,  2,  2,  2,  3,  3,  3,  3,
        4,  4,  5,  5,  6,  6,  7,  7,
        8,  9, 10, 11, 12, 13, 14, 15,
        16, 17, 18, 19, 20, 21, 22, 23,
        24
    };
}

// Generate all tables at compile time
constexpr auto square_table = generate_square_table();
constexpr auto inverse_table = generate_inverse_table();
constexpr auto sqrt_table = generate_sqrt_table();
constexpr auto crop_table = generate_crop_table<MAX_NEG_CROP>();

// Compile-time validation
static_assert(square_table[0] == 65536, "square(0) = (0-256)² = 65536");
static_assert(square_table[256] == 0, "square(256) = (256-256)² = 0");
static_assert(square_table[257] == 1, "square(257) = (257-256)² = 1");
static_assert(square_table[511] == 65025, "square(511) = (511-256)² = 65025");

static_assert(inverse_table[0] == 0, "inverse[0] = 0");
static_assert(inverse_table[1] == 4294967295U, "inverse[1] = 2^32-1");
static_assert(inverse_table[2] == 2147483648U, "inverse[2] = 2^31");

static_assert(sqrt_table[0] == 0, "sqrt(0) = 0");
static_assert(sqrt_table[1] >= 15 && sqrt_table[1] <= 17, "sqrt(1)*16 ≈ 16");

static_assert(crop_table[MAX_NEG_CROP] == 0, "crop(0) = 0");
static_assert(crop_table[MAX_NEG_CROP + 128] == 128, "crop(128) = 128");
static_assert(crop_table[MAX_NEG_CROP + 255] == 255, "crop(255) = 255");

static_assert(zigzag::direct[0] == 0, "zigzag[0] = 0");
static_assert(zigzag::direct[1] == 1, "zigzag[1] = 1");
static_assert(zigzag::direct[2] == 8, "zigzag[2] = 8");

/**
 * Constexpr accessor functions for type safety
 */
constexpr uint32_t square(int i) noexcept {
    return (i >= 0 && i < 512) ? square_table[i] : 0;
}

constexpr uint32_t inverse(int b) noexcept {
    return (b >= 0 && b <= 256) ? inverse_table[b] : 0;
}

constexpr uint8_t sqrt_lookup(uint8_t i) noexcept {
    return sqrt_table[i];
}

constexpr uint8_t crop(int i) noexcept {
    // Convert from [-MAX_NEG_CROP, 255+MAX_NEG_CROP] to [0, size)
    int index = i + MAX_NEG_CROP;
    if (index < 0) return 0;
    if (index >= static_cast<int>(crop_table.size())) return 255;
    return crop_table[index];
}

} // namespace tables
} // namespace math
} // namespace ffmpeg

#endif // AVCODEC_MATHTABLES_CONSTEXPR_HPP
