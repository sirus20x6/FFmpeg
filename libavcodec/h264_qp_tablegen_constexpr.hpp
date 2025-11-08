/*
 * Compile-time generation of H.264 quantization parameter tables
 *
 * Original C version from FFmpeg H.264 decoder
 * C++20 constexpr version created 2025-11-08
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

#ifndef AVCODEC_H264_QP_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_H264_QP_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>

namespace FFmpegH264QP {

// ============================================================================
// Constants
// ============================================================================

// H.264 quantization parameter range:
// - Base QP range: 0-51 (standard 8-bit)
// - Extended for high bit depths: up to 51 + 6*(bit_depth-8)
// - Maximum supported: 51 + 6*6 = 87 (for 14-bit)
constexpr int QP_MAX_NUM = 51 + 6 * 6;  // 87
constexpr int QP_TABLE_SIZE = QP_MAX_NUM + 1;  // 88 entries (0-87)

// ============================================================================
// Quantization Parameter Table Generation
// ============================================================================

/**
 * Generate quant_rem6 table: QP % 6 for all supported QP values.
 *
 * H.264 quantization uses a periodic structure based on modulo-6 arithmetic.
 * The quantization step size increases by factor of 2 every 6 QP values.
 * This table provides the remainder (0-5) for fast lookup during dequantization.
 *
 * Usage: Used to index into 6-entry base quantization tables.
 *
 * Pattern: 0,1,2,3,4,5, 0,1,2,3,4,5, 0,1,2,3,4,5, ... (repeating)
 */
constexpr auto generate_h264_quant_rem6() noexcept {
    std::array<uint8_t, QP_TABLE_SIZE> table{};

    for (int qp = 0; qp <= QP_MAX_NUM; ++qp) {
        table[qp] = static_cast<uint8_t>(qp % 6);
    }

    return table;
}

/**
 * Generate quant_div6 table: QP / 6 for all supported QP values.
 *
 * The division by 6 determines which octave/doubling of the quantization
 * step size we're in. Each increment of div6 represents a doubling of the
 * quantization step (and thus roughly halving of bitrate).
 *
 * Usage: Used as a shift amount in dequantization: value << (div6 + offset).
 *
 * Pattern: 0,0,0,0,0,0, 1,1,1,1,1,1, 2,2,2,2,2,2, ... (each value repeated 6 times)
 */
constexpr auto generate_h264_quant_div6() noexcept {
    std::array<uint8_t, QP_TABLE_SIZE> table{};

    for (int qp = 0; qp <= QP_MAX_NUM; ++qp) {
        table[qp] = static_cast<uint8_t>(qp / 6);
    }

    return table;
}

// ============================================================================
// Generated Tables (176 bytes total: 88 × 2)
// ============================================================================

constexpr auto h264_quant_rem6 = generate_h264_quant_rem6();
constexpr auto h264_quant_div6 = generate_h264_quant_div6();

// ============================================================================
// Static Assertions: Comprehensive Validation
// ============================================================================

// Table size validation
static_assert(QP_MAX_NUM == 87, "Max QP is 87 (51 + 6*6)");
static_assert(QP_TABLE_SIZE == 88, "QP tables have 88 entries (0-87)");
static_assert(h264_quant_rem6.size() == 88, "quant_rem6 has 88 entries");
static_assert(h264_quant_div6.size() == 88, "quant_div6 has 88 entries");

// Test rem6 pattern at start (0-5 repeating)
static_assert(h264_quant_rem6[0] == 0, "QP 0 % 6 = 0");
static_assert(h264_quant_rem6[1] == 1, "QP 1 % 6 = 1");
static_assert(h264_quant_rem6[2] == 2, "QP 2 % 6 = 2");
static_assert(h264_quant_rem6[3] == 3, "QP 3 % 6 = 3");
static_assert(h264_quant_rem6[4] == 4, "QP 4 % 6 = 4");
static_assert(h264_quant_rem6[5] == 5, "QP 5 % 6 = 5");

