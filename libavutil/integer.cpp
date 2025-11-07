/*
 * Modern C++20 arbitrary precision integers
 * Copyright (c) 2004 Michael Niedermayer <michaelni@gmx.at>
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
 * Modern C++20 version of arbitrary precision integers
 *
 * This file replaces integer.c with a C++ implementation that provides:
 * - Constexpr operations where possible
 * - Operator overloading for natural syntax
 * - Type-safe comparisons
 * - C ABI compatibility
 *
 * Benefits over C version:
 * - Compile-time arithmetic with constexpr
 * - Natural operators: a + b instead of av_add_i(a, b)
 * - Type safety prevents mixing integers with other types
 * - Zero overhead abstractions (same codegen as C)
 */

#include <cstring>

extern "C" {
#include "integer.h"
#include "avassert.h"
#include "intmath.h"
}

// C-compatible implementations
extern "C" {

static const AVInteger zero_i = {};

AVInteger av_add_i(AVInteger a, AVInteger b) {
    int carry = 0;
    for (int i = 0; i < AV_INTEGER_SIZE; i++) {
        carry = (carry >> 16) + a.v[i] + b.v[i];
        a.v[i] = static_cast<uint16_t>(carry);
    }
    return a;
}

AVInteger av_sub_i(AVInteger a, AVInteger b) {
    int carry = 0;
    for (int i = 0; i < AV_INTEGER_SIZE; i++) {
        carry = (carry >> 16) + a.v[i] - b.v[i];
        a.v[i] = static_cast<uint16_t>(carry);
    }
    return a;
}

int av_log2_i(AVInteger a) {
    for (int i = AV_INTEGER_SIZE - 1; i >= 0; i--) {
        if (a.v[i]) {
            return av_log2_16bit(a.v[i]) + 16 * i;
        }
    }
    return -1;
}

AVInteger av_mul_i(AVInteger a, AVInteger b) {
    AVInteger out = {};
    int na = (av_log2_i(a) + 16) >> 4;
    int nb = (av_log2_i(b) + 16) >> 4;

    for (int i = 0; i < na; i++) {
        unsigned int carry = 0;
        if (a.v[i]) {
            for (int j = i; j < AV_INTEGER_SIZE && j - i <= nb; j++) {
                carry = (carry >> 16) + out.v[j] +
                        a.v[i] * static_cast<unsigned>(b.v[j - i]);
                out.v[j] = static_cast<uint16_t>(carry);
            }
        }
    }
    return out;
}

int av_cmp_i(AVInteger a, AVInteger b) {
    int v = static_cast<int16_t>(a.v[AV_INTEGER_SIZE - 1]) -
            static_cast<int16_t>(b.v[AV_INTEGER_SIZE - 1]);
    if (v) return (v >> 16) | 1;

    for (int i = AV_INTEGER_SIZE - 2; i >= 0; i--) {
        int v = a.v[i] - b.v[i];
        if (v) return (v >> 16) | 1;
    }
    return 0;
}

AVInteger av_shr_i(AVInteger a, int s) {
    AVInteger out = {};
    for (int i = 0; i < AV_INTEGER_SIZE; i++) {
        unsigned int index = i + (s >> 4);
        unsigned int v = 0;
        if (index + 1 < AV_INTEGER_SIZE) {
            v = a.v[index + 1] * (1U << 16);
        }
        if (index < AV_INTEGER_SIZE) {
            v |= a.v[index];
        }
        out.v[i] = static_cast<uint16_t>(v >> (s & 15));
    }
    return out;
}

AVInteger av_mod_i(AVInteger *quot, AVInteger a, AVInteger b) {
    int i = av_log2_i(a) - av_log2_i(b);
    AVInteger quot_temp;
    if (!quot) quot = &quot_temp;

    if (static_cast<int16_t>(a.v[AV_INTEGER_SIZE - 1]) < 0) {
        a = av_mod_i(quot, av_sub_i(zero_i, a), b);
        *quot = av_sub_i(zero_i, *quot);
        return av_sub_i(zero_i, a);
    }

    av_assert2(static_cast<int16_t>(a.v[AV_INTEGER_SIZE - 1]) >= 0 &&
               static_cast<int16_t>(b.v[AV_INTEGER_SIZE - 1]) >= 0);
    av_assert2(av_log2_i(b) >= 0);

    if (i > 0) {
        b = av_shr_i(b, -i);
    }

    std::memset(quot, 0, sizeof(AVInteger));

    while (i-- >= 0) {
        *quot = av_shr_i(*quot, -1);
        if (av_cmp_i(a, b) >= 0) {
            a = av_sub_i(a, b);
            quot->v[0] += 1;
        }
        b = av_shr_i(b, 1);
    }
    return a;
}

AVInteger av_div_i(AVInteger a, AVInteger b) {
    AVInteger quot;
    av_mod_i(&quot, a, b);
    return quot;
}

AVInteger av_int2i(int64_t a) {
    AVInteger out = {};
    for (int i = 0; i < AV_INTEGER_SIZE; i++) {
        out.v[i] = static_cast<uint16_t>(a);
        a >>= 16;
    }
    return out;
}

int64_t av_i2int(AVInteger a) {
    uint64_t out = a.v[3];
    for (int i = 2; i >= 0; i--) {
        out = (out << 16) | a.v[i];
    }
    return static_cast<int64_t>(out);
}

} // extern "C"

// C++ wrapper implementations
#include "integer_constexpr.hpp"

namespace ffmpeg {
namespace integer {

// Division with optional remainder
Integer divide(const Integer& a, const Integer& b, Integer* remainder) noexcept {
    AVInteger quot;
    AVInteger rem = av_mod_i(&quot, a.av_integer(), b.av_integer());

    if (remainder) {
        *remainder = Integer(rem);
    }
    return Integer(quot);
}

} // namespace integer
} // namespace ffmpeg

// Compile-time validation
namespace {

// Validate that our implementations match expected behavior
static_assert(sizeof(AVInteger) == AV_INTEGER_SIZE * sizeof(uint16_t),
              "AVInteger size mismatch");

// Test constexpr arithmetic at compile time
using namespace ffmpeg::integer;

constexpr Integer test_a{100};
constexpr Integer test_b{50};
constexpr Integer test_sum = test_a + test_b;
static_assert(test_sum.to_int64() == 150, "Constexpr addition failed");

constexpr Integer test_diff = test_a - test_b;
static_assert(test_diff.to_int64() == 50, "Constexpr subtraction failed");

constexpr Integer test_prod = Integer{10} * Integer{20};
static_assert(test_prod.to_int64() == 200, "Constexpr multiplication failed");

// Test comparisons
static_assert(test_a > test_b, "Constexpr comparison failed");
static_assert(test_b < test_a, "Constexpr comparison failed");
static_assert(test_a == Integer{100}, "Constexpr equality failed");

// Test shifts
constexpr Integer test_shift_val{64};
constexpr Integer test_shifted = test_shift_val >> 2;
static_assert(test_shifted.to_int64() == 16, "Constexpr shift failed");

// Test zero
static_assert(zero.is_zero(), "Zero test failed");
static_assert(!one.is_zero(), "One test failed");

// Test negative
constexpr Integer neg_val = -Integer{42};
static_assert(neg_val.to_int64() == -42, "Negation test failed");

} // anonymous namespace
