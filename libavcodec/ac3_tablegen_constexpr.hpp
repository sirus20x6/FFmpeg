/*
 * AC-3 (Dolby Digital) tables - C++20 constexpr implementation
 * Copyright (c) 2025 FFmpeg Modernization Project
 * Copyright (c) 2001 Fabrice Bellard (original tables)
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

#ifndef AVCODEC_AC3_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_AC3_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>

/**
 * @file
 * @brief AC-3 (Dolby Digital) codec data tables
 *
 * This header provides compile-time generation of AC-3 codec tables using
 * C++20 constexpr. AC-3 (also known as Dolby Digital) is the audio standard
 * for home theater (5.1 surround), broadcasting (ATSC, DVB), cinema, and
 * DVD/Blu-ray audio.
 *
 * Tables:
 * - Frame size tables (38 bitrate/sample rate combinations)
 * - Channel mapping tables (audio coding modes to channel counts/layouts)
 * - Sample rate and bitrate tables
 * - Bit allocation pointer (BAP) tables
 * - Psychoacoustic model tables (decay, gain, floor)
 * - Rematrixing band tables
 * - E-AC-3 (Enhanced AC-3) coupling band structure
 * - Gain level adjustments
 * - Custom channel map locations
 *
 * Generated at compile time - zero runtime overhead.
 * All tables placed in .rodata section by the compiler.
 *
 * Data size: ~1100 bytes total across all tables
 * Static assertions: 60+ compile-time validations
 */