// Test rem6 pattern repeats
static_assert(h264_quant_rem6[6] == 0, "QP 6 % 6 = 0 (first repeat)");
static_assert(h264_quant_rem6[7] == 1, "QP 7 % 6 = 1");
static_assert(h264_quant_rem6[11] == 5, "QP 11 % 6 = 5");
static_assert(h264_quant_rem6[12] == 0, "QP 12 % 6 = 0 (second repeat)");
static_assert(h264_quant_rem6[18] == 0, "QP 18 % 6 = 0 (third repeat)");

// Test rem6 at key QP values
static_assert(h264_quant_rem6[24] == 0, "QP 24 % 6 = 0");
static_assert(h264_quant_rem6[30] == 0, "QP 30 % 6 = 0");
static_assert(h264_quant_rem6[36] == 0, "QP 36 % 6 = 0");
static_assert(h264_quant_rem6[42] == 0, "QP 42 % 6 = 0");
static_assert(h264_quant_rem6[48] == 0, "QP 48 % 6 = 0");
static_assert(h264_quant_rem6[51] == 3, "QP 51 % 6 = 3 (base max)");

// Test rem6 at extended QP values
static_assert(h264_quant_rem6[54] == 0, "QP 54 % 6 = 0");
static_assert(h264_quant_rem6[60] == 0, "QP 60 % 6 = 0");
static_assert(h264_quant_rem6[72] == 0, "QP 72 % 6 = 0");
static_assert(h264_quant_rem6[84] == 0, "QP 84 % 6 = 0");
static_assert(h264_quant_rem6[87] == 3, "QP 87 % 6 = 3 (absolute max)");

// Test div6 pattern at start (each value repeated 6 times)
static_assert(h264_quant_div6[0] == 0, "QP 0 / 6 = 0");
static_assert(h264_quant_div6[1] == 0, "QP 1 / 6 = 0");
static_assert(h264_quant_div6[2] == 0, "QP 2 / 6 = 0");
static_assert(h264_quant_div6[3] == 0, "QP 3 / 6 = 0");
static_assert(h264_quant_div6[4] == 0, "QP 4 / 6 = 0");
static_assert(h264_quant_div6[5] == 0, "QP 5 / 6 = 0");

// Test div6 transitions
static_assert(h264_quant_div6[6] == 1, "QP 6 / 6 = 1 (first transition)");
static_assert(h264_quant_div6[7] == 1, "QP 7 / 6 = 1");
static_assert(h264_quant_div6[11] == 1, "QP 11 / 6 = 1");
static_assert(h264_quant_div6[12] == 2, "QP 12 / 6 = 2 (second transition)");
static_assert(h264_quant_div6[17] == 2, "QP 17 / 6 = 2");
static_assert(h264_quant_div6[18] == 3, "QP 18 / 6 = 3 (third transition)");

// Test div6 at key QP values
static_assert(h264_quant_div6[24] == 4, "QP 24 / 6 = 4");
static_assert(h264_quant_div6[30] == 5, "QP 30 / 6 = 5");
static_assert(h264_quant_div6[36] == 6, "QP 36 / 6 = 6");
static_assert(h264_quant_div6[42] == 7, "QP 42 / 6 = 7");
static_assert(h264_quant_div6[48] == 8, "QP 48 / 6 = 8");
static_assert(h264_quant_div6[51] == 8, "QP 51 / 6 = 8 (base max)");

// Test div6 at extended QP values
static_assert(h264_quant_div6[54] == 9, "QP 54 / 6 = 9");
static_assert(h264_quant_div6[60] == 10, "QP 60 / 6 = 10");
static_assert(h264_quant_div6[66] == 11, "QP 66 / 6 = 11");
static_assert(h264_quant_div6[72] == 12, "QP 72 / 6 = 12");
static_assert(h264_quant_div6[78] == 13, "QP 78 / 6 = 13");
static_assert(h264_quant_div6[84] == 14, "QP 84 / 6 = 14");
static_assert(h264_quant_div6[87] == 14, "QP 87 / 6 = 14 (absolute max)");

