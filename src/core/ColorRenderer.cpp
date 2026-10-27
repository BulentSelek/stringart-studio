#include "core/ColorRenderer.h"

#include "core/Compositing.h"
#include "core/LineRasterizer.h"

#include <algorithm>
#include <cmath>

namespace sa {

LinearRgb toLinearRgb(const QColor &color)
{
    return {srgbToLinear(static_cast<float>(color.redF())), srgbToLinear(static_cast<float>(color.greenF())),
            srgbToLinear(static_cast<float>(color.blueF()))};
}

std::vector<float> ColorRenderer::renderRgb(const PinLayout &layout, std::span<const ColorStep> steps,
                                            std::span<const QColor> palette, const QColor &background,
                                            double threadWidth, float opacity, double scale, std::size_t stepLimit)
{
    const int width = static_cast<int>(std::lround(layout.canvasWidth() * scale));
    const int height = static_cast<int>(std::lround(layout.canvasHeight() * scale));
    const std::size_t pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const LinearRgb bg = toLinearRgb(background);
    std::vector<float> rgb(pixels * 3);
    for (std::size_t p = 0; p < pixels; ++p)
        std::copy(bg.begin(), bg.end(), rgb.begin() + static_cast<std::ptrdiff_t>(3 * p));

    std::vector<LinearRgb> colors;
    colors.reserve(palette.size());
    for (const QColor &c : palette)
        colors.push_back(toLinearRgb(c));

    std::vector<LineSample> samples;
    const std::size_t count = std::min(steps.size(), stepLimit);
    for (std::size_t i = 0; i < count; ++i) {
        const ColorStep &step = steps[i];
        rasterizePinLine(layout, step.from, step.to, threadWidth * scale, width, height, samples, scale);
        const LinearRgb &k = colors.at(static_cast<std::size_t>(step.color));
        for (const LineSample &s : samples)
            applyThreadOver(&rgb[3 * static_cast<std::size_t>(s.index)], k, opacity * s.coverage);
    }
    return rgb;
}

QImage ColorRenderer::toImage(std::span<const float> rgb, int width, int height)
{
    if (rgb.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3)
        return {};
    QImage image(width, height, QImage::Format_RGB32);
    auto byte = [](float v) { return static_cast<int>(std::lround(SrgbEncoder::encode(v) * 255.0f)); };
    for (int y = 0; y < height; ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        const float *row = rgb.data() + static_cast<std::size_t>(y) * width * 3;
        for (int x = 0; x < width; ++x)
            line[x] = qRgb(byte(row[3 * x]), byte(row[3 * x + 1]), byte(row[3 * x + 2]));
    }
    return image;
}

QImage ColorRenderer::residualImage(std::span<const float> rgb, const std::array<std::vector<float>, 3> &target,
                                    std::span<const float> weights, int width, int height)
{
    const std::size_t pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (rgb.size() != pixels * 3 || weights.size() != pixels || target[0].size() != pixels)
        return {};
    QImage image(width, height, QImage::Format_RGB32);
    for (int y = 0; y < height; ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * width + x;
            if (weights[i] <= 0.0f) {
                line[x] = qRgb(96, 96, 96);
                continue;
            }
            float r = 0.0f;
            constexpr float luma[3] = {0.2126f, 0.7152f, 0.0722f};
            for (std::size_t c = 0; c < 3; ++c)
                r += luma[c] * (SrgbEncoder::encode(rgb[3 * i + c]) - target[c][i]);
            r = std::clamp(r, -1.0f, 1.0f);
            const int fade = static_cast<int>(std::lround(255.0f * (1.0f - std::abs(r))));
            line[x] = r >= 0.0f ? qRgb(255, fade, fade) : qRgb(fade, fade, 255);
        }
    }
    return image;
}

QImage ColorRenderer::render(const PinLayout &layout, std::span<const ColorStep> steps,
                             std::span<const QColor> palette, const QColor &background, double threadWidth,
                             float opacity, double scale, std::size_t stepLimit)
{
    const std::vector<float> rgb =
        renderRgb(layout, steps, palette, background, threadWidth, opacity, scale, stepLimit);
    return toImage(rgb, static_cast<int>(std::lround(layout.canvasWidth() * scale)),
                   static_cast<int>(std::lround(layout.canvasHeight() * scale)));
}

} // namespace sa
