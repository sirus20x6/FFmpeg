/*
 * Modern C++20 template-based fixed-point DSP operations
 * Copyright (c) 2012 MIPS Technologies, Inc., California
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
 * Modern C++20 version of fixed-point DSP operations
 *
 * This file replaces fixed_dsp.c with a template-based C++ implementation.
 *
 * Benefits:
 * - Template-based operations for type flexibility
 * - Better inlining and optimization opportunities
 * - Type safety (can't accidentally mix fixed/float)
 * - Constexpr support for compile-time evaluation
 * - C ABI compatibility maintained
 */

#include "common.h"
#include "fixed_dsp.h"
#include "mem.h"

// Include template implementations
#include "fixed_dsp_constexpr.hpp"

using namespace ffmpeg::fixed_dsp;

// C-compatible function implementations using templates
extern "C" {

static void vector_fmul_add_c(int *dst, const int *src0, const int *src1,
                              const int *src2, int len) {
    vector_fmul_add(dst, src0, src1, src2, len);
}

static void vector_fmul_reverse_c(int *dst, const int *src0,
                                  const int *src1, int len) {
    vector_fmul_reverse(dst, src0, src1, len);
}

static void vector_fmul_window_scaled_c(int16_t *dst, const int32_t *src0,
                                       const int32_t *src1, const int32_t *win,
                                       int len, uint8_t bits) {
    int32_t s0, s1, wi, wj, i, j, round;

    dst += len;
    win += len;
    src0 += len;
    round = bits ? 1 << (bits - 1) : 0;

    for (i = -len, j = len - 1; i < 0; i++, j--) {
        s0 = src0[i];
        s1 = src1[j];
        wi = win[i];
        wj = win[j];
        dst[i] = av_clip_int16(((((int64_t)s0 * wj - (int64_t)s1 * wi + 0x40000000) >> 31) + round) >> bits);
        dst[j] = av_clip_int16(((((int64_t)s0 * wi + (int64_t)s1 * wj + 0x40000000) >> 31) + round) >> bits);
    }
}

static void vector_fmul_window_c(int32_t *dst, const int32_t *src0,
                                const int32_t *src1, const int32_t *win,
                                int len) {
    int32_t s0, s1, wi, wj, i, j;

    dst += len;
    win += len;
    src0 += len;

    for (i = -len, j = len - 1; i < 0; i++, j--) {
        s0 = src0[i];
        s1 = src1[j];
        wi = win[i];
        wj = win[j];
        dst[i] = ((int64_t)s0 * wj - (int64_t)s1 * wi + 0x40000000) >> 31;
        dst[j] = ((int64_t)s0 * wi + (int64_t)s1 * wj + 0x40000000) >> 31;
    }
}

static void vector_fmul_c(int *dst, const int *src0, const int *src1, int len) {
    vector_fmul(dst, src0, src1, len);
}

static int scalarproduct_fixed_c(const int *v1, const int *v2, int len) {
    return scalarproduct_fixed(v1, v2, len);
}

static void butterflies_fixed_c(int *restrict v1s, int *restrict v2, int len) {
    butterflies_fixed(v1s, v2, len);
}

AVFixedDSPContext *avpriv_alloc_fixed_dsp(int bit_exact) {
    AVFixedDSPContext *fdsp = static_cast<AVFixedDSPContext*>(
        av_malloc(sizeof(AVFixedDSPContext)));

    if (!fdsp)
        return nullptr;

    fdsp->vector_fmul_window_scaled = vector_fmul_window_scaled_c;
    fdsp->vector_fmul_window = vector_fmul_window_c;
    fdsp->vector_fmul = vector_fmul_c;
    fdsp->vector_fmul_add = vector_fmul_add_c;
    fdsp->vector_fmul_reverse = vector_fmul_reverse_c;
    fdsp->butterflies_fixed = butterflies_fixed_c;
    fdsp->scalarproduct_fixed = scalarproduct_fixed_c;

#if ARCH_RISCV
    ff_fixed_dsp_init_riscv(fdsp);
#elif ARCH_X86
    ff_fixed_dsp_init_x86(fdsp);
#endif

    return fdsp;
}

} // extern "C"

// C++ compile-time validation
namespace {

using namespace ffmpeg::fixed_dsp;

// Validate template instantiations compile
static_assert(std::is_same_v<decltype(vector_fmul<int>), void(int*, const int*, const int*, int)>,
              "vector_fmul signature check");

static_assert(std::is_same_v<decltype(scalarproduct_fixed<int>), int(const int*, const int*, int)>,
              "scalarproduct_fixed signature check");

// Test constexpr operations
constexpr int test_array1[8] = {
    0x40000000, 0x30000000, 0x20000000, 0x10000000,
    0x08000000, 0x04000000, 0x02000000, 0x01000000
};

constexpr int test_array2[8] = {
    0x40000000, 0x40000000, 0x40000000, 0x40000000,
    0x40000000, 0x40000000, 0x40000000, 0x40000000
};

// Test scalar product at compile time
constexpr int test_dot_8 = scalarproduct_fixed_constexpr<8>(test_array1, test_array2);
static_assert(test_dot_8 > 0, "Compile-time dot product > 0");

// Test vector multiplication at compile time
constexpr int test_vec_mul() {
    int result[4] = {0, 0, 0, 0};
    const int a[4] = {0x40000000, 0x20000000, 0x10000000, 0x08000000};
    const int b[4] = {0x40000000, 0x40000000, 0x40000000, 0x40000000};
    vector_fmul_constexpr<4>(result, a, b);
    return result[0] + result[1] + result[2] + result[3];
}
constexpr int vec_mul_sum = test_vec_mul();
static_assert(vec_mul_sum > 0, "Vector multiplication test");

// Test Q31 fixed-point class
using Q31 = FixedPoint<31>;

constexpr Q31 test_q31_1(0.5);
constexpr Q31 test_q31_2(0.25);
constexpr Q31 test_q31_product = test_q31_1 * test_q31_2;
static_assert(test_q31_product.raw() != 0, "Q31 multiplication non-zero");

constexpr Q31 test_q31_sum = test_q31_1 + test_q31_2;
static_assert(test_q31_sum.raw() > test_q31_1.raw(), "Q31 addition test");

// Test type safety - these should compile
template<typename T>
void test_template_instantiation() {
    T arr1[4] = {}, arr2[4] = {}, dst[4] = {};
    vector_fmul(dst, arr1, arr2, 4);
    vector_fmul_add(dst, arr1, arr2, dst, 4);
    [[maybe_unused]] T result = scalarproduct_fixed(arr1, arr2, 4);
}

// Instantiate for common types to verify compilation
template void test_template_instantiation<int>();
template void test_template_instantiation<int32_t>();

} // anonymous namespace
