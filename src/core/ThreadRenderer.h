#pragma once

#include "core/PinGeometry.h"

#include <QColor>
#include <QImage>

#include <cstddef>
#include <limits>
#include <span>
#include <vector>

namespace sa {

struct RenderStyle
{
    double threadWidth = 1.0; // in working-image pixels; scaled with the output
    float opacity = 0.2f;
    QColor background = Qt::white;
    QColor thread = Qt::black;
};

// CPU renderer using the compositing model in Compositing.h. Rendering a
// sequence at scale 1 with the optimizer's width and opacity reproduces the
// optimizer's internal transmittance buffer exactly.
class ThreadRenderer
{
public:
    static constexpr std::size_t kAllSegments = std::numeric_limits<std::size_t>::max();

    // Transmittance of the first `segmentLimit` segments of `sequence` on a
    // canvas of round(canvasWidth * scale) x round(canvasHeight * scale).
    // Pin coordinates and thread width are multiplied by `scale`.
    [[nodiscard]] static std::vector<float> renderTransmittance(const PinLayout &layout, std::span<const int> sequence,
                                                                double threadWidth, float opacity, double scale = 1.0,
                                                                std::size_t segmentLimit = kAllSegments);

    // Linear-light blend of thread and background colours, encoded to sRGB.
    [[nodiscard]] static QImage toImage(std::span<const float> transmittance, int width, int height,
                                        const QColor &background = Qt::white, const QColor &thread = Qt::black);

    // Signed residual enc(T) - target as a diverging map: red where the
    // result is lighter than the target (more thread needed), blue where it
    // is darker, white where they match, gray where weight is zero.
    [[nodiscard]] static QImage residualImage(std::span<const float> transmittance, std::span<const float> target,
                                              std::span<const float> weights, int width, int height);

    [[nodiscard]] static QImage render(const PinLayout &layout, std::span<const int> sequence, const RenderStyle &style,
                                       double scale = 1.0, std::size_t segmentLimit = kAllSegments);
};

} // namespace sa