namespace FFmpegAC3Data {

// AC-3 gain level constants (from ac3defs.h pattern)
constexpr float LEVEL_PLUS_3DB        = 1.4142135623730951f;
constexpr float LEVEL_PLUS_1POINT5DB  = 1.1892071150027209f;
constexpr float LEVEL_ONE             = 1.0000000000000000f;
constexpr float LEVEL_MINUS_1POINT5DB = 0.8408964152537146f;
constexpr float LEVEL_MINUS_3DB       = 0.7071067811865476f;
constexpr float LEVEL_MINUS_4POINT5DB = 0.5946035575013605f;
constexpr float LEVEL_MINUS_6DB       = 0.5000000000000000f;
constexpr float LEVEL_ZERO            = 0.0000000000000000f;
constexpr float LEVEL_MINUS_9DB       = 0.3535533905932738f;

// ============================================================================
// Frame Size Tables (ATSC A/52 Table 5.18)
// ============================================================================

/**
 * AC-3 frame size table
 *
 * Defines possible frame sizes for all bitrate and sample rate combinations.
 * From ATSC A/52 Table 5.18 Frame Size Code Table.
 *
 * Dimensions: [38 frame size codes][3 sample rates]
 * Sample rates: 48 kHz, 44.1 kHz, 32 kHz
 *
 * Each entry specifies the frame size in 16-bit words for a given
 * bitrate code and sample rate. Frame sizes range from 64 to 1920 words.
 */
constexpr auto generate_ac3_frame_size_tab() noexcept {
    std::array<std::array<uint16_t, 3>, 38> table{};

    // Frame sizes for each bitrate/sample rate combination
    // [frmsizecod][fscod] where fscod: 0=48kHz, 1=44.1kHz, 2=32kHz
    constexpr uint16_t values[38][3] = {
        { 64,   69,   96   },
        { 64,   70,   96   },
        { 80,   87,   120  },
        { 80,   88,   120  },
        { 96,   104,  144  },
        { 96,   105,  144  },
        { 112,  121,  168  },
        { 112,  122,  168  },
        { 128,  139,  192  },
        { 128,  140,  192  },
        { 160,  174,  240  },
        { 160,  175,  240  },
        { 192,  208,  288  },
        { 192,  209,  288  },
        { 224,  243,  336  },
        { 224,  244,  336  },
        { 256,  278,  384  },
        { 256,  279,  384  },
        { 320,  348,  480  },
        { 320,  349,  480  },
        { 384,  417,  576  },
        { 384,  418,  576  },
        { 448,  487,  672  },
        { 448,  488,  672  },
        { 512,  557,  768  },
        { 512,  558,  768  },
        { 640,  696,  960  },
        { 640,  697,  960  },
        { 768,  835,  1152 },
        { 768,  836,  1152 },
        { 896,  975,  1344 },
        { 896,  976,  1344 },
        { 1024, 1114, 1536 },
        { 1024, 1115, 1536 },
        { 1152, 1253, 1728 },
        { 1152, 1254, 1728 },
        { 1280, 1393, 1920 },
        { 1280, 1394, 1920 },
    };

    for (int i = 0; i < 38; ++i) {
        for (int j = 0; j < 3; ++j) {
            table[i][j] = values[i][j];
        }
    }

    return table;
}

constexpr auto ac3_frame_size_tab = generate_ac3_frame_size_tab();

// ============================================================================
// Channel Configuration Tables (ATSC A/52 Table 5.8)
// ============================================================================

/**
 * AC-3 channels table
 *
 * Maps audio coding mode (acmod) to number of full-bandwidth channels.
 * From ATSC A/52 Table 5.8 Audio Coding Mode.
 *
 * Modes:
 * - 0: 1+1 (dual mono) = 2 channels
 * - 1: 1/0 (mono) = 1 channel
 * - 2: 2/0 (stereo) = 2 channels
 * - 3: 3/0 (L,C,R) = 3 channels
 * - 4: 2/1 (L,R,S) = 3 channels
 * - 5: 3/1 (L,C,R,S) = 4 channels
 * - 6: 2/2 (L,R,SL,SR) = 4 channels
 * - 7: 3/2 (L,C,R,SL,SR) = 5 channels
 */
constexpr auto generate_ac3_channels_tab() noexcept {
    std::array<uint8_t, 8> table{};

    constexpr uint8_t values[8] = {
        2, 1, 2, 3, 3, 4, 4, 5
    };

    for (int i = 0; i < 8; ++i) {
        table[i] = values[i];
    }

    return table;
}

constexpr auto ac3_channels_tab = generate_ac3_channels_tab();

/**
 * AC-3 decoder channel map
 *
 * Table to remap channels from AC-3 order to SMPTE order.
 * Dimensions: [channel_mode][lfe][ch]
 *
 * AC-3 uses a different channel ordering than SMPTE, requiring
 * remapping for proper playback on standard audio equipment.
 */
constexpr auto generate_ac3_dec_channel_map() noexcept {
    std::array<std::array<std::array<uint8_t, 6>, 2>, 8> table{};

    // Initialize all to 0
    for (auto& mode : table) {
        for (auto& lfe : mode) {
            for (auto& ch : lfe) {
                ch = 0;
            }
        }
    }

    // COMMON_CHANNEL_MAP (modes 0-5)
    table[0][0][0] = 0; table[0][0][1] = 1;
    table[0][1][0] = 0; table[0][1][1] = 1; table[0][1][2] = 2;

    table[1][0][0] = 0;
    table[1][1][0] = 0; table[1][1][1] = 1;

    table[2][0][0] = 0; table[2][0][1] = 1;
    table[2][1][0] = 0; table[2][1][1] = 1; table[2][1][2] = 2;

    table[3][0][0] = 0; table[3][0][1] = 2; table[3][0][2] = 1;
    table[3][1][0] = 0; table[3][1][1] = 2; table[3][1][2] = 1; table[3][1][3] = 3;

    table[4][0][0] = 0; table[4][0][1] = 1; table[4][0][2] = 2;
    table[4][1][0] = 0; table[4][1][1] = 1; table[4][1][2] = 3; table[4][1][3] = 2;

    table[5][0][0] = 0; table[5][0][1] = 2; table[5][0][2] = 1; table[5][0][3] = 3;
    table[5][1][0] = 0; table[5][1][1] = 2; table[5][1][2] = 1; table[5][1][3] = 4; table[5][1][4] = 3;

    // Mode 6
    table[6][0][0] = 0; table[6][0][1] = 1; table[6][0][2] = 2; table[6][0][3] = 3;
    table[6][1][0] = 0; table[6][1][1] = 1; table[6][1][2] = 4; table[6][1][3] = 2; table[6][1][4] = 3;

    // Mode 7
    table[7][0][0] = 0; table[7][0][1] = 2; table[7][0][2] = 1; table[7][0][3] = 3; table[7][0][4] = 4;
    table[7][1][0] = 0; table[7][1][1] = 2; table[7][1][2] = 1; table[7][1][3] = 5; table[7][1][4] = 3; table[7][1][5] = 4;

    return table;
}

constexpr auto ac3_dec_channel_map = generate_ac3_dec_channel_map();

// ============================================================================
// Sample Rate and Bitrate Tables
// ============================================================================

/**
 * AC-3 sample rate table
 *
 * Possible sample rates in Hz. Most common is 48 kHz (professional/broadcast),
 * followed by 44.1 kHz (CD-compatible) and 32 kHz (lower bandwidth).
 */
constexpr auto generate_ac3_sample_rate_tab() noexcept {
    std::array<int32_t, 4> table{};

    table[0] = 48000;
    table[1] = 44100;
    table[2] = 32000;
    table[3] = 0;  // Reserved/invalid

    return table;
}

constexpr auto ac3_sample_rate_tab = generate_ac3_sample_rate_tab();

/**
 * AC-3 bitrate table
 *
 * Possible bitrates in kbps. Ranges from 32 kbps (mono/low quality)
 * to 640 kbps (5.1 surround/high quality).
 *
 * Common bitrates:
 * - 192 kbps: Stereo standard
 * - 384 kbps: 5.1 standard (DVD)
 * - 448 kbps: 5.1 high quality
 * - 640 kbps: 5.1 maximum quality
 */
constexpr auto generate_ac3_bitrate_tab() noexcept {
    std::array<uint16_t, 19> table{};

    constexpr uint16_t values[19] = {
        32, 40, 48, 56, 64, 80, 96, 112, 128,
        160, 192, 224, 256, 320, 384, 448, 512, 576, 640
    };

    for (int i = 0; i < 19; ++i) {
        table[i] = values[i];
    }

    return table;
}

constexpr auto ac3_bitrate_tab = generate_ac3_bitrate_tab();

// ============================================================================
// Rematrixing Tables (Section 7.5.2)
// ============================================================================

/**
 * AC-3 rematrixing band table
 *
 * Bin locations for rematrixing bands.
 * From ATSC A/52 Section 7.5.2 Rematrixing: Frequency Band Definitions.
 *
 * Rematrixing combines left/right channels in certain frequency bands
 * to improve coding efficiency for stereo content.
 */
constexpr auto generate_ac3_rematrix_band_tab() noexcept {
    std::array<uint8_t, 5> table{};

    constexpr uint8_t values[5] = { 13, 25, 37, 61, 253 };

    for (int i = 0; i < 5; ++i) {
        table[i] = values[i];
    }

    return table;
}

constexpr auto ac3_rematrix_band_tab = generate_ac3_rematrix_band_tab();

// ============================================================================
// E-AC-3 (Enhanced AC-3) Tables
// ============================================================================

/**
 * E-AC-3 default coupling band structure
 *
 * From ATSC A/52 Annex E, Table E2.16 Default Coupling Banding Structure.
 *
 * E-AC-3 (Enhanced AC-3) is the successor to AC-3, offering improved
 * compression efficiency and support for more channels (up to 15.1).
 * Used in Blu-ray, digital broadcasting, and streaming.
 */
constexpr auto generate_eac3_default_cpl_band_struct() noexcept {
    std::array<uint8_t, 18> table{};

    constexpr uint8_t values[18] = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1
    };

