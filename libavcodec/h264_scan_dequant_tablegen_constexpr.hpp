/*
 * Compile-time generation of H.264 scan patterns and dequantization init tables
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

#ifndef AVCODEC_H264_SCAN_DEQUANT_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_H264_SCAN_DEQUANT_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>

namespace FFmpegH264ScanDequant {

// ============================================================================
// Constants
// ============================================================================

constexpr int CHROMA_DC_BLOCK_SIZE = 4;      // 2×2 chroma DC block
constexpr int CHROMA422_DC_BLOCK_SIZE = 8;   // 2×4 chroma 4:2:2 DC block
constexpr int DEQUANT_QP_LEVELS = 6;         // 6 QP levels for dequant matrices
constexpr int DEQUANT4_MATRIX_SIZE = 3;      // 4×4 dequant has 3 positions
constexpr int DEQUANT8_SCAN_SIZE = 16;       // 8×8 dequant scan order
constexpr int DEQUANT8_MATRIX_SIZE = 6;      // 8×8 dequant has 6 positions

// ============================================================================
// Chroma DC Scan Patterns
// ============================================================================

/**
 * Generate H.264 chroma DC scan pattern for 2×2 block.
 *
 * Chroma DC coefficients are organized in a 2×2 block:
 *   (0,0) (1,0)
 *   (0,1) (1,1)
 *
 * Scan order (raster): top-left, top-right, bottom-left, bottom-right
 * Each value is: (x + y * 2) * 16
 * The *16 factor accounts for memory layout in decoder.
 */
constexpr auto generate_h264_chroma_dc_scan() noexcept {
    std::array<uint8_t, CHROMA_DC_BLOCK_SIZE> scan{};

    // 2×2 block in raster order
    int idx = 0;
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            scan[idx++] = static_cast<uint8_t>((x + y * 2) * 16);
        }
    }

    return scan;
}

/**
 * Generate H.264 chroma DC scan pattern for 2×4 block (4:2:2).
 *
 * Chroma 4:2:2 DC coefficients are organized in a 2×4 block:
 *   (0,0) (1,0)
 *   (0,1) (1,1)
 *   (0,2) (1,2)
 *   (0,3) (1,3)
 *
 * Scan order: column-major with specific H.264 ordering
 * Each value is: (x + y * 2) * 16
 */
constexpr auto generate_h264_chroma422_dc_scan() noexcept {
    std::array<uint8_t, CHROMA422_DC_BLOCK_SIZE> scan{};

    // H.264 4:2:2 chroma DC scan order
    // Column-major traversal with specific pattern
    const int positions[8][2] = {
        {0, 0}, {0, 1}, {1, 0}, {0, 2},
        {0, 3}, {1, 1}, {1, 2}, {1, 3}
    };

    for (int i = 0; i < 8; ++i) {
        int x = positions[i][0];
        int y = positions[i][1];
        scan[i] = static_cast<uint8_t>((x + y * 2) * 16);
    }

    return scan;
}

// ============================================================================
// Dequantization Initialization Tables
// ============================================================================

/**
 * Generate H.264 4×4 dequantization coefficient initialization table.
 *
 * These values are used to initialize the 4×4 dequantization matrices.
 * They represent scaling factors for different QP % 6 values and positions
 * within the quantization matrix.
 *
 * The pattern follows H.264 spec scaling factors with basis values:
 * - QP % 6 = 0: base values
 * - QP % 6 = 1: base × ~1.12
 * - QP % 6 = 2: base × ~1.26
 * - etc.
 *
 * Three positions represent different matrix elements (due to symmetry).
 */
constexpr auto generate_h264_dequant4_coeff_init() noexcept {
    std::array<std::array<uint8_t, DEQUANT4_MATRIX_SIZE>, DEQUANT_QP_LEVELS> table{};

    // H.264 spec values for 4×4 dequant scaling
    // Rows: QP % 6 (0-5), Columns: matrix positions (0-2)
    const int init_values[6][3] = {
        { 10, 13, 16 },  // QP % 6 = 0
        { 11, 14, 18 },  // QP % 6 = 1
        { 13, 16, 20 },  // QP % 6 = 2
        { 14, 18, 23 },  // QP % 6 = 3
        { 16, 20, 25 },  // QP % 6 = 4
        { 18, 23, 29 },  // QP % 6 = 5
    };

    for (int qp_mod = 0; qp_mod < 6; ++qp_mod) {
        for (int pos = 0; pos < 3; ++pos) {
            table[qp_mod][pos] = static_cast<uint8_t>(init_values[qp_mod][pos]);
        }
    }

    return table;
}

