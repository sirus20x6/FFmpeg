/*
 * Compile-time generation of CAVS (Chinese AVS) codec data tables
 *
 * Original C version from FFmpeg CAVS decoder
 * C++20 constexpr version created 2025-11-08
 *
 * Chinese AVS video (AVS1-P2, JiZhun profile) decoder.
 * Copyright (c) 2006  Stefan Gehrer <stefan.gehrer@gmx.de>
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

#ifndef AVCODEC_CAVS_DATA_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_CAVS_DATA_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>

namespace FFmpegCAVSData {

// ============================================================================
// Constants
// ============================================================================

// Partition flags for macroblock types
constexpr uint8_t FWD0   = 0x01;  // Forward reference 0
constexpr uint8_t FWD1   = 0x02;  // Forward reference 1
constexpr uint8_t BWD0   = 0x04;  // Backward reference 0
constexpr uint8_t BWD1   = 0x08;  // Backward reference 1
constexpr uint8_t SYM0   = 0x10;  // Symmetric prediction 0
constexpr uint8_t SYM1   = 0x20;  // Symmetric prediction 1
constexpr uint8_t SPLITH = 0x40;  // Horizontal split
constexpr uint8_t SPLITV = 0x80;  // Vertical split

// Reference types for motion vectors
constexpr int16_t REF_DIR   = -3;  // Directional reference (no prediction)
constexpr int16_t REF_INTRA = -2;  // Intra prediction

// ============================================================================
// CAVS Motion Vector Structure
// ============================================================================

/**
 * CAVS motion vector structure.
 *
 * Represents a motion vector with position, distance, and reference info.
 * Used throughout CAVS decoder for motion compensation.
 */
struct CavsVector {
    int16_t x;      // Horizontal component
    int16_t y;      // Vertical component
    int16_t dist;   // Temporal distance
    int16_t ref;    // Reference frame index or special value
};

// ============================================================================
// CAVS Partition Flags Table (30 bytes)
// ============================================================================

/**
 * Generate CAVS macroblock partition flags table.
 *
 * CAVS (Chinese Audio Video Standard) uses these flags to indicate
 * the partitioning and prediction modes for different macroblock types.
 *
 * The table covers 30 macroblock types from I_8X8 to B_8X8:
 * - Intra modes (I_8X8)
 * - P-frame modes (P_SKIP, P_16X16, P_16X8, P_8X16, P_8X8)
 * - B-frame modes (B_SKIP, B_DIRECT, various forward/backward/symmetric)
 *
 * Flags indicate:
 * - FWD0/FWD1: Forward prediction from reference 0/1
 * - BWD0/BWD1: Backward prediction from reference 0/1
 * - SYM0/SYM1: Symmetric (bidirectional) prediction
 * - SPLITH: Horizontal partitioning (16×8 or 8×4)
 * - SPLITV: Vertical partitioning (8×16 or 4×8)
 *
 * Index corresponds to macroblock type enum in CAVS decoder.
 */
constexpr auto generate_cavs_partition_flags() noexcept {
    std::array<uint8_t, 30> table{};

    // Macroblock type enumeration (implicit from index):
    table[0]  = 0;                       // I_8X8 - intra 8×8
    table[1]  = 0;                       // P_SKIP - P skip mode
    table[2]  = 0;                       // P_16X16 - P 16×16
    table[3]  = SPLITH;                  // P_16X8 - P horizontal split
    table[4]  = SPLITV;                  // P_8X16 - P vertical split
    table[5]  = SPLITH | SPLITV;         // P_8X8 - P both splits
    table[6]  = SPLITH | SPLITV;         // B_SKIP - B skip mode
    table[7]  = SPLITH | SPLITV;         // B_DIRECT - B direct mode
    table[8]  = 0;                       // B_FWD_16X16 - B forward 16×16
    table[9]  = 0;                       // B_BWD_16X16 - B backward 16×16
    table[10] = 0;                       // B_SYM_16X16 - B symmetric 16×16

    // B-frame 16×8 and 8×16 modes with various prediction combinations
    table[11] = FWD0 | FWD1 | SPLITH;                   // Both forward, H-split
    table[12] = FWD0 | FWD1 | SPLITV;                   // Both forward, V-split
    table[13] = BWD0 | BWD1 | SPLITH;                   // Both backward, H-split
    table[14] = BWD0 | BWD1 | SPLITV;                   // Both backward, V-split
    table[15] = FWD0 | BWD1 | SPLITH;                   // Mixed fwd/bwd, H-split
    table[16] = FWD0 | BWD1 | SPLITV;                   // Mixed fwd/bwd, V-split
    table[17] = BWD0 | FWD1 | SPLITH;                   // Mixed bwd/fwd, H-split
    table[18] = BWD0 | FWD1 | SPLITV;                   // Mixed bwd/fwd, V-split
    table[19] = FWD0 | FWD1 | SYM1 | SPLITH;            // Fwd + sym, H-split
    table[20] = FWD0 | FWD1 | SYM1 | SPLITV;            // Fwd + sym, V-split
    table[21] = BWD0 | FWD1 | SYM1 | SPLITH;            // Bwd/fwd + sym, H-split
    table[22] = BWD0 | FWD1 | SYM1 | SPLITV;            // Bwd/fwd + sym, V-split
    table[23] = FWD0 | FWD1 | SYM0 | SPLITH;            // Fwd + sym0, H-split
    table[24] = FWD0 | FWD1 | SYM0 | SPLITV;            // Fwd + sym0, V-split
    table[25] = FWD0 | BWD1 | SYM0 | SPLITH;            // Fwd/bwd + sym0, H-split
    table[26] = FWD0 | BWD1 | SYM0 | SPLITV;            // Fwd/bwd + sym0, V-split
    table[27] = FWD0 | FWD1 | SYM0 | SYM1 | SPLITH;     // All modes, H-split
    table[28] = FWD0 | FWD1 | SYM0 | SYM1 | SPLITV;     // All modes, V-split
    table[29] = SPLITH | SPLITV;                         // B_8X8 - both splits

    return table;
}

