/*
 * Compile-time generation of H.264 decoder data tables
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

#ifndef AVCODEC_H264_DECODER_DATA_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_H264_DECODER_DATA_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>

namespace FFmpegH264DecoderData {

// ============================================================================
// Constants
// ============================================================================

// Picture type constants (from libavutil/avutil.h enum AVPictureType)
constexpr uint8_t AV_PICTURE_TYPE_NONE = 0;  // Undefined
constexpr uint8_t AV_PICTURE_TYPE_I    = 1;  // Intra
constexpr uint8_t AV_PICTURE_TYPE_P    = 2;  // Predicted
constexpr uint8_t AV_PICTURE_TYPE_B    = 3;  // Bi-dir predicted
constexpr uint8_t AV_PICTURE_TYPE_S    = 4;  // S(GMC)-VOP MPEG-4
constexpr uint8_t AV_PICTURE_TYPE_SI   = 5;  // Switching Intra
constexpr uint8_t AV_PICTURE_TYPE_SP   = 6;  // Switching Predicted
constexpr uint8_t AV_PICTURE_TYPE_BI   = 7;  // BI type

// Macroblock type flags (from h264.h)
constexpr uint16_t MB_TYPE_INTRA4x4  = 0x0001;
constexpr uint16_t MB_TYPE_INTRA16x16 = 0x0002;
constexpr uint16_t MB_TYPE_INTRA_PCM = 0x0040;
constexpr uint16_t MB_TYPE_16x16     = 0x0008;
constexpr uint16_t MB_TYPE_16x8      = 0x0010;
constexpr uint16_t MB_TYPE_8x16      = 0x0020;
constexpr uint16_t MB_TYPE_8x8       = 0x0080;
constexpr uint16_t MB_TYPE_P0L0      = 0x1000;
constexpr uint16_t MB_TYPE_P1L0      = 0x2000;
constexpr uint16_t MB_TYPE_P0L1      = 0x4000;
constexpr uint16_t MB_TYPE_P1L1      = 0x8000;
constexpr uint16_t MB_TYPE_L0L1      = MB_TYPE_P0L0 | MB_TYPE_P0L1;
constexpr uint16_t MB_TYPE_DIRECT2   = 0x0100;
constexpr uint16_t MB_TYPE_REF0      = 0x0200;

// ============================================================================
// Struct Definitions
// ============================================================================

// I-frame macroblock info
struct IMbInfo {
    uint16_t type;       // Macroblock type flags
    uint8_t pred_mode;   // Prediction mode (-1 for none)
    uint8_t cbp;         // Coded block pattern
};

// P/B-frame macroblock info
struct PMbInfo {
    uint16_t type;             // Macroblock type flags
    uint8_t partition_count;   // Number of partitions
};

// ============================================================================
// Picture Type Mapping (5 bytes)
// ============================================================================

/**
 * Map exp-Golomb code to H.264 picture type.
 *
 * In H.264 slice headers, the slice_type is encoded as an exp-Golomb code.
 * This table maps the decoded code to the picture type constant:
 * - 0,5: P (Predicted)
 * - 1,6: B (Bi-directional)
 * - 2,7: I (Intra)
 * - 3,8: SP (Switching Predicted)
 * - 4,9: SI (Switching Intra)
 *
 * Only codes 0-4 need mapping (5-9 are "_ONLY" variants with same type).
 */
constexpr auto generate_golomb_to_pict_type() noexcept {
    std::array<uint8_t, 5> table{};

    table[0] = AV_PICTURE_TYPE_P;   // P-frame
    table[1] = AV_PICTURE_TYPE_B;   // B-frame
    table[2] = AV_PICTURE_TYPE_I;   // I-frame
    table[3] = AV_PICTURE_TYPE_SP;  // SP-frame (rare)
    table[4] = AV_PICTURE_TYPE_SI;  // SI-frame (rare)

    return table;
}

constexpr auto h264_golomb_to_pict_type = generate_golomb_to_pict_type();

// ============================================================================
// Coded Block Pattern (CBP) Tables (96 bytes)
// ============================================================================

