#include "core/OptimizationProblem.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sa {

std::vector<float> OptimizationProblem::edgeStrength(const GrayImage &encoded)
{
    const int w = encoded.width;
    const int h = encoded.height;
    std::vector<float> magnitude(encoded.pixels.size(), 0.0f);
    auto at = [&](int x, int y) {
        return encoded.at(std::clamp(x, 0, w - 1), std::clamp(y, 0, h - 1));
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float gx = (at(x + 1, y - 1) + 2.0f * at(x + 1, y) + at(x + 1, y + 1))
                           - (at(x - 1, y - 1) + 2.0f * at(x - 1, y) + at(x - 1, y + 1));
            const float gy = (at(x - 1, y + 1) + 2.0f * at(x, y + 1) + at(x + 1, y + 1))
                           - (at(x - 1, y - 1) + 2.0f * at(x, y - 1) + at(x + 1, y - 1));
            magnitude[static_cast<std::size_t>(y) * w + x] = std::sqrt(gx * gx + gy * gy);
        }
    }
    // 3x3 box blur widens thin edges so nearby threads also count.
    std::vector<float> blurred(magnitude.size(), 0.0f);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float sum = 0.0f;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    sum += magnitude[static_cast<std::size_t>(std::clamp(y + dy, 0, h - 1)) * w
                                     + std::clamp(x + dx, 0, w - 1)];
            blurred[static_cast<std::size_t>(y) * w + x] = sum / 9.0f;
        }
    }
    // Normalize by the 99th percentile so a few extreme edges do not flatten the rest.
    std::vector<float> sorted = blurred;
    const auto nth = sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() * 99 / 100);
    std::nth_element(sorted.begin(), nth, sorted.end());
    const float scale = *nth > 1e-6f ? 1.0f / *nth : 0.0f;
    for (float &v : blurred)
        v = std::min(1.0f, v * scale);
    return blurred;
}

OptimizationProblem OptimizationProblem::fromImage(const GrayImage &encoded, const PinLayout &pins,
                                                   double edgeEmphasis)
{
    if (!(edgeEmphasis >= 0.0))
        throw std::invalid_argument("edge emphasis must not be negative");
    OptimizationProblem problem;
    problem.width = encoded.width;
    problem.height = encoded.height;
    problem.target = encoded.pixels;
    problem.pins = pins;
    if (pins.canvasWidth() != encoded.width || pins.canvasHeight() != encoded.height)
        throw std::invalid_argument("pin layout canvas does not match the image size");
    problem.weights = pins.reachableMask();
    if (edgeEmphasis > 0.0) {
        const std::vector<float> edges = edgeStrength(encoded);
        const auto k = static_cast<float>(edgeEmphasis);
        for (std::size_t i = 0; i < edges.size(); ++i)
            problem.weights[i] *= 1.0f + k * edges[i];
    }
    problem.validate();
    return problem;
}

void OptimizationProblem::validate() const
{
    if (width <= 0 || height <= 0)
        throw std::invalid_argument("problem image is empty");
    const auto size = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (target.size() != size || weights.size() != size)
        throw std::invalid_argument("target/weight buffers do not match the image size");
    if (pins.canvasWidth() != width || pins.canvasHeight() != height)
        throw std::invalid_argument("pin layout canvas does not match the image size");
}

} // namespace sa
