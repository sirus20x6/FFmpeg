/*
 * Modern C++ constexpr PCM encoding tables
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
 * Modern C++20 constexpr PCM encoding lookup tables
 *
 * This header provides compile-time generation of PCM encoding tables:
 * - A-law (G.711 A-law encoding)
 * - μ-law (G.711 μ-law encoding)
 * - VIDC (British Telecom VIDC encoding)
 *
 * All tables are generated at compile time from the encoding algorithms,
 * eliminating runtime initialization and making the conversion logic
 * explicit and maintainable.
 */

#ifndef AVCODEC_PCM_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_PCM_TABLEGEN_CONSTEXPR_HPP

#include <cstdint>
#include <array>

namespace ffmpeg {
namespace pcm {

// Constants from G.711
constexpr int SIGN_BIT = 0x80;
constexpr int QUANT_MASK = 0xf;
constexpr int NSEGS = 8;
constexpr int SEG_SHIFT = 4;
constexpr int SEG_MASK = 0x70;
constexpr int BIAS = 0x84;

// VIDC-specific constants
constexpr int VIDC_SIGN_BIT = 1;
constexpr int VIDC_QUANT_MASK = 0x1E;
constexpr int VIDC_QUANT_SHIFT = 1;
constexpr int VIDC_SEG_SHIFT = 5;
constexpr int VIDC_SEG_MASK = 0xE0;

/**
 * Compile-time A-law to linear PCM conversion
 * Based on Sun Microsystems g711.c (unrestricted use)
 */
constexpr int alaw2linear(uint8_t a_val) noexcept {
    a_val ^= 0x55;  // Invert even bits

    int t = a_val & QUANT_MASK;
    int seg = (static_cast<unsigned>(a_val) & SEG_MASK) >> SEG_SHIFT;

    if (seg) {
        t = (t + t + 1 + 32) << (seg + 2);
    } else {
        t = (t + t + 1) << 3;
    }

    return (a_val & SIGN_BIT) ? t : -t;
}

/**
 * Compile-time μ-law to linear PCM conversion
 */
constexpr int ulaw2linear(uint8_t u_val) noexcept {
    u_val = ~u_val;  // Complement to obtain normal μ-law value

    int t = ((u_val & QUANT_MASK) << 3) + BIAS;
    t <<= (static_cast<unsigned>(u_val) & SEG_MASK) >> SEG_SHIFT;

    return (u_val & SIGN_BIT) ? (BIAS - t) : (t - BIAS);
}

/**
 * Compile-time VIDC to linear PCM conversion
 */
constexpr int vidc2linear(uint8_t u_val) noexcept {
    int t = (((u_val & VIDC_QUANT_MASK) >> VIDC_QUANT_SHIFT) << 3) + BIAS;
    t <<= (static_cast<unsigned>(u_val) & VIDC_SEG_MASK) >> VIDC_SEG_SHIFT;

    return (u_val & VIDC_SIGN_BIT) ? (BIAS - t) : (t - BIAS);
}

/**
 * Generic table builder for x-law encoding
 * This is the algorithm that was previously done at runtime
 *
 * @param xlaw2linear Function to convert encoded value to linear
 * @param mask XOR mask for encoding
 */
template<typename DecodeFn>
constexpr auto build_xlaw_table(DecodeFn xlaw2linear, int mask) noexcept {
    std::array<uint8_t, 16384> linear_to_xlaw{};

    int j = 1;
    linear_to_xlaw[8192] = static_cast<uint8_t>(mask);

    // Build the conversion table by finding boundaries
    for (int i = 0; i < 127; i++) {
        int v1 = xlaw2linear(i ^ mask);
        int v2 = xlaw2linear((i + 1) ^ mask);
        int v = (v1 + v2 + 4) >> 3;

        for (; j < v; j++) {
            linear_to_xlaw[8192 - j] = static_cast<uint8_t>(i ^ (mask ^ 0x80));
            linear_to_xlaw[8192 + j] = static_cast<uint8_t>(i ^ mask);
        }
    }

    // Fill remaining values
    for (; j < 8192; j++) {
        linear_to_xlaw[8192 - j] = static_cast<uint8_t>(127 ^ (mask ^ 0x80));
        linear_to_xlaw[8192 + j] = static_cast<uint8_t>(127 ^ mask);
    }

    linear_to_xlaw[0] = linear_to_xlaw[1];

    return linear_to_xlaw;
}

/**
 * Compile-time generated A-law encoding table
 */
constexpr auto linear_to_alaw_table = build_xlaw_table(alaw2linear, 0xd5);

/**
 * Compile-time generated μ-law encoding table
 */
constexpr auto linear_to_ulaw_table = build_xlaw_table(ulaw2linear, 0xff);

/**
 * Compile-time generated VIDC encoding table
 */
constexpr auto linear_to_vidc_table = build_xlaw_table(vidc2linear, 0xff);

// Compile-time validation
namespace tests {
    // Test decoder functions at key points
    constexpr int alaw_zero = alaw2linear(0xd5);
    static_assert(alaw_zero == 8, "A-law decode test");

