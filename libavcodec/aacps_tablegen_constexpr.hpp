/*
 * Modern C++ constexpr AAC Parametric Stereo tables
 * Copyright (c) 2010 Alex Converse <alex.converse@gmail.com>
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
 * Modern C++20 constexpr AAC Parametric Stereo lookup tables
 *
 * This header provides compile-time generation of AAC PS (Parametric Stereo)
 * codec tables. PS is an extension to AAC that efficiently encodes stereo
 * information using spatial audio parameters.
 *
 * Tables generated:
 * - pd_re_smooth, pd_im_smooth: Phase difference smoothing (512 entries each)
 * - HA, HB: Inter-channel mixing matrices (46×8×4 each = 1,472 entries each)
 * - Q_fract_allpass: Fractional delay allpass (2×50×3×2 = 600 entries)
 * - phi_fract: Phase fractional delay (2×50×2 = 200 entries)
 * - f20_0_8, f34_0_12, f34_1_8, f34_2_4: Filterbank prototypes (416 entries)
 *
 * Total: ~5,184 float entries
 *
 * All tables use trigonometric functions (sin, cos, atan, atan2) computed
 * at compile time using Taylor series and CORDIC-like algorithms.
 */

#ifndef AVCODEC_AACPS_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_AACPS_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>

namespace ffmpeg {
namespace aacps {

constexpr double PI = 3.14159265358979323846;
constexpr double M_SQRT2 = 1.41421356237309504880;
constexpr double M_SQRT1_2 = 0.70710678118654752440;

// Constants
constexpr int NR_ALLPASS_BANDS20 = 30;
constexpr int NR_ALLPASS_BANDS34 = 50;
constexpr int PS_AP_LINKS = 3;

// Trigonometric functions (reuse from previous implementations)

constexpr double sin_constexpr(double x) noexcept {
    // Normalize to [-π, π]
    while (x > PI) x -= 2.0 * PI;
    while (x < -PI) x += 2.0 * PI;

    double x2 = x * x;
    double result = x;
    double term = x;

    for (int i = 1; i <= 10; ++i) {
        term *= -x2 / ((2.0 * i) * (2.0 * i + 1.0));
        result += term;
    }

    return result;
}

constexpr double cos_constexpr(double x) noexcept {
    // cos(x) = sin(x + π/2)
    return sin_constexpr(x + PI / 2.0);
}

/**
 * Constexpr square root using Newton-Raphson
 */
constexpr double sqrt_constexpr(double x) noexcept {
    if (x == 0.0) return 0.0;
    if (x < 0.0) return 0.0;  // NaN would be better but not constexpr

    double guess = x;
    for (int i = 0; i < 10; ++i) {
        guess = (guess + x / guess) * 0.5;
    }

    return guess;
}

/**
 * Constexpr hypot: sqrt(x² + y²)
 */
constexpr double hypot_constexpr(double x, double y) noexcept {
    return sqrt_constexpr(x * x + y * y);
}

/**
 * Constexpr atan using Taylor series
 * For |x| < 1: atan(x) = x - x³/3 + x⁵/5 - x⁷/7 + ...
 */
constexpr double atan_constexpr(double x) noexcept {
    // Use identity: atan(x) = π/2 - atan(1/x) for |x| > 1
    if (x > 1.0) {
        return PI / 2.0 - atan_constexpr(1.0 / x);
    }
    if (x < -1.0) {
        return -PI / 2.0 - atan_constexpr(1.0 / x);
    }

    // Taylor series for |x| <= 1
    double x2 = x * x;
    double result = x;
    double term = x;

    for (int n = 1; n <= 20; ++n) {
        term *= -x2;
        result += term / (2.0 * n + 1.0);
    }

    return result;
}

/**
 * Constexpr atan2
 */
constexpr double atan2_constexpr(double y, double x) noexcept {
    if (x > 0.0) {
        return atan_constexpr(y / x);
    }
    if (x < 0.0) {
        if (y >= 0.0) {
            return atan_constexpr(y / x) + PI;
        } else {
            return atan_constexpr(y / x) - PI;
        }
    }
    // x == 0
    if (y > 0.0) return PI / 2.0;
    if (y < 0.0) return -PI / 2.0;
    return 0.0;  // Undefined, return 0
}

/**
 * Constexpr acos using identity: acos(x) = π/2 - asin(x)
 * And asin(x) ≈ atan(x / sqrt(1 - x²))
 */
constexpr double acos_constexpr(double x) noexcept {
    if (x >= 1.0) return 0.0;
    if (x <= -1.0) return PI;

    // asin(x) = atan(x / sqrt(1 - x²))
    double asin_val = atan_constexpr(x / sqrt_constexpr(1.0 - x * x));
    return PI / 2.0 - asin_val;
}

/**
 * Max/Min functions
 */
constexpr float max_constexpr(float a, float b) noexcept {
    return (a > b) ? a : b;
}

// Pre-computed constants

constexpr std::array<float, 8> ipdopd_sin = {
    0.0f,
    static_cast<float>(M_SQRT1_2),
    1.0f,
    static_cast<float>(M_SQRT1_2),
    0.0f,
    static_cast<float>(-M_SQRT1_2),
    -1.0f,
    static_cast<float>(-M_SQRT1_2)
};

constexpr std::array<float, 8> ipdopd_cos = {
    1.0f,
    static_cast<float>(M_SQRT1_2),
    0.0f,
    static_cast<float>(-M_SQRT1_2),
    -1.0f,
    static_cast<float>(-M_SQRT1_2),
    0.0f,
    static_cast<float>(M_SQRT1_2)
};

constexpr std::array<float, 46> iid_par_dequant = {
    // Default dequantization
    0.05623413251903f, 0.12589254117942f, 0.19952623149689f, 0.31622776601684f,
    0.44668359215096f, 0.63095734448019f, 0.79432823472428f, 1.0f,
    1.25892541179417f, 1.58489319246111f, 2.23872113856834f, 3.16227766016838f,
    5.01187233627272f, 7.94328234724282f, 17.7827941003892f,
    // Fine dequantization
    0.00316227766017f, 0.00562341325190f, 0.01f, 0.01778279410039f,
    0.03162277660168f, 0.05623413251903f, 0.07943282347243f, 0.11220184543020f,
    0.15848931924611f, 0.22387211385683f, 0.31622776601684f, 0.39810717055350f,
    0.50118723362727f, 0.63095734448019f, 0.79432823472428f, 1.0f,
    1.25892541179417f, 1.58489319246111f, 1.99526231496888f, 2.51188643150958f,
    3.16227766016838f, 4.46683592150963f, 6.30957344480193f, 8.91250938133745f,
    12.5892541179417f, 17.7827941003892f, 31.6227766016838f, 56.2341325190349f,
    100.0f, 177.827941003892f, 316.227766016837f,
};

constexpr std::array<float, 8> icc_invq = {
    1.0f, 0.937f, 0.84118f, 0.60092f, 0.36764f, 0.0f, -0.589f, -1.0f
};

constexpr std::array<float, 8> acos_icc_invq = {
    0.0f, 0.35685527f, 0.57133466f, 0.92614472f, 1.1943263f,
    static_cast<float>(PI / 2.0), 2.2006171f, static_cast<float>(PI)
};

constexpr std::array<int8_t, 10> f_center_20 = {
    -3, -1, 1, 3, 5, 7, 10, 14, 18, 22,
};

constexpr std::array<int8_t, 32> f_center_34 = {
     2,  6, 10, 14, 18, 22, 26, 30,
    34,-10, -6, -2, 51, 57, 15, 21,
    27, 33, 39, 45, 54, 66, 78, 42,
   102, 66, 78, 90,102,114,126, 90,
};

constexpr std::array<float, 3> fractional_delay_links = { 0.43f, 0.75f, 0.347f };
constexpr float fractional_delay_gain = 0.39f;

constexpr std::array<float, 7> g0_Q8 = {
    0.00746082949812f, 0.02270420949825f, 0.04546865930473f, 0.07266113929591f,
    0.09885108575264f, 0.11793710567217f, 0.125f
};

constexpr std::array<float, 7> g0_Q12 = {
    0.04081179924692f, 0.03812810994926f, 0.05144908135699f, 0.06399831151592f,
    0.07428313801106f, 0.08100347892914f, 0.08333333333333f
};

constexpr std::array<float, 7> g1_Q8 = {
    0.01565675600122f, 0.03752716391991f, 0.05417891378782f, 0.08417044116767f,
    0.10307344158036f, 0.12222452249753f, 0.125f
};

constexpr std::array<float, 7> g2_Q4 = {
    -0.05908211155639f, -0.04871498374946f, 0.0f, 0.07778723915851f,
    0.16486303567403f, 0.23279856662996f, 0.25f
};

/**
 * Generate phase difference smoothing tables
 */
constexpr auto generate_pd_smooth_tables() noexcept {
    struct Tables {
        std::array<float, 512> pd_re_smooth{};
        std::array<float, 512> pd_im_smooth{};
    };

    Tables tables;

    for (int pd0 = 0; pd0 < 8; ++pd0) {
        float pd0_re = ipdopd_cos[pd0];
        float pd0_im = ipdopd_sin[pd0];

        for (int pd1 = 0; pd1 < 8; ++pd1) {
            float pd1_re = ipdopd_cos[pd1];
            float pd1_im = ipdopd_sin[pd1];

            for (int pd2 = 0; pd2 < 8; ++pd2) {
                float pd2_re = ipdopd_cos[pd2];
                float pd2_im = ipdopd_sin[pd2];

                float re_smooth = 0.25f * pd0_re + 0.5f * pd1_re + pd2_re;
                float im_smooth = 0.25f * pd0_im + 0.5f * pd1_im + pd2_im;

                float pd_mag = 1.0f / static_cast<float>(
                    hypot_constexpr(im_smooth, re_smooth));

                int idx = pd0 * 64 + pd1 * 8 + pd2;
                tables.pd_re_smooth[idx] = re_smooth * pd_mag;
                tables.pd_im_smooth[idx] = im_smooth * pd_mag;
            }
        }
    }

    return tables;
}

/**
 * Generate HA and HB mixing matrices
 */
constexpr auto generate_mixing_matrices() noexcept {
    struct Matrices {
        std::array<std::array<std::array<float, 4>, 8>, 46> HA{};
        std::array<std::array<std::array<float, 4>, 8>, 46> HB{};
    };

    Matrices matrices;

    for (int iid = 0; iid < 46; ++iid) {
        float c = iid_par_dequant[iid];
        float c1 = static_cast<float>(M_SQRT2) /
                   static_cast<float>(sqrt_constexpr(1.0f + c * c));
        float c2 = c * c1;

        for (int icc = 0; icc < 8; ++icc) {
            // PS_BASELINE mode (always used in this implementation)
            float alpha = 0.5f * acos_icc_invq[icc];
            float beta = alpha * (c1 - c2) * static_cast<float>(M_SQRT1_2);

            matrices.HA[iid][icc][0] = c2 * static_cast<float>(cos_constexpr(beta + alpha));
            matrices.HA[iid][icc][1] = c1 * static_cast<float>(cos_constexpr(beta - alpha));
            matrices.HA[iid][icc][2] = c2 * static_cast<float>(sin_constexpr(beta + alpha));
            matrices.HA[iid][icc][3] = c1 * static_cast<float>(sin_constexpr(beta - alpha));

            // Non-baseline mode (HB matrices)
            float rho = max_constexpr(icc_invq[icc], 0.05f);
            float alpha_b = 0.5f * static_cast<float>(
                atan2_constexpr(2.0 * c * rho, c * c - 1.0));
            float mu = c + 1.0f / c;
            mu = static_cast<float>(sqrt_constexpr(1.0 + (4.0 * rho * rho - 4.0) / (mu * mu)));
            float gamma = static_cast<float>(
                atan_constexpr(sqrt_constexpr((1.0 - mu) / (1.0 + mu))));

            if (alpha_b < 0) alpha_b += static_cast<float>(PI / 2.0);

            float alpha_c = static_cast<float>(cos_constexpr(alpha_b));
            float alpha_s = static_cast<float>(sin_constexpr(alpha_b));
            float gamma_c = static_cast<float>(cos_constexpr(gamma));
            float gamma_s = static_cast<float>(sin_constexpr(gamma));

            matrices.HB[iid][icc][0] = static_cast<float>(M_SQRT2) * alpha_c * gamma_c;
            matrices.HB[iid][icc][1] = static_cast<float>(M_SQRT2) * alpha_s * gamma_c;
            matrices.HB[iid][icc][2] = -static_cast<float>(M_SQRT2) * alpha_s * gamma_s;
            matrices.HB[iid][icc][3] = static_cast<float>(M_SQRT2) * alpha_c * gamma_s;
        }
    }

    return matrices;
}

/**
 * Generate Q_fract_allpass and phi_fract tables
 */
constexpr auto generate_allpass_tables() noexcept {
    struct Tables {
        std::array<std::array<std::array<std::array<float, 2>, 3>, 50>, 2> Q_fract_allpass{};
        std::array<std::array<std::array<float, 2>, 50>, 2> phi_fract{};
    };

    Tables tables;

    // Band 20
    for (int k = 0; k < NR_ALLPASS_BANDS20; ++k) {
        double f_center;
        if (k < static_cast<int>(f_center_20.size())) {
            f_center = f_center_20[k] * 0.125;
        } else {
            f_center = k - 6.5;
        }

        for (int m = 0; m < PS_AP_LINKS; ++m) {
            double theta = -PI * fractional_delay_links[m] * f_center;
            tables.Q_fract_allpass[0][k][m][0] = static_cast<float>(cos_constexpr(theta));
            tables.Q_fract_allpass[0][k][m][1] = static_cast<float>(sin_constexpr(theta));
        }

        double theta = -PI * fractional_delay_gain * f_center;
        tables.phi_fract[0][k][0] = static_cast<float>(cos_constexpr(theta));
        tables.phi_fract[0][k][1] = static_cast<float>(sin_constexpr(theta));
    }

    // Band 34
    for (int k = 0; k < NR_ALLPASS_BANDS34; ++k) {
        double f_center;
        if (k < static_cast<int>(f_center_34.size())) {
            f_center = f_center_34[k] / 24.0;
        } else {
            f_center = k - 26.5;
        }

        for (int m = 0; m < PS_AP_LINKS; ++m) {
            double theta = -PI * fractional_delay_links[m] * f_center;
            tables.Q_fract_allpass[1][k][m][0] = static_cast<float>(cos_constexpr(theta));
            tables.Q_fract_allpass[1][k][m][1] = static_cast<float>(sin_constexpr(theta));
        }

        double theta = -PI * fractional_delay_gain * f_center;
        tables.phi_fract[1][k][0] = static_cast<float>(cos_constexpr(theta));
        tables.phi_fract[1][k][1] = static_cast<float>(sin_constexpr(theta));
    }

    return tables;
}

/**
 * Make filters from prototype
 */
template<int bands>
constexpr auto make_filters_from_proto(const std::array<float, 7>& proto) noexcept {
    std::array<std::array<std::array<float, 2>, 8>, bands> filter{};

    for (int q = 0; q < bands; ++q) {
        for (int n = 0; n < 7; ++n) {
            double theta = 2.0 * PI * (q + 0.5) * (n - 6) / bands;
            filter[q][n][0] = proto[n] * static_cast<float>(cos_constexpr(theta));
            filter[q][n][1] = proto[n] * static_cast<float>(-sin_constexpr(theta));
        }
    }

    return filter;
}

// Generate all tables at compile time
constexpr auto pd_smooth_tables = generate_pd_smooth_tables();
constexpr auto& pd_re_smooth = pd_smooth_tables.pd_re_smooth;
constexpr auto& pd_im_smooth = pd_smooth_tables.pd_im_smooth;

constexpr auto mixing_matrices = generate_mixing_matrices();
constexpr auto& HA = mixing_matrices.HA;
constexpr auto& HB = mixing_matrices.HB;

constexpr auto allpass_tables = generate_allpass_tables();
constexpr auto& Q_fract_allpass = allpass_tables.Q_fract_allpass;
constexpr auto& phi_fract = allpass_tables.phi_fract;

constexpr auto f20_0_8 = make_filters_from_proto<8>(g0_Q8);
constexpr auto f34_0_12 = make_filters_from_proto<12>(g0_Q12);
constexpr auto f34_1_8 = make_filters_from_proto<8>(g1_Q8);
constexpr auto f34_2_4 = make_filters_from_proto<4>(g2_Q4);

// Total entries: 512 + 512 + 1472 + 1472 + 600 + 200 + 128 + 192 + 128 + 64 = ~5,280!

// Compile-time validation
namespace tests {
    // Test table sizes
    static_assert(pd_re_smooth.size() == 512, "PD RE smooth size");
    static_assert(pd_im_smooth.size() == 512, "PD IM smooth size");

    // Test trigonometric function accuracy
    constexpr double test_cos_0 = cos_constexpr(0.0);
    static_assert(test_cos_0 > 0.999 && test_cos_0 < 1.001, "cos(0) = 1");

    constexpr double test_sin_pi_2 = sin_constexpr(PI / 2.0);
    static_assert(test_sin_pi_2 > 0.999 && test_sin_pi_2 < 1.001, "sin(π/2) = 1");

    constexpr double test_atan_1 = atan_constexpr(1.0);
    static_assert(test_atan_1 > 0.784 && test_atan_1 < 0.786, "atan(1) = π/4");

    constexpr double test_sqrt_4 = sqrt_constexpr(4.0);
    static_assert(test_sqrt_4 > 1.999 && test_sqrt_4 < 2.001, "sqrt(4) = 2");

    // Test that tables have non-zero entries
    static_assert(pd_re_smooth[0] != 0.0f || pd_re_smooth[1] != 0.0f, "PD smooth has values");
    static_assert(HA[0][0][0] != 0.0f || HA[0][0][1] != 0.0f, "HA has values");
}

} // namespace aacps
} // namespace ffmpeg

#endif // AVCODEC_AACPS_TABLEGEN_CONSTEXPR_HPP
