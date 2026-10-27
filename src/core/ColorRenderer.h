#pragma once

#include "core/PinGeometry.h"
#include "core/StringArtResult.h"

#include <QColor>
#include <QImage>

#include <array>
#include <cstddef>
#include <limits>
#include <span>
#include <vector>

namespace sa {

using LinearRgb = std::array<float, 3>;

[[nodiscard]] LinearRgb toLinearRgb(const QColor &color);

// Multi-colour compositing ("over"): a thread of linear colour k covering a
// fraction beta = a * c of a pixel shows its own colour there:
//
//     C' = C * (1 - beta) + k * beta        (per linear RGB channel)
//
// The canvas starts at the background colour. For a single colour this is
// exactly the single-colour model (C = k + (bg - k) * T). With several
// colours the result depends on the order, so results carry a global step
// order and the renderer applies steps in that order.
inline void applyThreadOver(float *rgb, const LinearRgb &color, float beta) noexcept
{
    rgb[0] += (color[0] - rgb[0]) * beta;
    rgb[1] += (color[1] - rgb[1]) * beta;
    rgb[2] += (color[2] - rgb[2]) * beta;
}

class ColorRenderer
{
public:
    static constexpr std::size_t kAllSteps = std::numeric_limits<std::size_t>::max();

    // Interleaved linear RGB of the first `stepLimit` steps on a canvas scaled
    // by `scale` (pin coordinates and thread width scaled as in ThreadRenderer).
    [[nodiscard]] static std::vector<float> renderRgb(const PinLayout &layout, std::span<const ColorStep> steps,
                                                      std::span<const QColor> palette, const QColor &background,
                                                      double threadWidth, float opacity, double scale = 1.0,
                                                      std::size_t stepLimit = kAllSteps);

    // sRGB-encoded image of an interleaved linear RGB buffer.
    [[nodiscard]] static QImage toImage(std::span<const float> rgb, int width, int height);

    // Diverging map of the luminance error (encoded result - target): red =
    // lighter than the target, blue = darker, gray = zero weight.
    [[nodiscard]] static QImage residualImage(std::span<const float> rgb,
                                              const std::array<std::vector<float>, 3> &target,
                                              std::span<const float> weights, int width, int height);

    [[nodiscard]] static QImage render(const PinLayout &layout, std::span<const ColorStep> steps,
                                       std::span<const QColor> palette, const QColor &background, double threadWidth,
                                       float opacity, double scale = 1.0, std::size_t stepLimit = kAllSteps);
};

} // namespace sa