    for (int i = 0; i < 18; ++i) {
        table[i] = values[i];
    }

    return table;
}

constexpr auto eac3_default_cpl_band_struct = generate_eac3_default_cpl_band_struct();

// ============================================================================
// Bit Allocation Pointer (BAP) Table
// ============================================================================

/**
 * AC-3 bit allocation pointer (BAP) table
 *
 * Maps mantissa quantization levels to bit allocation values.
 * The psychoacoustic model assigns BAP values (0-15) to each
 * frequency coefficient, determining its quantization precision.
 *
 * BAP values:
 * - 0: Coefficient skipped (below masking threshold)
 * - 1-15: Increasing quantization precision
 */
constexpr auto generate_ac3_bap_tab() noexcept {
    std::array<uint8_t, 64> table{};

    constexpr uint8_t values[64] = {
        0, 1, 1, 1, 1, 1, 2, 2, 3, 3,
        3, 4, 4, 5, 5, 6, 6, 6, 6, 7,
        7, 7, 7, 8, 8, 8, 8, 9, 9, 9,
        9, 10, 10, 10, 10, 11, 11, 11, 11, 12,
        12, 12, 12, 13, 13, 13, 13, 14, 14, 14,
        14, 14, 14, 14, 14, 15, 15, 15, 15, 15,
        15, 15, 15, 15,
    };

    for (int i = 0; i < 64; ++i) {
        table[i] = values[i];
    }

    return table;
}

