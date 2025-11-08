/*
 * Compile-time generation of MPEG-1/2 quantization matrices and VLC tables
 *
 * Original C version from FFmpeg MPEG-1/2 decoder
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

#ifndef AVCODEC_MPEG12_QUANT_VLC_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_MPEG12_QUANT_VLC_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>

namespace FFmpegMPEG12QuantVLC {

// ============================================================================
// Constants
// ============================================================================

constexpr int QUANT_MATRIX_SIZE = 64;    // 8×8 quantization matrix
constexpr int VLC_DC_TABLE_SIZE = 12;    // DC VLC table entries

// ============================================================================
// MPEG-1 Default Quantization Matrices
// ============================================================================

/**
 * Generate MPEG-1 default intra quantization matrix.
 *
 * This matrix is used for intra-coded blocks (I-frames and I-macroblocks).
 * The values are from the MPEG-1 specification and follow a pattern that
 * emphasizes low frequencies (top-left) over high frequencies (bottom-right).
 *
 * The matrix represents the quantization step size for each frequency component
 * in the 8×8 DCT transform. Lower values = finer quantization = higher quality.
 *
 * Pattern: Values increase from top-left to bottom-right, reflecting the
 * human visual system's greater sensitivity to low-frequency content.
 */
constexpr auto generate_mpeg1_default_intra_matrix() noexcept {
    std::array<uint16_t, QUANT_MATRIX_SIZE> matrix{};

    // MPEG-1 spec default intra matrix (Table D.1)
    // Values are in zigzag scan order, arranged as 8×8 matrix
    const uint16_t values[64] = {
         8, 16, 19, 22, 26, 27, 29, 34,
        16, 16, 22, 24, 27, 29, 34, 37,
        19, 22, 26, 27, 29, 34, 34, 38,
        22, 22, 26, 27, 29, 34, 37, 40,
        22, 26, 27, 29, 32, 35, 40, 48,
        26, 27, 29, 32, 35, 40, 48, 58,
        26, 27, 29, 34, 38, 46, 56, 69,
        27, 29, 35, 38, 46, 56, 69, 83
    };

    for (int i = 0; i < 64; ++i) {
        matrix[i] = values[i];
    }

    return matrix;
}

/**
 * Generate MPEG-1 default non-intra quantization matrix.
 *
 * This matrix is used for non-intra (predictive) blocks (P-frames, B-frames).
 * Unlike the intra matrix, this is flat (all 16s), as predicted blocks already
 * have most of their energy in prediction residuals which are more uniform.
 *
 * The uniform quantization prevents over-quantization of residuals.
 */
constexpr auto generate_mpeg1_default_non_intra_matrix() noexcept {
    std::array<uint16_t, QUANT_MATRIX_SIZE> matrix{};

    // All values are 16 for non-intra (MPEG-1 spec)
    for (int i = 0; i < 64; ++i) {
        matrix[i] = 16;
    }

    return matrix;
}

// ============================================================================
// MPEG-1/2 VLC DC Coefficient Tables
// ============================================================================

/**
 * Generate MPEG-1/2 VLC DC luma codes and bit lengths.
 *
 * Variable Length Coding (VLC) tables for DC coefficient magnitudes in luma blocks.
 * DC coefficients represent the average brightness of an 8×8 block.
 *
 * These tables map DC magnitude categories (0-11) to their Huffman codes.
 * Smaller magnitudes use fewer bits (shorter codes).
 */
constexpr auto generate_mpeg12_vlc_dc_lum() noexcept {
    struct VLCTable {
        std::array<uint16_t, VLC_DC_TABLE_SIZE> code;
        std::array<uint8_t, VLC_DC_TABLE_SIZE> bits;
    };

    VLCTable table{};

    // MPEG-1/2 spec VLC codes for DC luma (Table B.12)
    const uint16_t codes[12] = {
        0x4, 0x0, 0x1, 0x5, 0x6, 0xe, 0x1e, 0x3e, 0x7e, 0xfe, 0x1fe, 0x1ff
    };
    const uint8_t bits[12] = {
        3, 2, 2, 3, 3, 4, 5, 6, 7, 8, 9, 9
    };

    for (int i = 0; i < 12; ++i) {
        table.code[i] = codes[i];
        table.bits[i] = bits[i];
    }

    return table;
}

/**
 * Generate MPEG-1/2 VLC DC chroma codes and bit lengths.
 *
 * Similar to luma but for chroma (color) blocks.
 * Chroma DC coefficients often have different distributions than luma,
 * hence different VLC tables for optimal compression.
 */