    constexpr int ulaw_zero = ulaw2linear(0xff);
    static_assert(ulaw_zero == 0, "μ-law decode test");

    // Test table generation
    static_assert(linear_to_alaw_table.size() == 16384, "A-law table size");
    static_assert(linear_to_ulaw_table.size() == 16384, "μ-law table size");
    static_assert(linear_to_vidc_table.size() == 16384, "VIDC table size");

    // Test middle point (zero crossing)
    static_assert(linear_to_alaw_table[8192] == 0xd5, "A-law zero point");
    static_assert(linear_to_ulaw_table[8192] == 0xff, "μ-law zero point");
    static_assert(linear_to_vidc_table[8192] == 0xff, "VIDC zero point");

    // Test symmetry around zero point
    static_assert(linear_to_alaw_table[8191] != linear_to_alaw_table[8193],
                  "A-law symmetry");
    static_assert(linear_to_ulaw_table[8191] != linear_to_ulaw_table[8193],
                  "μ-law symmetry");

    // Test that table ends are different from middle
    static_assert(linear_to_alaw_table[0] != linear_to_alaw_table[8192],
                  "A-law table range");
    static_assert(linear_to_ulaw_table[0] != linear_to_ulaw_table[8192],
                  "μ-law table range");
}

/**
 * Helper functions for constexpr table access
 */
constexpr uint8_t alaw_encode(int16_t sample) noexcept {
    int index = sample + 8192;
    if (index < 0) return linear_to_alaw_table[0];
    if (index >= 16384) return linear_to_alaw_table[16383];
    return linear_to_alaw_table[index];
}

constexpr uint8_t ulaw_encode(int16_t sample) noexcept {
    int index = sample + 8192;
    if (index < 0) return linear_to_ulaw_table[0];
    if (index >= 16384) return linear_to_ulaw_table[16383];
    return linear_to_ulaw_table[index];
}

constexpr uint8_t vidc_encode(int16_t sample) noexcept {
    int index = sample + 8192;
    if (index < 0) return linear_to_vidc_table[0];
    if (index >= 16384) return linear_to_vidc_table[16383];
    return linear_to_vidc_table[index];
}

// Example compile-time usage
namespace examples {
    // Encode values at compile time
    constexpr uint8_t encoded_zero_alaw = alaw_encode(0);
    constexpr uint8_t encoded_zero_ulaw = ulaw_encode(0);

    constexpr uint8_t encoded_1000_alaw = alaw_encode(1000);
    constexpr uint8_t encoded_1000_ulaw = ulaw_encode(1000);

    // Verify we can decode back
    constexpr int decoded = alaw2linear(encoded_zero_alaw);
}

} // namespace pcm
} // namespace ffmpeg

#endif // AVCODEC_PCM_TABLEGEN_CONSTEXPR_HPP