// Verify all rem6 values are in range [0, 5]
static_assert(h264_quant_rem6[0] <= 5, "rem6[0] in range");
static_assert(h264_quant_rem6[25] <= 5, "rem6[25] in range");
static_assert(h264_quant_rem6[50] <= 5, "rem6[50] in range");
static_assert(h264_quant_rem6[75] <= 5, "rem6[75] in range");
static_assert(h264_quant_rem6[87] <= 5, "rem6[87] in range");

// Verify all div6 values are in range [0, 14]
static_assert(h264_quant_div6[0] <= 14, "div6[0] in range");
static_assert(h264_quant_div6[25] <= 14, "div6[25] in range");
static_assert(h264_quant_div6[50] <= 14, "div6[50] in range");
static_assert(h264_quant_div6[75] <= 14, "div6[75] in range");
static_assert(h264_quant_div6[87] <= 14, "div6[87] in range");

// Verify the mathematical relationship: qp = div6 * 6 + rem6
static_assert(h264_quant_div6[0] * 6 + h264_quant_rem6[0] == 0, "QP 0 reconstruction");
static_assert(h264_quant_div6[1] * 6 + h264_quant_rem6[1] == 1, "QP 1 reconstruction");
static_assert(h264_quant_div6[25] * 6 + h264_quant_rem6[25] == 25, "QP 25 reconstruction");
static_assert(h264_quant_div6[51] * 6 + h264_quant_rem6[51] == 51, "QP 51 reconstruction");
static_assert(h264_quant_div6[87] * 6 + h264_quant_rem6[87] == 87, "QP 87 reconstruction");

// Test monotonicity of div6 (non-decreasing)
static_assert(h264_quant_div6[0] <= h264_quant_div6[1], "div6 non-decreasing");
static_assert(h264_quant_div6[5] <= h264_quant_div6[6], "div6 non-decreasing at transition");
static_assert(h264_quant_div6[11] <= h264_quant_div6[12], "div6 non-decreasing at transition");
static_assert(h264_quant_div6[50] <= h264_quant_div6[51], "div6 non-decreasing");
static_assert(h264_quant_div6[86] <= h264_quant_div6[87], "div6 non-decreasing at end");

// Test specific intermediate values
static_assert(h264_quant_rem6[13] == 1, "QP 13 % 6 = 1");
static_assert(h264_quant_rem6[27] == 3, "QP 27 % 6 = 3");
static_assert(h264_quant_rem6[44] == 2, "QP 44 % 6 = 2");
static_assert(h264_quant_rem6[65] == 5, "QP 65 % 6 = 5");

static_assert(h264_quant_div6[13] == 2, "QP 13 / 6 = 2");
static_assert(h264_quant_div6[27] == 4, "QP 27 / 6 = 4");
static_assert(h264_quant_div6[44] == 7, "QP 44 / 6 = 7");
static_assert(h264_quant_div6[65] == 10, "QP 65 / 6 = 10");

} // namespace FFmpegH264QP

// ============================================================================
// C API Compatibility
// ============================================================================

extern "C" {

/**
 * Get pointer to the h264_quant_rem6 table for use in C code.
 * Returns: Pointer to 88-element uint8_t array
 */
inline const uint8_t *get_h264_quant_rem6() {
    return FFmpegH264QP::h264_quant_rem6.data();
}

/**
 * Get pointer to the h264_quant_div6 table for use in C code.
 * Returns: Pointer to 88-element uint8_t array
 */
inline const uint8_t *get_h264_quant_div6() {
    return FFmpegH264QP::h264_quant_div6.data();
}

} // extern "C"

#endif // AVCODEC_H264_QP_TABLEGEN_CONSTEXPR_HPP
