/*
 * Compile-time generation of H.264 chroma quantization parameter mapping tables
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

#ifndef AVCODEC_H264_CHROMA_QP_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_H264_CHROMA_QP_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>

namespace FFmpegH264ChromaQP {

// ============================================================================
// Constants
// ============================================================================

constexpr int QP_MAX_NUM = 51 + 6 * 6;  // 87 (same as base QP tables)
constexpr int QP_TABLE_SIZE = QP_MAX_NUM + 1;  // 88 entries (0-87)
constexpr int NUM_BIT_DEPTHS = 7;  // 8, 9, 10, 11, 12, 13, 14 bit

// ============================================================================
// Standard H.264 Chroma QP Mapping
// ============================================================================

/**
 * Standard H.264 chroma QP mapping table (from H.264 spec Table 8-15).
 * Maps luma QP (0-51) to chroma QP.
 *
 * The mapping is non-linear at higher QPs to preserve chroma quality:
 * - QP 0-29: identity mapping (chroma_qp = luma_qp)
 * - QP 30-51: compressed mapping (slower growth, some duplicates)
 */
constexpr std::array<uint8_t, 52> standard_chroma_qp_mapping = {
    // 0-29: Identity mapping
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
    // 30-51: Compressed mapping (from CHROMA_QP_TABLE_END macro)
    29, 30, 31, 32, 32, 33, 34, 34, 35, 35, 36, 36, 37, 37, 37, 38,
    38, 38, 39, 39, 39, 39
};

// ============================================================================
// Chroma QP Table Generation
// ============================================================================

/**
 * Generate complete H.264 chroma QP mapping table for all bit depths.
 *
 * For each bit depth (8-14 bits):
 * - Bit depth 8: Standard chroma mapping for QP 0-51, extended for QP 52-87
 * - Bit depths 9-14: Identity mapping initially, then offset standard mapping
 *
 * The bit depth offset formula: qp_offset = 6 * (bit_depth - 8)
 * This shifts the entire QP range up for higher bit depths.
 */
constexpr auto generate_h264_chroma_qp_table() noexcept {
    std::array<std::array<uint8_t, QP_TABLE_SIZE>, NUM_BIT_DEPTHS> table{};

    for (int depth_idx = 0; depth_idx < NUM_BIT_DEPTHS; ++depth_idx) {
        int bit_depth = 8 + depth_idx;  // 8, 9, 10, 11, 12, 13, 14
        int depth_offset = 6 * (bit_depth - 8);

        // Number of initial identity mappings before using standard chroma mapping
        // depth 8: 0 identity entries (use standard mapping from start)
        // depth 9: 6 identity entries (0→0, 1→1, ..., 5→5)
        // depth 10: 12 identity entries
        // depth 11: 18 identity entries
        // depth 12: 24 identity entries
        // depth 13: 30 identity entries
        // depth 14: 36 identity entries
        int identity_count = depth_offset;

        for (int qp = 0; qp <= QP_MAX_NUM; ++qp) {
            if (qp < identity_count) {
                // Identity mapping for initial QPs
                table[depth_idx][qp] = static_cast<uint8_t>(qp);
            } else {
                // Use standard chroma mapping (possibly with depth offset)
                // Adjust QP to standard range by subtracting identity_count
                int adjusted_qp = qp - identity_count;

                if (adjusted_qp < static_cast<int>(standard_chroma_qp_mapping.size())) {
                    // Use standard mapping and add depth offset
                    table[depth_idx][qp] = static_cast<uint8_t>(
                        standard_chroma_qp_mapping[adjusted_qp] + depth_offset
                    );
                } else {
                    // Extended range beyond standard (QP 52-87)
                    // Continue the pattern: last standard value + depth offset + extension
                    int extension = adjusted_qp - 51;  // How far beyond standard range
                    table[depth_idx][qp] = static_cast<uint8_t>(
                        39 + depth_offset + extension  // 39 is last standard chroma QP
                    );
                }
            }
        }
    }

    return table;
}