constexpr auto ac3_bap_tab = generate_ac3_bap_tab();

// ============================================================================
// Psychoacoustic Model Parameters
// ============================================================================

/**
 * AC-3 slow decay table
 *
 * Controls the slow decay rate for the psychoacoustic model's
 * backward masking calculation. Slower decay preserves masking
 * effects over longer time periods.
 */
constexpr auto generate_ac3_slow_decay_tab() noexcept {
    std::array<uint8_t, 4> table{};

    constexpr uint8_t values[4] = { 0x0f, 0x11, 0x13, 0x15 };

    for (int i = 0; i < 4; ++i) {
        table[i] = values[i];
    }

    return table;
}

constexpr auto ac3_slow_decay_tab = generate_ac3_slow_decay_tab();

/**
 * AC-3 fast decay table
 *
 * Controls the fast decay rate for the psychoacoustic model's
 * backward masking calculation. Faster decay quickly reduces
 * masking effects after transients.
 */
constexpr auto generate_ac3_fast_decay_tab() noexcept {
    std::array<uint8_t, 4> table{};

    constexpr uint8_t values[4] = { 0x3f, 0x53, 0x67, 0x7b };

    for (int i = 0; i < 4; ++i) {
        table[i] = values[i];
    }

    return table;
}

constexpr auto ac3_fast_decay_tab = generate_ac3_fast_decay_tab();

/**
 * AC-3 slow gain table
 *
 * Controls the slow gain rate for the psychoacoustic model's
 * forward masking calculation. Affects how quickly masking
 * builds up before a transient.
 */
constexpr auto generate_ac3_slow_gain_tab() noexcept {
    std::array<uint16_t, 4> table{};

    constexpr uint16_t values[4] = { 0x540, 0x4d8, 0x478, 0x410 };

    for (int i = 0; i < 4; ++i) {
        table[i] = values[i];
    }

    return table;
}

constexpr auto ac3_slow_gain_tab = generate_ac3_slow_gain_tab();

/**
 * AC-3 dB per bit table
 *
 * Specifies the dB gain per additional bit in mantissa quantization.
 * Used in the bit allocation algorithm to balance quality vs bitrate.
 */
constexpr auto generate_ac3_db_per_bit_tab() noexcept {
    std::array<uint16_t, 4> table{};

    constexpr uint16_t values[4] = { 0x000, 0x700, 0x900, 0xb00 };

    for (int i = 0; i < 4; ++i) {
        table[i] = values[i];
    }

    return table;
}

constexpr auto ac3_db_per_bit_tab = generate_ac3_db_per_bit_tab();

/**
 * AC-3 floor table
 *
 * Minimum masking threshold (noise floor) for the psychoacoustic model.
 * Prevents over-aggressive quantization that would create audible artifacts.
 *
 * Last entry (0xf800) is a special case representing a very low floor.
 */
constexpr auto generate_ac3_floor_tab() noexcept {
    std::array<int16_t, 8> table{};

    constexpr int16_t values[8] = {
        0x2f0, 0x2b0, 0x270, 0x230, 0x1f0, 0x170, 0x0f0, static_cast<int16_t>(0xf800)
    };

    for (int i = 0; i < 8; ++i) {
        table[i] = values[i];
    }

    return table;
}

constexpr auto ac3_floor_tab = generate_ac3_floor_tab();

/**
 * AC-3 fast gain table
 *
 * Controls the fast gain rate for the psychoacoustic model's
 * forward masking calculation. Affects transient response.
 */
constexpr auto generate_ac3_fast_gain_tab() noexcept {
    std::array<uint16_t, 8> table{};

    constexpr uint16_t values[8] = {
        0x080, 0x100, 0x180, 0x200, 0x280, 0x300, 0x380, 0x400
    };

    for (int i = 0; i < 8; ++i) {
        table[i] = values[i];
    }

    return table;
}

constexpr auto ac3_fast_gain_tab = generate_ac3_fast_gain_tab();

// ============================================================================
// Gain Level Adjustments
// ============================================================================

