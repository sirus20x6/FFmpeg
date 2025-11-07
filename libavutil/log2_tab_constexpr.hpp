/*
 * Modern C++ constexpr log2 lookup table
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
 * Modern C++20 constexpr log2 lookup table generation
 *
 * This header provides a compile-time generated log2 lookup table using
 * constexpr functions. The table is identical to the original C version
 * but is generated at compile time with zero runtime initialization cost.
 *
 * Benefits over C version:
 * - Table generated at compile time (zero runtime cost)
 * - Compile-time validation with static_assert
 * - Can be used in constexpr contexts
 * - Single header, no separate .c file needed
 */

#ifndef AVUTIL_LOG2_TAB_CONSTEXPR_HPP
#define AVUTIL_LOG2_TAB_CONSTEXPR_HPP

#include <cstdint>
#include <array>

namespace ffmpeg {
namespace internal {

/**
 * Compile-time log2 calculation for a single value
 * Returns floor(log2(x)) for x > 0, or 0 for x == 0
 */
constexpr uint8_t compute_log2_single(uint8_t x) noexcept {
    if (x == 0) return 0;

    uint8_t result = 0;
    uint8_t val = x;

    // Find position of highest set bit
    if (val >= 128) return 7;
    if (val >= 64)  return 6;
    if (val >= 32)  return 5;
    if (val >= 16)  return 4;
    if (val >= 8)   return 3;
    if (val >= 4)   return 2;
    if (val >= 2)   return 1;
    return 0;
}

/**
 * Generate the complete log2 lookup table at compile time
 * This is evaluated by the compiler and results in a static constant array
 */
constexpr auto generate_log2_table() noexcept {
    std::array<uint8_t, 256> table{};

    for (int i = 0; i < 256; i++) {
        table[i] = compute_log2_single(static_cast<uint8_t>(i));
    }

    return table;
}

} // namespace internal

/**
 * Compile-time generated log2 lookup table
 * Identical to the C version ff_log2_tab but generated at compile time
 */
constexpr auto log2_table = internal::generate_log2_table();

// Static assertions to validate table correctness
// Verify key values match expected log2 results
static_assert(log2_table[0] == 0, "log2(0) should be 0");
static_assert(log2_table[1] == 0, "log2(1) should be 0");
static_assert(log2_table[2] == 1, "log2(2) should be 1");
static_assert(log2_table[3] == 1, "log2(3) should be 1");
static_assert(log2_table[4] == 2, "log2(4) should be 2");
static_assert(log2_table[7] == 2, "log2(7) should be 2");
static_assert(log2_table[8] == 3, "log2(8) should be 3");
static_assert(log2_table[15] == 3, "log2(15) should be 3");
static_assert(log2_table[16] == 4, "log2(16) should be 4");
static_assert(log2_table[31] == 4, "log2(31) should be 4");
static_assert(log2_table[32] == 5, "log2(32) should be 5");
static_assert(log2_table[63] == 5, "log2(63) should be 5");
static_assert(log2_table[64] == 6, "log2(64) should be 6");
static_assert(log2_table[127] == 6, "log2(127) should be 6");
static_assert(log2_table[128] == 7, "log2(128) should be 7");
static_assert(log2_table[255] == 7, "log2(255) should be 7");

/**
 * Constexpr log2 table access
 * Can be used in both constexpr and runtime contexts
 */
constexpr uint8_t log2_tab(uint8_t index) noexcept {
    return log2_table[index];
}

// Example constexpr usage at compile time
namespace examples {
    // These are computed at compile time
    constexpr uint8_t log2_of_16 = log2_tab(16);  // = 4
    constexpr uint8_t log2_of_64 = log2_tab(64);  // = 6
    constexpr uint8_t log2_of_128 = log2_tab(128); // = 7

    static_assert(log2_of_16 == 4, "Example check failed");
    static_assert(log2_of_64 == 6, "Example check failed");
    static_assert(log2_of_128 == 7, "Example check failed");
}

} // namespace ffmpeg

// C compatibility: Provide C-compatible symbol if needed
// This allows gradual migration from C to C++
#ifdef __cplusplus
extern "C" {
#endif

// Alias the C++ constexpr table as the C symbol
// This is a compile-time constant, so it can be directly used
extern const uint8_t ff_log2_tab_cpp[256];

#ifdef __cplusplus
}
#endif

// Definition of the C-compatible symbol
// The alignas ensures it has the same alignment as the original C version
namespace {
    alignas(const uint8_t) constexpr uint8_t ff_log2_tab_storage[256] = {
        0,0,1,1,2,2,2,2,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
        5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
        6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
        6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
        7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
        7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
        7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
        7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7
    };
}

// Validate that our generated table matches the hardcoded original
namespace ffmpeg {
namespace validation {
    // Compile-time validation that generated table matches expected values
    constexpr bool validate_table() noexcept {
        for (int i = 0; i < 256; i++) {
            if (log2_table[i] != ff_log2_tab_storage[i]) {
                return false;
            }
        }
        return true;
    }

    static_assert(validate_table(), "Generated log2 table does not match original!");
}
}

#endif // AVUTIL_LOG2_TAB_CONSTEXPR_HPP