constexpr auto cavs_partition_flags = generate_cavs_partition_flags();

// ============================================================================
// CAVS Chroma QP Table (64 bytes)
// ============================================================================

/**
 * Generate CAVS chroma quantization parameter table.
 *
 * Maps luma QP (0-63) to chroma QP for CAVS video codec.
 * This table implements the QP mapping specified in the Chinese AVS
 * standard for chroma quantization.
 *
 * Properties:
 * - Linear mapping for QP 0-41 (identity: chroma_qp = luma_qp)
 * - Saturating behavior for QP 42-63 (prevents over-quantization)
 * - Maximum chroma QP is 51 (even when luma QP reaches 63)
 *
 * This mapping ensures chroma is quantized less aggressively than
 * luma at high QP values, preserving color fidelity.
 */
constexpr auto generate_cavs_chroma_qp() noexcept {
    std::array<uint8_t, 64> table{};

    // Linear region (0-41): chroma QP = luma QP
    for (int i = 0; i < 42; ++i) {
        table[i] = static_cast<uint8_t>(i);
    }

    // Saturation region (42-63): reduce chroma QP growth
    // Pattern from AVS spec: 42,42,43,43,44,44,45,45,46,46,47,47,48,48,48,49,49,49,50,50,50,51
    table[42] = 42;
    table[43] = 42;
    table[44] = 43;
    table[45] = 43;
    table[46] = 44;
    table[47] = 44;
    table[48] = 45;
    table[49] = 45;
    table[50] = 46;
    table[51] = 46;
    table[52] = 47;
    table[53] = 47;
    table[54] = 48;
    table[55] = 48;
    table[56] = 48;
    table[57] = 49;
    table[58] = 49;
    table[59] = 49;
    table[60] = 50;
    table[61] = 50;
    table[62] = 50;
    table[63] = 51;

    return table;
}

constexpr auto cavs_chroma_qp = generate_cavs_chroma_qp();

// ============================================================================
// Special Motion Vectors (16 bytes)
// ============================================================================

/**
 * CAVS directional motion vector (no prediction from this direction).
 *
 * Marks a block as having no prediction from a specific direction.
 * For example, a forward motion vector in a backward-only partition.
 *
 * Structure: {x=0, y=0, dist=1, ref=REF_DIR}
 */
constexpr CavsVector cavs_dir_mv = {0, 0, 1, REF_DIR};

/**
 * CAVS intra prediction motion vector.
 *
 * Marks a block as using intra prediction (no motion compensation).
 *
 * Structure: {x=0, y=0, dist=1, ref=REF_INTRA}
 */
constexpr CavsVector cavs_intra_mv = {0, 0, 1, REF_INTRA};

// ============================================================================
// Static Assertions: Comprehensive Validation
// ============================================================================

// Table sizes
static_assert(cavs_partition_flags.size() == 30, "Partition flags table has 30 entries");
static_assert(cavs_chroma_qp.size() == 64, "Chroma QP table has 64 entries");

// Partition flag constants verify correct bit patterns
static_assert(FWD0 == 0x01, "FWD0 flag is 0x01");
static_assert(FWD1 == 0x02, "FWD1 flag is 0x02");
static_assert(BWD0 == 0x04, "BWD0 flag is 0x04");
static_assert(BWD1 == 0x08, "BWD1 flag is 0x08");
static_assert(SYM0 == 0x10, "SYM0 flag is 0x10");
static_assert(SYM1 == 0x20, "SYM1 flag is 0x20");
static_assert(SPLITH == 0x40, "SPLITH flag is 0x40");
static_assert(SPLITV == 0x80, "SPLITV flag is 0x80");