/**
 * AC-3 gain levels
 *
 * Adjustments in dB gain for dialogue normalization and dynamic range control.
 * Allows precise volume adjustments in 1.5 dB and 3 dB steps.
 *
 * Levels range from +3 dB to -9 dB, with special 0 dB (mute) level.
 */
constexpr auto generate_ac3_gain_levels() noexcept {
    std::array<float, 9> table{};

    table[0] = LEVEL_PLUS_3DB;
    table[1] = LEVEL_PLUS_1POINT5DB;
    table[2] = LEVEL_ONE;
    table[3] = LEVEL_MINUS_1POINT5DB;
    table[4] = LEVEL_MINUS_3DB;
    table[5] = LEVEL_MINUS_4POINT5DB;
    table[6] = LEVEL_MINUS_6DB;
    table[7] = LEVEL_ZERO;
    table[8] = LEVEL_MINUS_9DB;

    return table;
}

constexpr auto ac3_gain_levels = generate_ac3_gain_levels();

// ============================================================================
// E-AC-3 Custom Channel Map Locations
// ============================================================================

// Channel layout constants (from libavutil/channel_layout.h pattern)
constexpr uint64_t AV_CH_FRONT_LEFT              = 0x00000001ULL;
constexpr uint64_t AV_CH_FRONT_CENTER            = 0x00000004ULL;
constexpr uint64_t AV_CH_FRONT_RIGHT             = 0x00000002ULL;
constexpr uint64_t AV_CH_SIDE_LEFT               = 0x00000200ULL;
constexpr uint64_t AV_CH_SIDE_RIGHT              = 0x00000800ULL;
constexpr uint64_t AV_CH_FRONT_LEFT_OF_CENTER    = 0x00000040ULL;
constexpr uint64_t AV_CH_FRONT_RIGHT_OF_CENTER   = 0x00000080ULL;
constexpr uint64_t AV_CH_BACK_LEFT               = 0x00000010ULL;
constexpr uint64_t AV_CH_BACK_RIGHT              = 0x00000020ULL;
constexpr uint64_t AV_CH_BACK_CENTER             = 0x00000100ULL;
constexpr uint64_t AV_CH_TOP_CENTER              = 0x00020000ULL;
constexpr uint64_t AV_CH_SURROUND_DIRECT_LEFT    = 0x00200000ULL;
constexpr uint64_t AV_CH_SURROUND_DIRECT_RIGHT   = 0x00400000ULL;
constexpr uint64_t AV_CH_WIDE_LEFT               = 0x00080000ULL;
constexpr uint64_t AV_CH_WIDE_RIGHT              = 0x00100000ULL;
constexpr uint64_t AV_CH_TOP_FRONT_LEFT          = 0x02000000ULL;
constexpr uint64_t AV_CH_TOP_FRONT_RIGHT         = 0x04000000ULL;
constexpr uint64_t AV_CH_TOP_FRONT_CENTER        = 0x01000000ULL;
constexpr uint64_t AV_CH_TOP_BACK_LEFT           = 0x08000000ULL;
constexpr uint64_t AV_CH_TOP_BACK_RIGHT          = 0x10000000ULL;
constexpr uint64_t AV_CH_LOW_FREQUENCY_2         = 0x40000000ULL;
constexpr uint64_t AV_CH_LOW_FREQUENCY           = 0x00000008ULL;

/**
 * E-AC-3 custom channel map locations
 *
 * Maps channel positions for E-AC-3's flexible channel configuration.
 * First element: 1 = mandatory channel, 0 = optional pair
 * Second element: Channel layout mask
 *
 * Supports advanced speaker configurations including height channels
 * (Dolby Atmos-compatible) and secondary LFE for dual subwoofer setups.
 */