/**
 * Map exp-Golomb code to intra 4×4 coded block pattern.
 *
 * CBP indicates which 4×4 blocks contain non-zero coefficients after
 * quantization. The exp-Golomb coded value maps to a specific bit pattern
 * indicating which blocks are coded. The mapping is designed for compression
 * efficiency - more common patterns get lower codes.
 *
 * For intra 4×4 macroblocks, this table maps the 48 possible exp-Golomb
 * codes to the actual CBP values (0-47).
 */
constexpr auto generate_golomb_to_intra4x4_cbp() noexcept {
    std::array<uint8_t, 48> table{};

    // From H.264 spec: Table 9-4(a) - Intra 4×4 CBP mapping
    // Optimized for typical intra prediction residual patterns
    constexpr uint8_t values[48] = {
        47, 31, 15,  0, 23, 27, 29, 30,  7, 11, 13, 14, 39, 43, 45, 46,
        16,  3,  5, 10, 12, 19, 21, 26, 28, 35, 37, 42, 44,  1,  2,  4,
         8, 17, 18, 20, 24,  6,  9, 22, 25, 32, 33, 34, 36, 40, 38, 41
    };

    for (int i = 0; i < 48; ++i) {
        table[i] = values[i];
    }

    return table;
}

constexpr auto h264_golomb_to_intra4x4_cbp = generate_golomb_to_intra4x4_cbp();

/**
 * Map exp-Golomb code to inter coded block pattern.
 *
 * For inter (P/B) macroblocks, the CBP mapping is different to optimize
 * for the characteristics of motion-compensated residuals, which have
 * different statistical properties than intra residuals.
 */
constexpr auto generate_golomb_to_inter_cbp() noexcept {
    std::array<uint8_t, 48> table{};

    // From H.264 spec: Table 9-4(b) - Inter CBP mapping
    // Optimized for motion-compensated residual patterns
    constexpr uint8_t values[48] = {
         0, 16,  1,  2,  4,  8, 32,  3,  5, 10, 12, 15, 47,  7, 11, 13,
        14,  6,  9, 31, 35, 37, 42, 44, 33, 34, 36, 40, 39, 43, 45, 46,
        17, 18, 20, 24, 19, 21, 26, 28, 23, 27, 29, 30, 22, 25, 38, 41
    };

    for (int i = 0; i < 48; ++i) {
        table[i] = values[i];
    }

    return table;
}

constexpr auto h264_golomb_to_inter_cbp = generate_golomb_to_inter_cbp();

// ============================================================================
// Chroma Scan Patterns (12 bytes)
// ============================================================================

/**
 * Chroma DC coefficient scan order for 4:2:0 format.
 *
 * In H.264, chroma DC coefficients are organized in a 2×2 block.
 * This table defines the scan order (raster scan with 16-byte stride)
 * for accessing the DC coefficients in memory.
 */
constexpr auto generate_chroma_dc_scan() noexcept {
    std::array<uint8_t, 4> table{};

    // 2×2 raster scan with 16-byte stride
    table[0] = (0 + 0 * 2) * 16;  // Top-left
    table[1] = (1 + 0 * 2) * 16;  // Top-right
    table[2] = (0 + 1 * 2) * 16;  // Bottom-left
    table[3] = (1 + 1 * 2) * 16;  // Bottom-right

    return table;
}

constexpr auto h264_chroma_dc_scan = generate_chroma_dc_scan();

/**
 * Chroma DC coefficient scan order for 4:2:2 format.
 *
 * For 4:2:2 chroma format, DC coefficients form a 2×4 block.
 * Scan order is optimized for cache access patterns.
 */
constexpr auto generate_chroma422_dc_scan() noexcept {
    std::array<uint8_t, 8> table{};

    // 2×4 scan with 16-byte stride
    table[0] = (0 + 0 * 2) * 16;
    table[1] = (0 + 1 * 2) * 16;
    table[2] = (1 + 0 * 2) * 16;
    table[3] = (0 + 2 * 2) * 16;
    table[4] = (0 + 3 * 2) * 16;
    table[5] = (1 + 1 * 2) * 16;
    table[6] = (1 + 2 * 2) * 16;
    table[7] = (1 + 3 * 2) * 16;

    return table;
}

constexpr auto h264_chroma422_dc_scan = generate_chroma422_dc_scan();