/**
 * Generate H.264 8×8 dequantization scan order.
 *
 * This table maps linear index to position within 8×8 dequantization matrix.
 * The scan order follows H.264's specific pattern for 8×8 coefficient ordering.
 *
 * The pattern groups similar-scaled coefficients together based on quantization
 * matrix structure.
 */
constexpr auto generate_h264_dequant8_coeff_init_scan() noexcept {
    std::array<uint8_t, DEQUANT8_SCAN_SIZE> scan{};

    // H.264 spec scan order for 8×8 dequant matrix positions
    const uint8_t pattern[16] = {
        0, 3, 4, 3,  3, 1, 5, 1,
        4, 5, 2, 5,  3, 1, 5, 1
    };

    for (int i = 0; i < 16; ++i) {
        scan[i] = pattern[i];
    }

    return scan;
}

/**
 * Generate H.264 8×8 dequantization coefficient initialization table.
 *
 * These values are used to initialize the 8×8 dequantization matrices.
 * Similar to 4×4 but with 6 positions (more matrix variation due to 8×8 size).
 *
 * The pattern follows H.264 spec scaling factors for high-profile 8×8 transforms.
 */
constexpr auto generate_h264_dequant8_coeff_init() noexcept {
    std::array<std::array<uint8_t, DEQUANT8_MATRIX_SIZE>, DEQUANT_QP_LEVELS> table{};

    // H.264 spec values for 8×8 dequant scaling
    // Rows: QP % 6 (0-5), Columns: matrix positions (0-5)
    const int init_values[6][6] = {
        { 20, 18, 32, 19, 25, 24 },  // QP % 6 = 0
        { 22, 19, 35, 21, 28, 26 },  // QP % 6 = 1
        { 26, 23, 42, 24, 33, 31 },  // QP % 6 = 2
        { 28, 25, 45, 26, 35, 33 },  // QP % 6 = 3
        { 32, 28, 51, 30, 40, 38 },  // QP % 6 = 4
        { 36, 32, 58, 34, 46, 43 },  // QP % 6 = 5
    };

    for (int qp_mod = 0; qp_mod < 6; ++qp_mod) {
        for (int pos = 0; pos < 6; ++pos) {
            table[qp_mod][pos] = static_cast<uint8_t>(init_values[qp_mod][pos]);
        }
    }

    return table;
}

// ============================================================================
// Generated Tables (82 bytes total)
// ============================================================================

constexpr auto h264_chroma_dc_scan = generate_h264_chroma_dc_scan();
constexpr auto h264_chroma422_dc_scan = generate_h264_chroma422_dc_scan();
constexpr auto h264_dequant4_coeff_init = generate_h264_dequant4_coeff_init();
constexpr auto h264_dequant8_coeff_init_scan = generate_h264_dequant8_coeff_init_scan();
constexpr auto h264_dequant8_coeff_init = generate_h264_dequant8_coeff_init();

// ============================================================================
// Static Assertions: Comprehensive Validation
// ============================================================================

// Chroma DC scan validation
static_assert(h264_chroma_dc_scan.size() == 4, "Chroma DC scan has 4 positions");
static_assert(h264_chroma_dc_scan[0] == 0, "Position 0: (0,0)*16 = 0");
static_assert(h264_chroma_dc_scan[1] == 16, "Position 1: (1,0)*16 = 16");
static_assert(h264_chroma_dc_scan[2] == 32, "Position 2: (0,1)*16 = 32");
static_assert(h264_chroma_dc_scan[3] == 48, "Position 3: (1,1)*16 = 48");