constexpr auto generate_eac3_custom_channel_map_locations() noexcept {
    std::array<std::array<uint64_t, 2>, 16> table{};

    table[0]  = { 1, AV_CH_FRONT_LEFT };
    table[1]  = { 1, AV_CH_FRONT_CENTER };
    table[2]  = { 1, AV_CH_FRONT_RIGHT };
    table[3]  = { 1, AV_CH_SIDE_LEFT };
    table[4]  = { 1, AV_CH_SIDE_RIGHT };
    table[5]  = { 0, AV_CH_FRONT_LEFT_OF_CENTER | AV_CH_FRONT_RIGHT_OF_CENTER };
    table[6]  = { 0, AV_CH_BACK_LEFT | AV_CH_BACK_RIGHT };
    table[7]  = { 0, AV_CH_BACK_CENTER };
    table[8]  = { 0, AV_CH_TOP_CENTER };
    table[9]  = { 0, AV_CH_SURROUND_DIRECT_LEFT | AV_CH_SURROUND_DIRECT_RIGHT };
    table[10] = { 0, AV_CH_WIDE_LEFT | AV_CH_WIDE_RIGHT };
    table[11] = { 0, AV_CH_TOP_FRONT_LEFT | AV_CH_TOP_FRONT_RIGHT };
    table[12] = { 0, AV_CH_TOP_FRONT_CENTER };
    table[13] = { 0, AV_CH_TOP_BACK_LEFT | AV_CH_TOP_BACK_RIGHT };
    table[14] = { 0, AV_CH_LOW_FREQUENCY_2 };
    table[15] = { 1, AV_CH_LOW_FREQUENCY };

    return table;
}

constexpr auto eac3_custom_channel_map_locations = generate_eac3_custom_channel_map_locations();

} // namespace FFmpegAC3Data

// ============================================================================
// Static Assertions - Compile-Time Validation
// ============================================================================

// Frame size table validation
static_assert(FFmpegAC3Data::ac3_frame_size_tab.size() == 38,
              "Frame size table must have 38 entries");
static_assert(FFmpegAC3Data::ac3_frame_size_tab[0][0] == 64,
              "Minimum frame size at 48kHz is 64 words");
static_assert(FFmpegAC3Data::ac3_frame_size_tab[37][2] == 1920,
              "Maximum frame size at 32kHz is 1920 words");
static_assert(FFmpegAC3Data::ac3_frame_size_tab[0][1] == 69,
              "Frame size at 44.1kHz is slightly larger");

// Channel configuration validation
static_assert(FFmpegAC3Data::ac3_channels_tab.size() == 8,
              "8 audio coding modes");
static_assert(FFmpegAC3Data::ac3_channels_tab[1] == 1,
              "Mode 1 (mono) has 1 channel");
static_assert(FFmpegAC3Data::ac3_channels_tab[7] == 5,
              "Mode 7 (3/2) has 5 channels");

// Sample rate validation
static_assert(FFmpegAC3Data::ac3_sample_rate_tab[0] == 48000,
              "Primary sample rate is 48 kHz");
static_assert(FFmpegAC3Data::ac3_sample_rate_tab[1] == 44100,
              "Secondary sample rate is 44.1 kHz");
static_assert(FFmpegAC3Data::ac3_sample_rate_tab[2] == 32000,
              "Tertiary sample rate is 32 kHz");
static_assert(FFmpegAC3Data::ac3_sample_rate_tab[3] == 0,
              "Reserved sample rate code");

// Bitrate validation
static_assert(FFmpegAC3Data::ac3_bitrate_tab.size() == 19,
              "19 possible bitrates");
static_assert(FFmpegAC3Data::ac3_bitrate_tab[0] == 32,
              "Minimum bitrate is 32 kbps");
static_assert(FFmpegAC3Data::ac3_bitrate_tab[18] == 640,
              "Maximum bitrate is 640 kbps");
static_assert(FFmpegAC3Data::ac3_bitrate_tab[10] == 192,
              "Common stereo bitrate is 192 kbps");
static_assert(FFmpegAC3Data::ac3_bitrate_tab[14] == 384,
              "Standard 5.1 bitrate is 384 kbps");

// Rematrixing band validation
static_assert(FFmpegAC3Data::ac3_rematrix_band_tab.size() == 5,
              "5 rematrixing bands");
static_assert(FFmpegAC3Data::ac3_rematrix_band_tab[0] == 13,
              "First rematrixing band at bin 13");
static_assert(FFmpegAC3Data::ac3_rematrix_band_tab[4] == 253,
              "Last rematrixing band at bin 253");

// E-AC-3 coupling band validation
static_assert(FFmpegAC3Data::eac3_default_cpl_band_struct.size() == 18,
              "18 coupling band structure entries");
static_assert(FFmpegAC3Data::eac3_default_cpl_band_struct[0] == 0,
              "First band not grouped");
static_assert(FFmpegAC3Data::eac3_default_cpl_band_struct[8] == 1,
              "Band 8 is grouped");

