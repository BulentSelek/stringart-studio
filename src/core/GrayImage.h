#pragma once

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

    GrayImage() = default;
    GrayImage(int w, int h, float fill = 0.0f)
        : width(w), height(h), pixels(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), fill)
    {
    }

    [[nodiscard]] bool isNull() const noexcept { return width <= 0 || height <= 0; }
    [[nodiscard]] float at(int x, int y) const { return pixels[static_cast<std::size_t>(y) * width + x]; }
    [[nodiscard]] float &at(int x, int y) { return pixels[static_cast<std::size_t>(y) * width + x]; }
};

} // namespace sa
