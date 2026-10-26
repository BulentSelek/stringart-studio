#pragma once

#include <algorithm>
#include <cmath>

// Thread compositing model shared by the optimizer and the renderer.
//
// Every pixel stores a linear-light transmittance T in [0, 1]: the fraction of
// background light that is still visible. An empty canvas has T = 1.
// A thread segment covering a fraction c of a pixel with opacity a multiplies
// the transmittance by (1 - a * c):
//
//     T' = T * (1 - a * c)
//
// Equivalently, optical density D = -ln(T) is additive over segments, which
// is the Beer-Lambert view of stacked semi-transparent layers. Overlaps
// therefore darken progressively but never go below zero, unlike additive
// "black ink" accumulation. Assumptions: threads are treated as independent
// absorbing layers, scattering and shadows are ignored, and the coverage c
// comes from an anti-aliased box-filtered line profile (see LineRasterizer).
//
// The final colour is a linear-light blend: C = thread + (background - thread) * T.

namespace sa {

[[nodiscard]] inline float applyThread(float transmittance, float opacity, float coverage) noexcept
{
    return transmittance * (1.0f - opacity * coverage);
}

[[nodiscard]] inline float srgbToLinear(float v) noexcept
{
    v = std::clamp(v, 0.0f, 1.0f);
    return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}

[[nodiscard]] inline float linearToSrgb(float v) noexcept
{
    v = std::clamp(v, 0.0f, 1.0f);
    return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

// Table-driven linearToSrgb with linear interpolation (max abs. error ~1e-4).
// Used in the optimizer's inner loop; it defines the objective exactly, so
// incremental and brute-force scoring agree bit for bit.
class SrgbEncoder
{
public:
    static constexpr int kSize = 4096;

    [[nodiscard]] static float encode(float linear) noexcept
    {
        const float x = std::clamp(linear, 0.0f, 1.0f) * kSize;
        const int i = static_cast<int>(x);
        const float f = x - static_cast<float>(i);
        const float *t = table();
        return t[i] + (t[i + 1] - t[i]) * f;
    }

private:
    [[nodiscard]] static const float *table() noexcept;
};

} // namespace sa
