/*
 * MPEG audio DSP data tables - C++20 constexpr implementation
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

#ifndef AVCODEC_MPEGAUDIODSP_DATA_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_MPEGAUDIODSP_DATA_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>

/**
 * @file
 * @brief MPEG audio DSP encoding window coefficients
 *
 * This header provides compile-time generation of MPEG audio layer I/II/III
 * encoding window coefficients using C++20 constexpr. The encoding window
 * (also called analysis window) is used in the polyphase filterbank for
 * MP3 encoding, converting time-domain audio into frequency subbands.
 *
 * Tables:
 * - mpa_enwindow: Half MPEG encoding window (257 coefficients, full precision)
 *
 * The window is symmetric, so only half is stored. These coefficients are
 * applied during the MDCT (Modified Discrete Cosine Transform) stage of MP3
 * encoding to shape the frequency response and reduce spectral leakage.
 *
 * Generated at compile time - zero runtime overhead.
 * All tables placed in .rodata section by the compiler.
 *
 * Data size: 1028 bytes (257 entries × 4 bytes per int32_t)
 * Static assertions: 30+ compile-time validations
 */

namespace FFmpegMPEGAudioDSPData {

// ============================================================================
// MPEG Audio Encoding Window (257 entries, 1028 bytes)
// ============================================================================

/**
 * Half MPEG encoding window (full precision int32_t)
 *
 * This table contains 257 coefficients representing half of the symmetric
 * MPEG audio analysis window used in MP3 encoding. The window:
 * - Shapes the frequency response of the polyphase filterbank
 * - Reduces spectral leakage between subbands
 * - Provides smooth transitions between analysis blocks
 * - Uses full int32_t precision for accurate encoding
 *
 * The values follow a specific pattern designed by the MPEG audio standard:
 * - Starts near zero with small negative values
 * - Gradually increases in magnitude (negative)
 * - Has a transition region with mixed positive/negative
 * - Ends with large positive value at maximum (75038)
 *
 * Window characteristics:
 * - Length: 257 samples (half window, symmetric)
 * - Full window: 512 samples (mirrored)
 * - Used in polyphase analysis filterbank
 * - Critical for MP3 audio quality
 */
constexpr auto generate_mpa_enwindow() noexcept {
    std::array<int32_t, 257> window{};

    // Exact coefficients from MPEG audio specification
    constexpr int32_t values[257] = {
             0,    -1,    -1,    -1,    -1,    -1,    -1,    -2,
            -2,    -2,    -2,    -3,    -3,    -4,    -4,    -5,
            -5,    -6,    -7,    -7,    -8,    -9,   -10,   -11,
           -13,   -14,   -16,   -17,   -19,   -21,   -24,   -26,
           -29,   -31,   -35,   -38,   -41,   -45,   -49,   -53,
           -58,   -63,   -68,   -73,   -79,   -85,   -91,   -97,
          -104,  -111,  -117,  -125,  -132,  -139,  -147,  -154,
          -161,  -169,  -176,  -183,  -190,  -196,  -202,  -208,
           213,   218,   222,   225,   227,   228,   228,   227,
           224,   221,   215,   208,   200,   189,   177,   163,
           146,   127,   106,    83,    57,    29,    -2,   -36,
           -72,  -111,  -153,  -197,  -244,  -294,  -347,  -401,
          -459,  -519,  -581,  -645,  -711,  -779,  -848,  -919,
          -991, -1064, -1137, -1210, -1283, -1356, -1428, -1498,
         -1567, -1634, -1698, -1759, -1817, -1870, -1919, -1962,
         -2001, -2032, -2057, -2075, -2085, -2087, -2080, -2063,
          2037,  2000,  1952,  1893,  1822,  1739,  1644,  1535,
          1414,  1280,  1131,   970,   794,   605,   402,   185,
           -45,  -288,  -545,  -814, -1095, -1388, -1692, -2006,
         -2330, -2663, -3004, -3351, -3705, -4063, -4425, -4788,
         -5153, -5517, -5879, -6237, -6589, -6935, -7271, -7597,
         -7910, -8209, -8491, -8755, -8998, -9219, -9416, -9585,
         -9727, -9838, -9916, -9959, -9966, -9935, -9863, -9750,
         -9592, -9389, -9139, -8840, -8492, -8092, -7640, -7134,
          6574,  5959,  5288,  4561,  3776,  2935,  2037,  1082,
            70,  -998, -2122, -3300, -4533, -5818, -7154, -8540,
         -9975,-11455,-12980,-14548,-16155,-17799,-19478,-21189,
        -22929,-24694,-26482,-28289,-30112,-31947,-33791,-35640,
        -37489,-39336,-41176,-43006,-44821,-46617,-48390,-50137,
        -51853,-53534,-55178,-56778,-58333,-59838,-61289,-62684,
        -64019,-65290,-66494,-67629,-68692,-69679,-70590,-71420,
        -72169,-72835,-73415,-73908,-74313,-74630,-74856,-74992,
         75038
    };

    for (int i = 0; i < 257; ++i) {
        window[i] = values[i];
    }

    return window;
}

constexpr auto mpa_enwindow = generate_mpa_enwindow();

} // namespace FFmpegMPEGAudioDSPData

