#include "core/ThreadRenderer.h"

#include "core/Compositing.h"
#include "core/LineRasterizer.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>

namespace sa {

namespace {

constexpr int kEncodeTableSize = 4096;

const std::array<std::uint8_t, kEncodeTableSize + 1> &linearToSrgbTable()
{
    static const std::array<std::uint8_t, kEncodeTableSize + 1> table = [] {
        std::array<std::uint8_t, kEncodeTableSize + 1> t{};
        for (int i = 0; i <= kEncodeTableSize; ++i)
            t[i] = static_cast<std::uint8_t>(std::lround(linearToSrgb(static_cast<float>(i) / kEncodeTableSize) * 255.0f));
        return t;
    }();
    return table;
}

std::uint8_t encode(float linear)
{
    const int i = static_cast<int>(std::clamp(linear, 0.0f, 1.0f) * kEncodeTableSize + 0.5f);
    return linearToSrgbTable()[static_cast<std::size_t>(i)];
}

} // namespace

std::vector<float> ThreadRenderer::renderTransmittance(const PinLayout &layout, std::span<const int> sequence,
                                                       double threadWidth, float opacity, double scale,
                                                       std::size_t segmentLimit)
{
    const int width = static_cast<int>(std::lround(layout.canvasWidth() * scale));
    const int height = static_cast<int>(std::lround(layout.canvasHeight() * scale));
    std::vector<float> transmittance(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 1.0f);

    const std::size_t segments = sequence.size() < 2 ? 0 : std::min(sequence.size() - 1, segmentLimit);
    std::vector<LineSample> samples;
    for (std::size_t i = 0; i < segments; ++i) {
        rasterizePinLine(layout, sequence[i], sequence[i + 1], threadWidth * scale, width, height, samples, scale);
        for (const LineSample &s : samples)
            transmittance[s.index] = applyThread(transmittance[s.index], opacity, s.coverage);
    }
    return transmittance;
}

QImage ThreadRenderer::toImage(std::span<const float> transmittance, int width, int height,
                               const QColor &background, const QColor &thread)
{
    QImage image(width, height, QImage::Format_RGB32);
    if (transmittance.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height))
        return {};

    const float bg[3] = {srgbToLinear(background.redF()), srgbToLinear(background.greenF()),
                         srgbToLinear(background.blueF())};
    const float fg[3] = {srgbToLinear(thread.redF()), srgbToLinear(thread.greenF()), srgbToLinear(thread.blueF())};
    for (int y = 0; y < height; ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        const float *t = transmittance.data() + static_cast<std::size_t>(y) * width;
        for (int x = 0; x < width; ++x) {
            const float v = t[x];
            line[x] = qRgb(encode(fg[0] + (bg[0] - fg[0]) * v), encode(fg[1] + (bg[1] - fg[1]) * v),
                           encode(fg[2] + (bg[2] - fg[2]) * v));
        }
    }
    return image;
}

QImage ThreadRenderer::render(const PinLayout &layout, std::span<const int> sequence, const RenderStyle &style,
                              double scale, std::size_t segmentLimit)
{
    const std::vector<float> t =
        renderTransmittance(layout, sequence, style.threadWidth, style.opacity, scale, segmentLimit);
    const int width = static_cast<int>(std::lround(layout.canvasWidth() * scale));
    const int height = static_cast<int>(std::lround(layout.canvasHeight() * scale));
    return toImage(t, width, height, style.background, style.thread);
}

} // namespace sa