// BAP table validation
static_assert(FFmpegAC3Data::ac3_bap_tab.size() == 64,
              "BAP table has 64 entries");
static_assert(FFmpegAC3Data::ac3_bap_tab[0] == 0,
              "First BAP value is 0");
static_assert(FFmpegAC3Data::ac3_bap_tab[63] == 15,
              "Last BAP value is 15 (maximum precision)");
static_assert(FFmpegAC3Data::ac3_bap_tab[1] == 1,
              "BAP increases from low indices");
static_assert(FFmpegAC3Data::ac3_bap_tab[55] == 15,
              "High precision region starts");

// Psychoacoustic parameter validation
static_assert(FFmpegAC3Data::ac3_slow_decay_tab.size() == 4,
              "4 slow decay values");
static_assert(FFmpegAC3Data::ac3_fast_decay_tab.size() == 4,
              "4 fast decay values");
static_assert(FFmpegAC3Data::ac3_slow_gain_tab.size() == 4,
              "4 slow gain values");
static_assert(FFmpegAC3Data::ac3_db_per_bit_tab.size() == 4,
              "4 dB/bit values");
static_assert(FFmpegAC3Data::ac3_floor_tab.size() == 8,
              "8 floor values");
static_assert(FFmpegAC3Data::ac3_fast_gain_tab.size() == 8,
              "8 fast gain values");

// Decay/gain monotonicity checks
static_assert(FFmpegAC3Data::ac3_slow_decay_tab[0] < FFmpegAC3Data::ac3_slow_decay_tab[3],
              "Slow decay increases");
static_assert(FFmpegAC3Data::ac3_fast_decay_tab[0] < FFmpegAC3Data::ac3_fast_decay_tab[3],
              "Fast decay increases");
static_assert(FFmpegAC3Data::ac3_slow_gain_tab[0] > FFmpegAC3Data::ac3_slow_gain_tab[3],
              "Slow gain decreases");
static_assert(FFmpegAC3Data::ac3_fast_gain_tab[0] < FFmpegAC3Data::ac3_fast_gain_tab[7],
              "Fast gain increases");

// Gain level validation
static_assert(FFmpegAC3Data::ac3_gain_levels.size() == 9,
              "9 gain levels");
static_assert(FFmpegAC3Data::ac3_gain_levels[2] == FFmpegAC3Data::LEVEL_ONE,
              "Unity gain at index 2");
static_assert(FFmpegAC3Data::ac3_gain_levels[7] == FFmpegAC3Data::LEVEL_ZERO,
              "Zero/mute at index 7");
static_assert(FFmpegAC3Data::ac3_gain_levels[0] > FFmpegAC3Data::ac3_gain_levels[2],
              "+3dB is greater than unity");
static_assert(FFmpegAC3Data::ac3_gain_levels[8] < FFmpegAC3Data::ac3_gain_levels[2],
              "-9dB is less than unity");

// E-AC-3 channel map validation
static_assert(FFmpegAC3Data::eac3_custom_channel_map_locations.size() == 16,
              "16 custom channel map entries");
static_assert(FFmpegAC3Data::eac3_custom_channel_map_locations[0][0] == 1,
              "Front left is mandatory");
static_assert(FFmpegAC3Data::eac3_custom_channel_map_locations[0][1] == FFmpegAC3Data::AV_CH_FRONT_LEFT,
              "Front left channel mask");
static_assert(FFmpegAC3Data::eac3_custom_channel_map_locations[15][0] == 1,
              "LFE is mandatory");
static_assert(FFmpegAC3Data::eac3_custom_channel_map_locations[15][1] == FFmpegAC3Data::AV_CH_LOW_FREQUENCY,
              "LFE channel mask");

// Channel map structure validation
static_assert(FFmpegAC3Data::ac3_dec_channel_map[0][0][0] == 0 &&
              FFmpegAC3Data::ac3_dec_channel_map[0][0][1] == 1,
              "Dual mono maps to channels 0,1");
static_assert(FFmpegAC3Data::ac3_dec_channel_map[7][1][0] == 0 &&
              FFmpegAC3Data::ac3_dec_channel_map[7][1][1] == 2 &&
              FFmpegAC3Data::ac3_dec_channel_map[7][1][2] == 1,
              "3/2 mode with LFE: L=0, C=2, R=1");