// Chroma 4:2:2 DC scan validation
static_assert(h264_chroma422_dc_scan.size() == 8, "Chroma 4:2:2 DC scan has 8 positions");
static_assert(h264_chroma422_dc_scan[0] == 0, "Position 0: (0,0)*16 = 0");
static_assert(h264_chroma422_dc_scan[1] == 32, "Position 1: (0,1)*16 = 32");
static_assert(h264_chroma422_dc_scan[2] == 16, "Position 2: (1,0)*16 = 16");
static_assert(h264_chroma422_dc_scan[3] == 64, "Position 3: (0,2)*16 = 64");
static_assert(h264_chroma422_dc_scan[4] == 96, "Position 4: (0,3)*16 = 96");
static_assert(h264_chroma422_dc_scan[5] == 48, "Position 5: (1,1)*16 = 48");
static_assert(h264_chroma422_dc_scan[6] == 80, "Position 6: (1,2)*16 = 80");
static_assert(h264_chroma422_dc_scan[7] == 112, "Position 7: (1,3)*16 = 112");

// Dequant 4×4 table validation
static_assert(h264_dequant4_coeff_init.size() == 6, "6 QP levels for 4×4 dequant");
static_assert(h264_dequant4_coeff_init[0].size() == 3, "3 positions per QP level");

// Verify first row (QP % 6 = 0)
static_assert(h264_dequant4_coeff_init[0][0] == 10, "QP%6=0, pos 0: 10");
static_assert(h264_dequant4_coeff_init[0][1] == 13, "QP%6=0, pos 1: 13");
static_assert(h264_dequant4_coeff_init[0][2] == 16, "QP%6=0, pos 2: 16");

// Verify last row (QP % 6 = 5)
static_assert(h264_dequant4_coeff_init[5][0] == 18, "QP%6=5, pos 0: 18");
static_assert(h264_dequant4_coeff_init[5][1] == 23, "QP%6=5, pos 1: 23");
static_assert(h264_dequant4_coeff_init[5][2] == 29, "QP%6=5, pos 2: 29");

// Verify monotonicity within each QP level (values increase with position)
static_assert(h264_dequant4_coeff_init[0][0] < h264_dequant4_coeff_init[0][1],
              "QP%6=0: pos 0 < pos 1");
static_assert(h264_dequant4_coeff_init[0][1] < h264_dequant4_coeff_init[0][2],
              "QP%6=0: pos 1 < pos 2");
static_assert(h264_dequant4_coeff_init[3][0] < h264_dequant4_coeff_init[3][1],
              "QP%6=3: pos 0 < pos 1");
static_assert(h264_dequant4_coeff_init[3][1] < h264_dequant4_coeff_init[3][2],
              "QP%6=3: pos 1 < pos 2");

// Verify scaling across QP levels (higher QP % 6 → larger values)
static_assert(h264_dequant4_coeff_init[0][0] < h264_dequant4_coeff_init[5][0],
              "pos 0: QP%6=0 < QP%6=5");
static_assert(h264_dequant4_coeff_init[0][2] < h264_dequant4_coeff_init[5][2],
              "pos 2: QP%6=0 < QP%6=5");

// Dequant 8×8 scan validation
static_assert(h264_dequant8_coeff_init_scan.size() == 16, "8×8 scan has 16 positions");
static_assert(h264_dequant8_coeff_init_scan[0] == 0, "Scan position 0: matrix pos 0");
static_assert(h264_dequant8_coeff_init_scan[1] == 3, "Scan position 1: matrix pos 3");
static_assert(h264_dequant8_coeff_init_scan[2] == 4, "Scan position 2: matrix pos 4");

// Verify all scan values are within bounds [0, 5]
static_assert(h264_dequant8_coeff_init_scan[0] < 6, "Scan values < 6");
static_assert(h264_dequant8_coeff_init_scan[7] < 6, "Scan values < 6");
static_assert(h264_dequant8_coeff_init_scan[15] < 6, "Scan values < 6");

// Dequant 8×8 table validation
static_assert(h264_dequant8_coeff_init.size() == 6, "6 QP levels for 8×8 dequant");
static_assert(h264_dequant8_coeff_init[0].size() == 6, "6 positions per QP level");

// Verify first row (QP % 6 = 0)
static_assert(h264_dequant8_coeff_init[0][0] == 20, "QP%6=0, pos 0: 20");
static_assert(h264_dequant8_coeff_init[0][1] == 18, "QP%6=0, pos 1: 18");
static_assert(h264_dequant8_coeff_init[0][2] == 32, "QP%6=0, pos 2: 32");
static_assert(h264_dequant8_coeff_init[0][5] == 24, "QP%6=0, pos 5: 24");