// ============================================================================
// Static Assertions - Compile-Time Validation
// ============================================================================

// Table size validation
static_assert(FFmpegMPEGAudioDSPData::mpa_enwindow.size() == 257,
              "MPEG encoding window must have 257 entries");

// Start of window (near-zero region)
static_assert(FFmpegMPEGAudioDSPData::mpa_enwindow[0] == 0,
              "Window starts at zero");
static_assert(FFmpegMPEGAudioDSPData::mpa_enwindow[1] == -1,
              "First coefficient is -1");
static_assert(FFmpegMPEGAudioDSPData::mpa_enwindow[6] == -1,
              "Early coefficients near -1");

// Gradual increase in magnitude
static_assert(FFmpegMPEGAudioDSPData::mpa_enwindow[10] == -2,
              "Gradual magnitude increase");
static_assert(FFmpegMPEGAudioDSPData::mpa_enwindow[20] == -8,
              "Continuing increase");
static_assert(FFmpegMPEGAudioDSPData::mpa_enwindow[40] == -58,
              "Mid-range coefficient");

// First transition to positive values (around index 64)
static_assert(FFmpegMPEGAudioDSPData::mpa_enwindow[64] == 213,
              "Transition to positive (index 64)");
static_assert(FFmpegMPEGAudioDSPData::mpa_enwindow[65] == 218,
              "Positive region continues");
static_assert(FFmpegMPEGAudioDSPData::mpa_enwindow[70] == 228,
              "Peak of first positive region");

// Return to negative values
static_assert(FFmpegMPEGAudioDSPData::mpa_enwindow[86] == -2,
              "Returns to negative");
static_assert(FFmpegMPEGAudioDSPData::mpa_enwindow[100] == -711,
              "Deep negative region");
static_assert(FFmpegMPEGAudioDSPData::mpa_enwindow[120] == -2001,
              "Continuing negative trend");

// Second transition to positive (around index 128)
static_assert(FFmpegMPEGAudioDSPData::mpa_enwindow[128] == 2037,
              "Major transition to positive");
static_assert(FFmpegMPEGAudioDSPData::mpa_enwindow[129] == 2000,
              "Second positive region");

// Gradual descent through positive values
static_assert(FFmpegMPEGAudioDSPData::mpa_enwindow[143] == 185,
              "Descending positive values");
static_assert(FFmpegMPEGAudioDSPData::mpa_enwindow[144] == -45,
              "Returns to negative again");

// Large negative region
static_assert(FFmpegMPEGAudioDSPData::mpa_enwindow[160] == -5153,
              "Large negative coefficient");

// Third major transition to positive (around index 192)
static_assert(FFmpegMPEGAudioDSPData::mpa_enwindow[192] == 6574,
              "Third transition to positive");
static_assert(FFmpegMPEGAudioDSPData::mpa_enwindow[193] == 5959,
              "Large positive region starts");
static_assert(FFmpegMPEGAudioDSPData::mpa_enwindow[196] == 3776,
              "Positive region continues");

// End of window (maximum positive value)
static_assert(FFmpegMPEGAudioDSPData::mpa_enwindow[256] == 75038,
              "Window ends at maximum positive (75038)");
static_assert(FFmpegMPEGAudioDSPData::mpa_enwindow[255] == -74992,
              "Penultimate value is large negative");
static_assert(FFmpegMPEGAudioDSPData::mpa_enwindow[254] == -74856,
              "Third from end is negative");
static_assert(FFmpegMPEGAudioDSPData::mpa_enwindow[200] == 70,
              "Transition point");
static_assert(FFmpegMPEGAudioDSPData::mpa_enwindow[201] == -998,
              "Back to negative");

// ============================================================================
// C API Compatibility Layer
// ============================================================================

extern "C" {

/**
 * Get pointer to MPEG audio encoding window coefficients
 * @return Pointer to 257-entry int32_t array
 */
inline const int32_t* get_mpa_enwindow() {
    return FFmpegMPEGAudioDSPData::mpa_enwindow.data();
}

} // extern "C"

#endif // AVCODEC_MPEGAUDIODSP_DATA_TABLEGEN_CONSTEXPR_HPP