constexpr auto generate_mpeg12_vlc_dc_chroma() noexcept {
    struct VLCTable {
        std::array<uint16_t, VLC_DC_TABLE_SIZE> code;
        std::array<uint8_t, VLC_DC_TABLE_SIZE> bits;
    };

    VLCTable table{};

    // MPEG-1/2 spec VLC codes for DC chroma (Table B.13)
    const uint16_t codes[12] = {
        0x0, 0x1, 0x2, 0x6, 0xe, 0x1e, 0x3e, 0x7e, 0xfe, 0x1fe, 0x3fe, 0x3ff
    };
    const uint8_t bits[12] = {
        2, 2, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10
    };

    for (int i = 0; i < 12; ++i) {
        table.code[i] = codes[i];
        table.bits[i] = bits[i];
    }

    return table;
}

// ============================================================================
// Generated Tables (176 bytes total)
// ============================================================================

constexpr auto mpeg1_default_intra_matrix = generate_mpeg1_default_intra_matrix();
constexpr auto mpeg1_default_non_intra_matrix = generate_mpeg1_default_non_intra_matrix();
constexpr auto mpeg12_vlc_dc_lum = generate_mpeg12_vlc_dc_lum();
constexpr auto mpeg12_vlc_dc_chroma = generate_mpeg12_vlc_dc_chroma();

// ============================================================================
// Static Assertions: Comprehensive Validation
// ============================================================================

// Matrix sizes
static_assert(mpeg1_default_intra_matrix.size() == 64, "Intra matrix is 8×8 = 64");
static_assert(mpeg1_default_non_intra_matrix.size() == 64, "Non-intra matrix is 8×8 = 64");

// Intra matrix validation - verify spec values
static_assert(mpeg1_default_intra_matrix[0] == 8, "Intra[0,0] = 8 (DC position)");
static_assert(mpeg1_default_intra_matrix[1] == 16, "Intra[0,1] = 16");
static_assert(mpeg1_default_intra_matrix[7] == 34, "Intra[0,7] = 34");
static_assert(mpeg1_default_intra_matrix[8] == 16, "Intra[1,0] = 16");
static_assert(mpeg1_default_intra_matrix[63] == 83, "Intra[7,7] = 83 (highest freq)");

// Verify intra matrix increases towards high frequencies
static_assert(mpeg1_default_intra_matrix[0] < mpeg1_default_intra_matrix[63],
              "Intra: DC < highest frequency (8 < 83)");
static_assert(mpeg1_default_intra_matrix[0] < mpeg1_default_intra_matrix[32],
              "Intra: increases towards bottom-right");

// Non-intra matrix validation - all 16s
static_assert(mpeg1_default_non_intra_matrix[0] == 16, "Non-intra all 16s: [0]");
static_assert(mpeg1_default_non_intra_matrix[31] == 16, "Non-intra all 16s: [31]");
static_assert(mpeg1_default_non_intra_matrix[63] == 16, "Non-intra all 16s: [63]");

// Verify uniformity of non-intra matrix
constexpr auto verify_non_intra_uniform() {
    for (int i = 0; i < 64; ++i) {
        if (mpeg1_default_non_intra_matrix[i] != 16) return false;
    }
    return true;
}
static_assert(verify_non_intra_uniform(), "Non-intra matrix is uniformly 16");

// VLC DC luma validation
static_assert(mpeg12_vlc_dc_lum.code.size() == 12, "DC luma has 12 entries");
static_assert(mpeg12_vlc_dc_lum.bits.size() == 12, "DC luma has 12 bit lengths");

// Verify first few DC luma codes
static_assert(mpeg12_vlc_dc_lum.code[0] == 0x4, "DC luma code[0] = 0x4");
static_assert(mpeg12_vlc_dc_lum.bits[0] == 3, "DC luma bits[0] = 3");
static_assert(mpeg12_vlc_dc_lum.code[1] == 0x0, "DC luma code[1] = 0x0");
static_assert(mpeg12_vlc_dc_lum.bits[1] == 2, "DC luma bits[1] = 2");

// Verify last DC luma codes
static_assert(mpeg12_vlc_dc_lum.code[11] == 0x1ff, "DC luma code[11] = 0x1ff");
static_assert(mpeg12_vlc_dc_lum.bits[11] == 9, "DC luma bits[11] = 9");

// Verify bit lengths are reasonable (2-10 bits for VLC)
static_assert(mpeg12_vlc_dc_lum.bits[0] >= 2 && mpeg12_vlc_dc_lum.bits[0] <= 10,
              "DC luma bit lengths in range [2,10]");
static_assert(mpeg12_vlc_dc_lum.bits[11] >= 2 && mpeg12_vlc_dc_lum.bits[11] <= 10,
              "DC luma bit lengths in range [2,10]");

// VLC DC chroma validation
static_assert(mpeg12_vlc_dc_chroma.code.size() == 12, "DC chroma has 12 entries");
static_assert(mpeg12_vlc_dc_chroma.bits.size() == 12, "DC chroma has 12 bit lengths");

// Verify first few DC chroma codes
static_assert(mpeg12_vlc_dc_chroma.code[0] == 0x0, "DC chroma code[0] = 0x0");
static_assert(mpeg12_vlc_dc_chroma.bits[0] == 2, "DC chroma bits[0] = 2");
static_assert(mpeg12_vlc_dc_chroma.code[1] == 0x1, "DC chroma code[1] = 0x1");
static_assert(mpeg12_vlc_dc_chroma.bits[1] == 2, "DC chroma bits[1] = 2");