// ============================================================================
// I-Frame Macroblock Type Info (26 entries × 4 bytes = 104 bytes)
// ============================================================================

/**
 * I-frame macroblock type information table.
 *
 * For I-slices, the mb_type exp-Golomb code indicates:
 * - 0: Intra_4×4 prediction (each 4×4 block predicted separately)
 * - 1-24: Intra_16×16 prediction (whole MB predicted with one mode)
 *   - Encodes prediction mode (0-3) and CBP pattern
 * - 25: I_PCM (raw pixel values, no prediction)
 *
 * Intra_16×16 encoding packs: (pred_mode + cbp_offset)
 * - pred_mode: 0=Vertical, 1=Horizontal, 2=DC, 3=Plane
 * - cbp_offset: 0, 16, 32 for luma pattern; +15 for chroma
 */
constexpr auto generate_i_mb_type_info() noexcept {
    std::array<IMbInfo, 26> table{};

    // Code 0: Intra 4×4 (each block predicted individually)
    table[0] = { MB_TYPE_INTRA4x4, 0xFF, 0xFF };  // 0xFF = -1 (no pred_mode/cbp)

    // Codes 1-24: Intra 16×16 (whole MB predicted with one mode)
    // Pattern: 4 pred_modes × 6 CBP patterns = 24 codes
    // CBP patterns: 0, 16, 32, 15+0, 15+16, 15+32

    int idx = 1;
    for (int cbp_base : {0, 16, 32, 15, 15+16, 15+32}) {
        for (int pred : {2, 1, 0, 3}) {  // Ordered by frequency: DC, H, V, Plane
            table[idx++] = { MB_TYPE_INTRA16x16, static_cast<uint8_t>(pred),
                            static_cast<uint8_t>(cbp_base == 15 ? 15 : cbp_base) };
        }
    }

    // Code 25: I_PCM (raw samples, no prediction)
    table[25] = { MB_TYPE_INTRA_PCM, 0xFF, 0xFF };

    return table;
}

constexpr auto h264_i_mb_type_info = generate_i_mb_type_info();

// ============================================================================
// P-Frame Macroblock Type Info (5 entries × 3 bytes = 15 bytes)
// ============================================================================

/**
 * P-frame macroblock type information table.
 *
 * For P-slices, mb_type exp-Golomb code 0-4 indicates partition layout:
 * - 0: 16×16 (one MV for whole MB)
 * - 1: 16×8  (two MVs, split horizontally)
 * - 2: 8×16  (two MVs, split vertically)
 * - 3: 8×8   (four 8×8 sub-macroblocks, each with own partition)
 * - 4: 8×8 with REF0 flag (optimized for reference index 0)
 *
 * partition_count indicates how many motion vectors are needed.
 */
constexpr auto generate_p_mb_type_info() noexcept {
    std::array<PMbInfo, 5> table{};

    table[0] = { MB_TYPE_16x16 | MB_TYPE_P0L0, 1 };                         // 16×16
    table[1] = { MB_TYPE_16x8  | MB_TYPE_P0L0 | MB_TYPE_P1L0, 2 };          // 16×8
    table[2] = { MB_TYPE_8x16  | MB_TYPE_P0L0 | MB_TYPE_P1L0, 2 };          // 8×16
    table[3] = { MB_TYPE_8x8   | MB_TYPE_P0L0 | MB_TYPE_P1L0, 4 };          // 8×8
    table[4] = { MB_TYPE_8x8   | MB_TYPE_P0L0 | MB_TYPE_P1L0 | MB_TYPE_REF0, 4 };  // 8×8 REF0

    return table;
}

constexpr auto h264_p_mb_type_info = generate_p_mb_type_info();

/**
 * P-frame sub-macroblock type information table.
 *
 * For 8×8 P macroblocks (codes 3-4 above), each 8×8 sub-block has its own
 * sub_mb_type indicating further partitioning:
 * - 0: 8×8 (one MV)
 * - 1: 8×4 (two MVs, split horizontally)
 * - 2: 4×8 (two MVs, split vertically)
 * - 3: 4×4 (four MVs, full partition)
 */