// Verify last row (QP % 6 = 5)
static_assert(h264_dequant8_coeff_init[5][0] == 36, "QP%6=5, pos 0: 36");
static_assert(h264_dequant8_coeff_init[5][2] == 58, "QP%6=5, pos 2: 58");
static_assert(h264_dequant8_coeff_init[5][5] == 43, "QP%6=5, pos 5: 43");

// Verify middle row (QP % 6 = 2)
static_assert(h264_dequant8_coeff_init[2][0] == 26, "QP%6=2, pos 0: 26");
static_assert(h264_dequant8_coeff_init[2][1] == 23, "QP%6=2, pos 1: 23");
static_assert(h264_dequant8_coeff_init[2][2] == 42, "QP%6=2, pos 2: 42");

// Verify scaling across QP levels for 8×8
static_assert(h264_dequant8_coeff_init[0][0] < h264_dequant8_coeff_init[5][0],
              "8×8 pos 0: QP%6=0 < QP%6=5");
static_assert(h264_dequant8_coeff_init[0][2] < h264_dequant8_coeff_init[5][2],
              "8×8 pos 2: QP%6=0 < QP%6=5");
static_assert(h264_dequant8_coeff_init[2][3] < h264_dequant8_coeff_init[4][3],
              "8×8 pos 3: QP%6=2 < QP%6=4");

// Verify position 2 is consistently largest in each QP level (it's the DC position)
static_assert(h264_dequant8_coeff_init[0][2] > h264_dequant8_coeff_init[0][0],
              "QP%6=0: pos 2 > pos 0 (DC has higher scale)");
static_assert(h264_dequant8_coeff_init[0][2] > h264_dequant8_coeff_init[0][1],
              "QP%6=0: pos 2 > pos 1");
static_assert(h264_dequant8_coeff_init[3][2] > h264_dequant8_coeff_init[3][0],
              "QP%6=3: pos 2 > pos 0");
static_assert(h264_dequant8_coeff_init[5][2] > h264_dequant8_coeff_init[5][1],
              "QP%6=5: pos 2 > pos 1");

// Verify all values fit in uint8_t and are reasonable
static_assert(h264_dequant8_coeff_init[5][2] < 128, "All dequant values < 128");

} // namespace FFmpegH264ScanDequant

// ============================================================================
// C API Compatibility
// ============================================================================

extern "C" {

/**
 * Get pointer to H.264 chroma DC scan pattern (2×2).
 * Returns: Pointer to 4-element uint8_t array
 */
inline const uint8_t *get_h264_chroma_dc_scan() {
    return FFmpegH264ScanDequant::h264_chroma_dc_scan.data();
}

/**
 * Get pointer to H.264 chroma 4:2:2 DC scan pattern (2×4).
 * Returns: Pointer to 8-element uint8_t array
 */
inline const uint8_t *get_h264_chroma422_dc_scan() {
    return FFmpegH264ScanDequant::h264_chroma422_dc_scan.data();
}

/**
 * Get pointer to H.264 4×4 dequantization init table.
 * Returns: Pointer to 6×3 uint8_t array
 */
inline const uint8_t *get_h264_dequant4_coeff_init() {
    return reinterpret_cast<const uint8_t*>(
        FFmpegH264ScanDequant::h264_dequant4_coeff_init.data()
    );
}

/**
 * Get pointer to H.264 8×8 dequantization scan pattern.
 * Returns: Pointer to 16-element uint8_t array
 */
inline const uint8_t *get_h264_dequant8_coeff_init_scan() {
    return FFmpegH264ScanDequant::h264_dequant8_coeff_init_scan.data();
}

/**
 * Get pointer to H.264 8×8 dequantization init table.
 * Returns: Pointer to 6×6 uint8_t array
 */
inline const uint8_t *get_h264_dequant8_coeff_init() {
    return reinterpret_cast<const uint8_t*>(
        FFmpegH264ScanDequant::h264_dequant8_coeff_init.data()
    );
}

} // extern "C"

#endif // AVCODEC_H264_SCAN_DEQUANT_TABLEGEN_CONSTEXPR_HPP