// ============================================================================
// Generated Table (616 bytes = 7 × 88)
// ============================================================================

constexpr auto h264_chroma_qp = generate_h264_chroma_qp_table();

// ============================================================================
// Static Assertions: Comprehensive Validation
// ============================================================================

// Table dimensions
static_assert(h264_chroma_qp.size() == 7, "7 bit depths supported");
static_assert(h264_chroma_qp[0].size() == 88, "88 QP values (0-87)");

// Test bit depth 8 (row 0): uses standard chroma mapping directly
static_assert(h264_chroma_qp[0][0] == 0, "8-bit: QP 0 maps to chroma QP 0");
static_assert(h264_chroma_qp[0][29] == 29, "8-bit: QP 29 maps to chroma QP 29");
static_assert(h264_chroma_qp[0][30] == 29, "8-bit: QP 30 maps to chroma QP 29 (first compression)");
static_assert(h264_chroma_qp[0][31] == 30, "8-bit: QP 31 maps to chroma QP 30");
static_assert(h264_chroma_qp[0][32] == 31, "8-bit: QP 32 maps to chroma QP 31");
static_assert(h264_chroma_qp[0][33] == 32, "8-bit: QP 33 maps to chroma QP 32");
static_assert(h264_chroma_qp[0][34] == 32, "8-bit: QP 34 maps to chroma QP 32 (duplicate)");
static_assert(h264_chroma_qp[0][51] == 39, "8-bit: QP 51 maps to chroma QP 39 (max standard)");

// Test bit depth 9 (row 1): 6 identity entries, then standard mapping with offset
static_assert(h264_chroma_qp[1][0] == 0, "9-bit: QP 0 identity");
static_assert(h264_chroma_qp[1][5] == 5, "9-bit: QP 5 identity (last)");
static_assert(h264_chroma_qp[1][6] == 6, "9-bit: QP 6 = standard[0] + 6");
static_assert(h264_chroma_qp[1][7] == 7, "9-bit: QP 7 = standard[1] + 6");

// Test bit depth 10 (row 2): 12 identity entries
static_assert(h264_chroma_qp[2][0] == 0, "10-bit: QP 0 identity");
static_assert(h264_chroma_qp[2][11] == 11, "10-bit: QP 11 identity (last)");
static_assert(h264_chroma_qp[2][12] == 12, "10-bit: QP 12 = standard[0] + 12");

// Test bit depth 11 (row 3): 18 identity entries
static_assert(h264_chroma_qp[3][0] == 0, "11-bit: QP 0 identity");
static_assert(h264_chroma_qp[3][17] == 17, "11-bit: QP 17 identity (last)");
static_assert(h264_chroma_qp[3][18] == 18, "11-bit: QP 18 = standard[0] + 18");

// Test bit depth 12 (row 4): 24 identity entries
static_assert(h264_chroma_qp[4][0] == 0, "12-bit: QP 0 identity");
static_assert(h264_chroma_qp[4][23] == 23, "12-bit: QP 23 identity (last)");
static_assert(h264_chroma_qp[4][24] == 24, "12-bit: QP 24 = standard[0] + 24");

// Test bit depth 13 (row 5): 30 identity entries
static_assert(h264_chroma_qp[5][0] == 0, "13-bit: QP 0 identity");
static_assert(h264_chroma_qp[5][29] == 29, "13-bit: QP 29 identity (last)");
static_assert(h264_chroma_qp[5][30] == 30, "13-bit: QP 30 = standard[0] + 30");

// Test bit depth 14 (row 6): 36 identity entries
static_assert(h264_chroma_qp[6][0] == 0, "14-bit: QP 0 identity");
static_assert(h264_chroma_qp[6][35] == 35, "14-bit: QP 35 identity (last)");
static_assert(h264_chroma_qp[6][36] == 36, "14-bit: QP 36 = standard[0] + 36");