constexpr auto generate_p_sub_mb_type_info() noexcept {
    std::array<PMbInfo, 4> table{};

    table[0] = { MB_TYPE_16x16 | MB_TYPE_P0L0, 1 };  // 8×8 (use 16×16 flag for compatibility)
    table[1] = { MB_TYPE_16x8  | MB_TYPE_P0L0, 2 };  // 8×4 (use 16×8 flag)
    table[2] = { MB_TYPE_8x16  | MB_TYPE_P0L0, 2 };  // 4×8 (use 8×16 flag)
    table[3] = { MB_TYPE_8x8   | MB_TYPE_P0L0, 4 };  // 4×4

    return table;
}

constexpr auto h264_p_sub_mb_type_info = generate_p_sub_mb_type_info();

// ============================================================================
// B-Frame Macroblock Type Info (23 entries × 3 bytes = 69 bytes)
// ============================================================================

/**
 * B-frame macroblock type information table.
 *
 * B-slices support bi-directional prediction with two reference lists (L0, L1).
 * mb_type encodes partition layout and which list(s) are used:
 * - Code 0: Direct mode (MVs derived from co-located MB)
 * - Codes 1-3: 16×16 with L0, L1, or bi-prediction
 * - Codes 4-21: Various 16×8 and 8×16 partitions with different list combinations
 * - Code 22: 8×8 sub-macroblocks with full flexibility
 */
constexpr auto generate_b_mb_type_info() noexcept {
    std::array<PMbInfo, 23> table{};

    table[0]  = { MB_TYPE_DIRECT2 | MB_TYPE_L0L1, 1 };  // Direct

    // 16×16 modes (codes 1-3)
    table[1]  = { MB_TYPE_16x16 | MB_TYPE_P0L0, 1 };                    // L0
    table[2]  = { MB_TYPE_16x16 | MB_TYPE_P0L1, 1 };                    // L1
    table[3]  = { MB_TYPE_16x16 | MB_TYPE_P0L0 | MB_TYPE_P0L1, 1 };     // Bi

    // 16×8 modes (codes 4, 6, 8, 10, 12, 14, 16, 18, 20)
    table[4]  = { MB_TYPE_16x8 | MB_TYPE_P0L0 | MB_TYPE_P1L0, 2 };      // L0, L0
    table[6]  = { MB_TYPE_16x8 | MB_TYPE_P0L1 | MB_TYPE_P1L1, 2 };      // L1, L1
    table[8]  = { MB_TYPE_16x8 | MB_TYPE_P0L0 | MB_TYPE_P1L1, 2 };      // L0, L1
    table[10] = { MB_TYPE_16x8 | MB_TYPE_P0L1 | MB_TYPE_P1L0, 2 };      // L1, L0
    table[12] = { MB_TYPE_16x8 | MB_TYPE_P0L0 | MB_TYPE_P1L0 | MB_TYPE_P1L1, 2 };  // L0, Bi
    table[14] = { MB_TYPE_16x8 | MB_TYPE_P0L1 | MB_TYPE_P1L0 | MB_TYPE_P1L1, 2 };  // L1, Bi
    table[16] = { MB_TYPE_16x8 | MB_TYPE_P0L0 | MB_TYPE_P0L1 | MB_TYPE_P1L0, 2 };  // Bi, L0
    table[18] = { MB_TYPE_16x8 | MB_TYPE_P0L0 | MB_TYPE_P0L1 | MB_TYPE_P1L1, 2 };  // Bi, L1
    table[20] = { MB_TYPE_16x8 | MB_TYPE_P0L0 | MB_TYPE_P0L1 | MB_TYPE_P1L0 | MB_TYPE_P1L1, 2 };  // Bi, Bi

    // 8×16 modes (codes 5, 7, 9, 11, 13, 15, 17, 19, 21)
    table[5]  = { MB_TYPE_8x16 | MB_TYPE_P0L0 | MB_TYPE_P1L0, 2 };      // L0, L0
    table[7]  = { MB_TYPE_8x16 | MB_TYPE_P0L1 | MB_TYPE_P1L1, 2 };      // L1, L1
    table[9]  = { MB_TYPE_8x16 | MB_TYPE_P0L0 | MB_TYPE_P1L1, 2 };      // L0, L1
    table[11] = { MB_TYPE_8x16 | MB_TYPE_P0L1 | MB_TYPE_P1L0, 2 };      // L1, L0
    table[13] = { MB_TYPE_8x16 | MB_TYPE_P0L0 | MB_TYPE_P1L0 | MB_TYPE_P1L1, 2 };  // L0, Bi
    table[15] = { MB_TYPE_8x16 | MB_TYPE_P0L1 | MB_TYPE_P1L0 | MB_TYPE_P1L1, 2 };  // L1, Bi
    table[17] = { MB_TYPE_8x16 | MB_TYPE_P0L0 | MB_TYPE_P0L1 | MB_TYPE_P1L0, 2 };  // Bi, L0
    table[19] = { MB_TYPE_8x16 | MB_TYPE_P0L0 | MB_TYPE_P0L1 | MB_TYPE_P1L1, 2 };  // Bi, L1
    table[21] = { MB_TYPE_8x16 | MB_TYPE_P0L0 | MB_TYPE_P0L1 | MB_TYPE_P1L0 | MB_TYPE_P1L1, 2 };  // Bi, Bi

    // 8×8 sub-macroblocks (code 22)
    table[22] = { MB_TYPE_8x8 | MB_TYPE_P0L0 | MB_TYPE_P0L1 | MB_TYPE_P1L0 | MB_TYPE_P1L1, 4 };

    return table;
}