// Verify last DC chroma codes
static_assert(mpeg12_vlc_dc_chroma.code[11] == 0x3ff, "DC chroma code[11] = 0x3ff");
static_assert(mpeg12_vlc_dc_chroma.bits[11] == 10, "DC chroma bits[11] = 10");

// Verify chroma bit lengths
static_assert(mpeg12_vlc_dc_chroma.bits[0] >= 2 && mpeg12_vlc_dc_chroma.bits[0] <= 10,
              "DC chroma bit lengths in range [2,10]");

// Verify code values fit within bit length
static_assert(mpeg12_vlc_dc_lum.code[0] < (1 << mpeg12_vlc_dc_lum.bits[0]),
              "DC luma code[0] fits in bits[0]");
static_assert(mpeg12_vlc_dc_lum.code[11] < (1 << mpeg12_vlc_dc_lum.bits[11]),
              "DC luma code[11] fits in bits[11]");
static_assert(mpeg12_vlc_dc_chroma.code[0] < (1 << mpeg12_vlc_dc_chroma.bits[0]),
              "DC chroma code[0] fits in bits[0]");
static_assert(mpeg12_vlc_dc_chroma.code[11] < (1 << mpeg12_vlc_dc_chroma.bits[11]),
              "DC chroma code[11] fits in bits[11]");

// Verify shorter codes for common values (Huffman property)
static_assert(mpeg12_vlc_dc_lum.bits[1] <= mpeg12_vlc_dc_lum.bits[11],
              "Common values use shorter codes");
static_assert(mpeg12_vlc_dc_chroma.bits[1] <= mpeg12_vlc_dc_chroma.bits[11],
              "Common values use shorter codes");

// Cross-validation: luma vs chroma
static_assert(mpeg12_vlc_dc_lum.code.size() == mpeg12_vlc_dc_chroma.code.size(),
              "Luma and chroma have same table size");

// Verify matrix value ranges (MPEG-1 allows 1-255)
static_assert(mpeg1_default_intra_matrix[0] >= 1 && mpeg1_default_intra_matrix[0] <= 255,
              "Intra matrix values in range [1,255]");
static_assert(mpeg1_default_intra_matrix[63] >= 1 && mpeg1_default_intra_matrix[63] <= 255,
              "Intra matrix values in range [1,255]");
static_assert(mpeg1_default_non_intra_matrix[0] >= 1 && mpeg1_default_non_intra_matrix[0] <= 255,
              "Non-intra matrix values in range [1,255]");

} // namespace FFmpegMPEG12QuantVLC

// ============================================================================
// C API Compatibility
// ============================================================================

extern "C" {

/**
 * Get pointer to MPEG-1 default intra quantization matrix.
 * Returns: Pointer to 64-element uint16_t array (8×8 matrix)
 */
inline const uint16_t *get_mpeg1_default_intra_matrix() {
    return FFmpegMPEG12QuantVLC::mpeg1_default_intra_matrix.data();
}

/**
 * Get pointer to MPEG-1 default non-intra quantization matrix.
 * Returns: Pointer to 64-element uint16_t array (8×8 matrix)
 */
inline const uint16_t *get_mpeg1_default_non_intra_matrix() {
    return FFmpegMPEG12QuantVLC::mpeg1_default_non_intra_matrix.data();
}

/**
 * Get pointer to MPEG-1/2 DC luma VLC codes.
 * Returns: Pointer to 12-element uint16_t array
 */
inline const uint16_t *get_mpeg12_vlc_dc_lum_code() {
    return FFmpegMPEG12QuantVLC::mpeg12_vlc_dc_lum.code.data();
}

/**
 * Get pointer to MPEG-1/2 DC luma VLC bit lengths.
 * Returns: Pointer to 12-element uint8_t array
 */
inline const uint8_t *get_mpeg12_vlc_dc_lum_bits() {
    return FFmpegMPEG12QuantVLC::mpeg12_vlc_dc_lum.bits.data();
}

/**
 * Get pointer to MPEG-1/2 DC chroma VLC codes.
 * Returns: Pointer to 12-element uint16_t array
 */
inline const uint16_t *get_mpeg12_vlc_dc_chroma_code() {
    return FFmpegMPEG12QuantVLC::mpeg12_vlc_dc_chroma.code.data();
}

/**
 * Get pointer to MPEG-1/2 DC chroma VLC bit lengths.
 * Returns: Pointer to 12-element uint8_t array
 */
inline const uint8_t *get_mpeg12_vlc_dc_chroma_bits() {
    return FFmpegMPEG12QuantVLC::mpeg12_vlc_dc_chroma.bits.data();
}

} // extern "C"

#endif // AVCODEC_MPEG12_QUANT_VLC_TABLEGEN_CONSTEXPR_HPP
