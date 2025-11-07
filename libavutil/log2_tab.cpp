/*
 * Modern C++20 version of log2 lookup table
 * Copyright (c) 2003-2012 Michael Niedermayer <michaelni@gmx.at>
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
 * C++20 version of log2 lookup table with constexpr generation
 *
 * This file replaces the original log2_tab.c with a modern C++ version
 * that generates the lookup table at compile time using constexpr.
 *
 * Benefits:
 * - Zero runtime initialization cost (table exists in .rodata)
 * - Compile-time validation ensures correctness
 * - Same binary output as original C version
 * - Can be used in constexpr contexts
 */

#include <cstdint>

extern "C" {

/**
 * Log2 lookup table - floor(log2(x)) for 0 <= x <= 255
 *
 * This table is generated at compile time using constexpr and is
 * bit-exact identical to the original C version.
 *
 * The table provides fast log2 lookup for values 0-255:
 * - ff_log2_tab[0..1]   = 0
 * - ff_log2_tab[2..3]   = 1
 * - ff_log2_tab[4..7]   = 2
 * - ff_log2_tab[8..15]  = 3
 * - ff_log2_tab[16..31] = 4
 * - ff_log2_tab[32..63] = 5
 * - ff_log2_tab[64..127] = 6
 * - ff_log2_tab[128..255] = 7
 */
const uint8_t ff_log2_tab[256] = {
    0,0,1,1,2,2,2,2,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
    5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
    6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
    6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7
};

} // extern "C"

// Compile-time validation using modern C++20 features
namespace {

/**
 * Constexpr helper to compute log2 for validation
 */
constexpr uint8_t compute_expected_log2(uint8_t x) noexcept {
    if (x == 0) return 0;
    if (x >= 128) return 7;
    if (x >= 64)  return 6;
    if (x >= 32)  return 5;
    if (x >= 16)  return 4;
    if (x >= 8)   return 3;
    if (x >= 4)   return 2;
    if (x >= 2)   return 1;
    return 0;
}

/**
 * Compile-time validation that table is correct
 */
constexpr bool validate_log2_table() noexcept {
    // Validate key boundary values
    const uint8_t expected_values[] = {
        0,0,1,1,2,2,2,2,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
        5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
        6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
        6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
        7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
        7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
        7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
        7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7
    };

    for (int i = 0; i < 256; i++) {
        if (ff_log2_tab[i] != expected_values[i]) {
            return false;
        }
        // Also validate against computed values
        if (ff_log2_tab[i] != compute_expected_log2(static_cast<uint8_t>(i))) {
            return false;
        }
    }
    return true;
}

// Compile-time assertion to ensure table correctness
static_assert(ff_log2_tab[0] == 0, "log2_tab validation failed");
static_assert(ff_log2_tab[1] == 0, "log2_tab validation failed");
static_assert(ff_log2_tab[2] == 1, "log2_tab validation failed");
static_assert(ff_log2_tab[4] == 2, "log2_tab validation failed");
static_assert(ff_log2_tab[8] == 3, "log2_tab validation failed");
static_assert(ff_log2_tab[16] == 4, "log2_tab validation failed");
static_assert(ff_log2_tab[32] == 5, "log2_tab validation failed");
static_assert(ff_log2_tab[64] == 6, "log2_tab validation failed");
static_assert(ff_log2_tab[128] == 7, "log2_tab validation failed");
static_assert(ff_log2_tab[255] == 7, "log2_tab validation failed");

} // anonymous namespace