constexpr auto h264_b_mb_type_info = generate_b_mb_type_info();

/**
 * B-frame sub-macroblock type information table.
 *
 * For 8×8 B macroblocks, each 8×8 sub-block can use different prediction:
 * - Code 0: Direct mode
 * - Codes 1-3: 8×8 with L0, L1, or bi-prediction
 * - Codes 4-7: 8×4 partitions with various list combinations
 * - Codes 8-9: 4×8 bi-prediction
 * - Codes 10-12: 4×4 with L0, L1, or bi-prediction
 */
constexpr auto generate_b_sub_mb_type_info() noexcept {
    std::array<PMbInfo, 13> table{};

    table[0]  = { MB_TYPE_DIRECT2, 1 };                                 // Direct

    // 8×8 modes
    table[1]  = { MB_TYPE_16x16 | MB_TYPE_P0L0, 1 };                    // L0
    table[2]  = { MB_TYPE_16x16 | MB_TYPE_P0L1, 1 };                    // L1
    table[3]  = { MB_TYPE_16x16 | MB_TYPE_P0L0 | MB_TYPE_P0L1, 1 };     // Bi

    // 8×4 modes
    table[4]  = { MB_TYPE_16x8 | MB_TYPE_P0L0 | MB_TYPE_P1L0, 2 };      // L0, L0
    table[6]  = { MB_TYPE_16x8 | MB_TYPE_P0L1 | MB_TYPE_P1L1, 2 };      // L1, L1

    // 4×8 modes
    table[5]  = { MB_TYPE_8x16 | MB_TYPE_P0L0 | MB_TYPE_P1L0, 2 };      // L0, L0
    table[7]  = { MB_TYPE_8x16 | MB_TYPE_P0L1 | MB_TYPE_P1L1, 2 };      // L1, L1

    // Complex partition modes
    table[8]  = { MB_TYPE_16x8 | MB_TYPE_P0L0 | MB_TYPE_P0L1 | MB_TYPE_P1L0 | MB_TYPE_P1L1, 2 };  // Bi, Bi (16×8)
    table[9]  = { MB_TYPE_8x16 | MB_TYPE_P0L0 | MB_TYPE_P0L1 | MB_TYPE_P1L0 | MB_TYPE_P1L1, 2 };  // Bi, Bi (8×16)

    // 4×4 modes
    table[10] = { MB_TYPE_8x8 | MB_TYPE_P0L0 | MB_TYPE_P1L0, 4 };       // L0
    table[11] = { MB_TYPE_8x8 | MB_TYPE_P0L1 | MB_TYPE_P1L1, 4 };       // L1
    table[12] = { MB_TYPE_8x8 | MB_TYPE_P0L0 | MB_TYPE_P0L1 | MB_TYPE_P1L0 | MB_TYPE_P1L1, 4 };  // Bi

    return table;
}

constexpr auto h264_b_sub_mb_type_info = generate_b_sub_mb_type_info();

// ============================================================================
// Dequantization Tables (70 bytes)
// ============================================================================