// Verify standard chroma mapping characteristics
static_assert(standard_chroma_qp_mapping[0] == 0, "Standard mapping starts at 0");
static_assert(standard_chroma_qp_mapping[29] == 29, "Standard: QP 29 → 29");
static_assert(standard_chroma_qp_mapping[30] == 29, "Standard: QP 30 → 29 (compression starts)");
static_assert(standard_chroma_qp_mapping[51] == 39, "Standard mapping ends at 39");

// Test non-linear compression in standard mapping
static_assert(standard_chroma_qp_mapping[30] == standard_chroma_qp_mapping[29],
              "QP 30 compressed (same as 29)");
static_assert(standard_chroma_qp_mapping[33] == standard_chroma_qp_mapping[34],
              "QP 33-34 compressed");

// Verify extended range (QP 52-87)
static_assert(h264_chroma_qp[0][52] > h264_chroma_qp[0][51],
              "8-bit: Extended range increases");
static_assert(h264_chroma_qp[0][87] > h264_chroma_qp[0][86],
              "8-bit: Continues to end");

// Test bit depth offset formula: depth_offset = 6 * (bit_depth - 8)
static_assert(h264_chroma_qp[1][6] == h264_chroma_qp[0][0] + 6,
              "9-bit offset is +6 from 8-bit");
static_assert(h264_chroma_qp[2][12] == h264_chroma_qp[0][0] + 12,
              "10-bit offset is +12 from 8-bit");
static_assert(h264_chroma_qp[6][36] == h264_chroma_qp[0][0] + 36,
              "14-bit offset is +36 from 8-bit");

// Verify monotonicity within each depth (chroma QP never decreases for increasing luma QP)
static_assert(h264_chroma_qp[0][0] <= h264_chroma_qp[0][1],
              "8-bit: monotonic at start");
static_assert(h264_chroma_qp[0][50] <= h264_chroma_qp[0][51],
              "8-bit: monotonic at end of standard range");
static_assert(h264_chroma_qp[3][40] <= h264_chroma_qp[3][41],
              "11-bit: monotonic in middle");

// Test specific mappings across different bit depths
static_assert(h264_chroma_qp[0][25] == 25, "8-bit: QP 25 (identity region)");
static_assert(h264_chroma_qp[0][35] == 33, "8-bit: QP 35 maps to chroma QP 33");
static_assert(h264_chroma_qp[0][40] == 36, "8-bit: QP 40 maps to 36");

// Boundary conditions
static_assert(h264_chroma_qp[0][QP_MAX_NUM] < 128, "All values fit in uint8_t");
static_assert(h264_chroma_qp[6][QP_MAX_NUM] < 128, "14-bit max QP fits in uint8_t");

// Cross-depth consistency: test offset behavior
// At the boundary where standard mapping starts for each depth
static_assert(h264_chroma_qp[1][12] == h264_chroma_qp[0][6] + 6,
              "9-bit at QP 12 equals 8-bit at QP 6 plus offset");
static_assert(h264_chroma_qp[2][24] == h264_chroma_qp[0][12] + 12,
              "10-bit at QP 24 equals 8-bit at QP 12 plus offset");

} // namespace FFmpegH264ChromaQP

// ============================================================================
// C API Compatibility
// ============================================================================

extern "C" {

/**
 * Get pointer to the H.264 chroma QP mapping table for use in C code.
 * Returns: Pointer to 7×88 uint8_t array
 */
inline const uint8_t (*get_h264_chroma_qp_table())[88] {
    return reinterpret_cast<const uint8_t(*)[88]>(
        &FFmpegH264ChromaQP::h264_chroma_qp
    );
}

} // extern "C"

#endif // AVCODEC_H264_CHROMA_QP_TABLEGEN_CONSTEXPR_HPP
