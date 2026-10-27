#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace sa {

// Single-channel float image, row-major, values nominally in [0, 1].
// The meaning of the values (display-encoded gray or linear transmittance)
// is defined by the producer; see ImageProcessor and OptimizationProblem.
struct GrayImage
{
    int width = 0;
    int height = 0;
    std::vector<float> pixels;
    // Optional per-pixel coverage of the source image in [0, 1]; empty means
    // fully opaque. Pixels with alpha < 1 are transparent source areas or
    // padding outside the image and are treated as empty canvas.
    std::vector<float> alpha;

    GrayImage() = default;
    GrayImage(int w, int h, float fill = 0.0f)
        : width(w), height(h), pixels(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), fill)
    {
    }

    [[nodiscard]] bool isNull() const noexcept { return width <= 0 || height <= 0; }
    [[nodiscard]] float at(int x, int y) const { return pixels[static_cast<std::size_t>(y) * width + x]; }
    [[nodiscard]] float &at(int x, int y) { return pixels[static_cast<std::size_t>(y) * width + x]; }
};

// Three display-encoded (sRGB) planes in [0, 1] plus optional coverage alpha
// with the same meaning as GrayImage::alpha.
struct RgbImage
{
    int width = 0;
    int height = 0;
    std::array<std::vector<float>, 3> channels; // r, g, b
    std::vector<float> alpha;

    RgbImage() = default;
    RgbImage(int w, int h)
        : width(w), height(h)
    {
        for (auto &channel : channels)
            channel.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0.0f);
    }

    [[nodiscard]] bool isNull() const noexcept { return width <= 0 || height <= 0; }
};

} // namespace sa