// Verify simple partition modes
static_assert(cavs_partition_flags[0] == 0, "I_8X8: no flags");
static_assert(cavs_partition_flags[1] == 0, "P_SKIP: no flags");
static_assert(cavs_partition_flags[2] == 0, "P_16X16: no flags");
static_assert(cavs_partition_flags[3] == SPLITH, "P_16X8: horizontal split");
static_assert(cavs_partition_flags[4] == SPLITV, "P_8X16: vertical split");
static_assert(cavs_partition_flags[5] == (SPLITH | SPLITV), "P_8X8: both splits");

// Verify B-frame modes
static_assert(cavs_partition_flags[6] == (SPLITH | SPLITV), "B_SKIP: both splits");
static_assert(cavs_partition_flags[7] == (SPLITH | SPLITV), "B_DIRECT: both splits");
static_assert(cavs_partition_flags[8] == 0, "B_FWD_16X16: no flags");
static_assert(cavs_partition_flags[9] == 0, "B_BWD_16X16: no flags");
static_assert(cavs_partition_flags[10] == 0, "B_SYM_16X16: no flags");

// Verify complex B-frame partition modes
static_assert(cavs_partition_flags[11] == (FWD0 | FWD1 | SPLITH),
              "Index 11: both forward + H-split");
static_assert(cavs_partition_flags[29] == (SPLITH | SPLITV),
              "B_8X8: both splits");

// Chroma QP linear region
static_assert(cavs_chroma_qp[0] == 0, "Chroma QP[0] = 0");
static_assert(cavs_chroma_qp[10] == 10, "Chroma QP[10] = 10 (linear)");
static_assert(cavs_chroma_qp[41] == 41, "Chroma QP[41] = 41 (last linear)");

// Chroma QP saturation region
static_assert(cavs_chroma_qp[42] == 42, "Chroma QP[42] = 42");
static_assert(cavs_chroma_qp[43] == 42, "Chroma QP[43] = 42 (saturated)");
static_assert(cavs_chroma_qp[44] == 43, "Chroma QP[44] = 43");
static_assert(cavs_chroma_qp[45] == 43, "Chroma QP[45] = 43 (saturated)");
static_assert(cavs_chroma_qp[63] == 51, "Chroma QP[63] = 51 (max)");

// Verify chroma QP is monotonic non-decreasing
constexpr bool verify_chroma_qp_monotonic() {
    for (int i = 0; i < 63; ++i) {
        if (cavs_chroma_qp[i] > cavs_chroma_qp[i + 1]) {
            return false;  // Should never decrease
        }
    }
    return true;
}
static_assert(verify_chroma_qp_monotonic(), "Chroma QP table is monotonic non-decreasing");

// Verify maximum chroma QP is 51
constexpr bool verify_max_chroma_qp() {
    for (int i = 0; i < 64; ++i) {
        if (cavs_chroma_qp[i] > 51) {
            return false;
        }
    }
    return true;
}
static_assert(verify_max_chroma_qp(), "Maximum chroma QP is 51");

// Special motion vectors
static_assert(cavs_dir_mv.x == 0, "Dir MV: x = 0");
static_assert(cavs_dir_mv.y == 0, "Dir MV: y = 0");
static_assert(cavs_dir_mv.dist == 1, "Dir MV: dist = 1");
static_assert(cavs_dir_mv.ref == REF_DIR, "Dir MV: ref = REF_DIR");

static_assert(cavs_intra_mv.x == 0, "Intra MV: x = 0");
static_assert(cavs_intra_mv.y == 0, "Intra MV: y = 0");
static_assert(cavs_intra_mv.dist == 1, "Intra MV: dist = 1");
static_assert(cavs_intra_mv.ref == REF_INTRA, "Intra MV: ref = REF_INTRA");

// Verify REF constants are distinct
static_assert(REF_DIR != REF_INTRA, "REF_DIR and REF_INTRA are distinct");
static_assert(REF_DIR == -3, "REF_DIR = -3");
static_assert(REF_INTRA == -2, "REF_INTRA = -2");

} // namespace FFmpegCAVSData

// ============================================================================
// C API Compatibility
// ============================================================================

extern "C" {

/**
 * Get pointer to CAVS partition flags table.
 * Returns: Pointer to 30-element uint8_t array
 */
inline const uint8_t *get_cavs_partition_flags() {
    return FFmpegCAVSData::cavs_partition_flags.data();
}

/**
 * Get pointer to CAVS chroma QP table.
 * Returns: Pointer to 64-element uint8_t array
 */
inline const uint8_t *get_cavs_chroma_qp() {
    return FFmpegCAVSData::cavs_chroma_qp.data();
}

/**
 * Get CAVS directional motion vector.
 * Returns: Const reference to directional MV
 */
inline const FFmpegCAVSData::CavsVector& get_cavs_dir_mv() {
    return FFmpegCAVSData::cavs_dir_mv;
}

/**
 * Get CAVS intra prediction motion vector.
 * Returns: Const reference to intra MV
 */
inline const FFmpegCAVSData::CavsVector& get_cavs_intra_mv() {
    return FFmpegCAVSData::cavs_intra_mv;
}

} // extern "C"

#endif // AVCODEC_CAVS_DATA_TABLEGEN_CONSTEXPR_HPP