/**
 * Dequantization coefficients for 4×4 blocks.
 *
 * H.264 quantization uses QP-dependent scaling. This table provides the
 * base scaling factors for 4×4 DCT blocks. The actual dequantization
 * multiplier is: dequant4_coeff_init[qp % 6][position] << (qp / 6)
 *
 * These values are from the H.264 specification and ensure that
 * quantization/dequantization approximates the ideal DCT scaling.
 */
constexpr auto generate_dequant4_coeff_init() noexcept {
    std::array<std::array<uint8_t, 3>, 6> table{};

    // From H.264 spec: 6 QP remainders × 3 position classes
    table[0] = { 10, 13, 16 };
    table[1] = { 11, 14, 18 };
    table[2] = { 13, 16, 20 };
    table[3] = { 14, 18, 23 };
    table[4] = { 16, 20, 25 };
    table[5] = { 18, 23, 29 };

    return table;
}

constexpr auto h264_dequant4_coeff_init = generate_dequant4_coeff_init();

/**
 * Scan pattern for 8×8 dequantization coefficient initialization.
 *
 * Maps linear index to position class for 8×8 DCT blocks.
 * Used to index into dequant8_coeff_init during dequantization setup.
 */
constexpr auto generate_dequant8_coeff_init_scan() noexcept {
    std::array<uint8_t, 16> table{};

    // Scan pattern for 8×8 position classes
    constexpr uint8_t values[16] = {
        0, 3, 4, 3,  3, 1, 5, 1,
        4, 5, 2, 5,  3, 1, 5, 1
    };

    for (int i = 0; i < 16; ++i) {
        table[i] = values[i];
    }

    return table;
}

constexpr auto h264_dequant8_coeff_init_scan = generate_dequant8_coeff_init_scan();

/**
 * Dequantization coefficients for 8×8 blocks.
 *
 * Similar to dequant4_coeff_init but for 8×8 DCT blocks (used in High Profile).
 * 6 QP remainders × 6 position classes.
 */
constexpr auto generate_dequant8_coeff_init() noexcept {
    std::array<std::array<uint8_t, 6>, 6> table{};

    // From H.264 spec: 6 QP remainders × 6 position classes
    table[0] = { 20, 18, 32, 19, 25, 24 };
    table[1] = { 22, 19, 35, 21, 28, 26 };
    table[2] = { 26, 23, 42, 24, 33, 31 };
    table[3] = { 28, 25, 45, 26, 35, 33 };
    table[4] = { 32, 28, 51, 30, 40, 38 };
    table[5] = { 36, 32, 58, 34, 46, 43 };

    return table;
}

constexpr auto h264_dequant8_coeff_init = generate_dequant8_coeff_init();

// ============================================================================
// Static Assertions: Comprehensive Validation
// ============================================================================

// Picture type table
static_assert(h264_golomb_to_pict_type.size() == 5, "Picture type table has 5 entries");
static_assert(h264_golomb_to_pict_type[0] == AV_PICTURE_TYPE_P, "Code 0 is P-frame");
static_assert(h264_golomb_to_pict_type[1] == AV_PICTURE_TYPE_B, "Code 1 is B-frame");
static_assert(h264_golomb_to_pict_type[2] == AV_PICTURE_TYPE_I, "Code 2 is I-frame");
static_assert(h264_golomb_to_pict_type[3] == AV_PICTURE_TYPE_SP, "Code 3 is SP-frame");
static_assert(h264_golomb_to_pict_type[4] == AV_PICTURE_TYPE_SI, "Code 4 is SI-frame");

// CBP tables
static_assert(h264_golomb_to_intra4x4_cbp.size() == 48, "Intra CBP table has 48 entries");
static_assert(h264_golomb_to_inter_cbp.size() == 48, "Inter CBP table has 48 entries");
static_assert(h264_golomb_to_intra4x4_cbp[0] == 47, "Intra code 0 → CBP 47");
static_assert(h264_golomb_to_inter_cbp[0] == 0, "Inter code 0 → CBP 0");