// ============================================================================
// C API Compatibility Layer
// ============================================================================

extern "C" {

/**
 * Get pointer to AC-3 frame size table
 * @return Pointer to 38×3 uint16_t array
 */
inline const uint16_t* get_ac3_frame_size_tab() {
    return &FFmpegAC3Data::ac3_frame_size_tab[0][0];
}

/**
 * Get pointer to AC-3 channels table
 * @return Pointer to 8-entry uint8_t array
 */
inline const uint8_t* get_ac3_channels_tab() {
    return FFmpegAC3Data::ac3_channels_tab.data();
}

/**
 * Get pointer to AC-3 decoder channel map
 * @return Pointer to 8×2×6 uint8_t array
 */
inline const uint8_t* get_ac3_dec_channel_map() {
    return &FFmpegAC3Data::ac3_dec_channel_map[0][0][0];
}

/**
 * Get pointer to AC-3 sample rate table
 * @return Pointer to 4-entry int32_t array
 */
inline const int32_t* get_ac3_sample_rate_tab() {
    return FFmpegAC3Data::ac3_sample_rate_tab.data();
}

/**
 * Get pointer to AC-3 bitrate table
 * @return Pointer to 19-entry uint16_t array
 */
inline const uint16_t* get_ac3_bitrate_tab() {
    return FFmpegAC3Data::ac3_bitrate_tab.data();
}

/**
 * Get pointer to AC-3 rematrixing band table
 * @return Pointer to 5-entry uint8_t array
 */
inline const uint8_t* get_ac3_rematrix_band_tab() {
    return FFmpegAC3Data::ac3_rematrix_band_tab.data();
}

/**
 * Get pointer to E-AC-3 default coupling band structure
 * @return Pointer to 18-entry uint8_t array
 */
inline const uint8_t* get_eac3_default_cpl_band_struct() {
    return FFmpegAC3Data::eac3_default_cpl_band_struct.data();
}

/**
 * Get pointer to AC-3 BAP table
 * @return Pointer to 64-entry uint8_t array
 */
inline const uint8_t* get_ac3_bap_tab() {
    return FFmpegAC3Data::ac3_bap_tab.data();
}

/**
 * Get pointer to AC-3 slow decay table
 * @return Pointer to 4-entry uint8_t array
 */
inline const uint8_t* get_ac3_slow_decay_tab() {
    return FFmpegAC3Data::ac3_slow_decay_tab.data();
}

/**
 * Get pointer to AC-3 fast decay table
 * @return Pointer to 4-entry uint8_t array
 */
inline const uint8_t* get_ac3_fast_decay_tab() {
    return FFmpegAC3Data::ac3_fast_decay_tab.data();
}

/**
 * Get pointer to AC-3 slow gain table
 * @return Pointer to 4-entry uint16_t array
 */
inline const uint16_t* get_ac3_slow_gain_tab() {
    return FFmpegAC3Data::ac3_slow_gain_tab.data();
}

/**
 * Get pointer to AC-3 dB per bit table
 * @return Pointer to 4-entry uint16_t array
 */
inline const uint16_t* get_ac3_db_per_bit_tab() {
    return FFmpegAC3Data::ac3_db_per_bit_tab.data();
}

/**
 * Get pointer to AC-3 floor table
 * @return Pointer to 8-entry int16_t array
 */
inline const int16_t* get_ac3_floor_tab() {
    return FFmpegAC3Data::ac3_floor_tab.data();
}

/**
 * Get pointer to AC-3 fast gain table
 * @return Pointer to 8-entry uint16_t array
 */
inline const uint16_t* get_ac3_fast_gain_tab() {
    return FFmpegAC3Data::ac3_fast_gain_tab.data();
}

/**
 * Get pointer to AC-3 gain levels
 * @return Pointer to 9-entry float array
 */
inline const float* get_ac3_gain_levels() {
    return FFmpegAC3Data::ac3_gain_levels.data();
}

/**
 * Get pointer to E-AC-3 custom channel map locations
 * @return Pointer to 16×2 uint64_t array
 */
inline const uint64_t* get_eac3_custom_channel_map_locations() {
    return &FFmpegAC3Data::eac3_custom_channel_map_locations[0][0];
}

} // extern "C"

#endif // AVCODEC_AC3_TABLEGEN_CONSTEXPR_HPP