// Scan patterns
static_assert(h264_chroma_dc_scan.size() == 4, "Chroma DC scan has 4 entries");
static_assert(h264_chroma422_dc_scan.size() == 8, "Chroma 4:2:2 DC scan has 8 entries");
static_assert(h264_chroma_dc_scan[0] == 0, "First DC position is 0");
static_assert(h264_chroma_dc_scan[1] == 16, "Second DC position is 16");

// I-MB type info
static_assert(h264_i_mb_type_info.size() == 26, "I-MB type table has 26 entries");
static_assert(h264_i_mb_type_info[0].type == MB_TYPE_INTRA4x4, "Code 0 is Intra4×4");
static_assert(h264_i_mb_type_info[25].type == MB_TYPE_INTRA_PCM, "Code 25 is I_PCM");
static_assert((h264_i_mb_type_info[1].type & MB_TYPE_INTRA16x16) != 0, "Code 1 is Intra16×16");

// P-MB type info
static_assert(h264_p_mb_type_info.size() == 5, "P-MB type table has 5 entries");
static_assert(h264_p_sub_mb_type_info.size() == 4, "P sub-MB type table has 4 entries");
static_assert(h264_p_mb_type_info[0].partition_count == 1, "16×16 has 1 partition");
static_assert(h264_p_mb_type_info[3].partition_count == 4, "8×8 has 4 partitions");

// B-MB type info
static_assert(h264_b_mb_type_info.size() == 23, "B-MB type table has 23 entries");
static_assert(h264_b_sub_mb_type_info.size() == 13, "B sub-MB type table has 13 entries");
static_assert((h264_b_mb_type_info[0].type & MB_TYPE_DIRECT2) != 0, "Code 0 is Direct");
static_assert(h264_b_mb_type_info[22].partition_count == 4, "8×8 has 4 partitions");

// Dequant tables
static_assert(h264_dequant4_coeff_init.size() == 6, "Dequant4 has 6 QP remainders");
static_assert(h264_dequant8_coeff_init.size() == 6, "Dequant8 has 6 QP remainders");
static_assert(h264_dequant8_coeff_init_scan.size() == 16, "Dequant8 scan has 16 entries");
static_assert(h264_dequant4_coeff_init[0][0] == 10, "QP%6=0, pos 0 → 10");
static_assert(h264_dequant4_coeff_init[5][2] == 29, "QP%6=5, pos 2 → 29");

// Verify table values match spec
static_assert(h264_dequant8_coeff_init[0][0] == 20, "8×8: QP%6=0, pos 0 → 20");
static_assert(h264_dequant8_coeff_init[5][5] == 43, "8×8: QP%6=5, pos 5 → 43");

} // namespace FFmpegH264DecoderData

// ============================================================================
// C API Compatibility
// ============================================================================

extern "C" {

/**
 * Get pointer to H.264 picture type mapping table.
 * Returns: Pointer to 5-element uint8_t array
 */
inline const uint8_t *get_h264_golomb_to_pict_type() {
    return FFmpegH264DecoderData::h264_golomb_to_pict_type.data();
}

/**
 * Get pointer to H.264 intra 4×4 CBP mapping table.
 * Returns: Pointer to 48-element uint8_t array
 */
inline const uint8_t *get_h264_golomb_to_intra4x4_cbp() {
    return FFmpegH264DecoderData::h264_golomb_to_intra4x4_cbp.data();
}

/**
 * Get pointer to H.264 inter CBP mapping table.
 * Returns: Pointer to 48-element uint8_t array
 */
inline const uint8_t *get_h264_golomb_to_inter_cbp() {
    return FFmpegH264DecoderData::h264_golomb_to_inter_cbp.data();
}

/**
 * Get pointer to H.264 chroma DC scan pattern.
 * Returns: Pointer to 4-element uint8_t array
 */
inline const uint8_t *get_h264_chroma_dc_scan() {
    return FFmpegH264DecoderData::h264_chroma_dc_scan.data();
}

/**
 * Get pointer to H.264 chroma 4:2:2 DC scan pattern.
 * Returns: Pointer to 8-element uint8_t array
 */
inline const uint8_t *get_h264_chroma422_dc_scan() {
    return FFmpegH264DecoderData::h264_chroma422_dc_scan.data();
}

} // extern "C"

#endif // AVCODEC_H264_DECODER_DATA_TABLEGEN_CONSTEXPR_HPP
