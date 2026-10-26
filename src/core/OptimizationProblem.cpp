#include "core/OptimizationProblem.h"

#include <stdexcept>

namespace sa {

OptimizationProblem OptimizationProblem::fromImage(const GrayImage &encoded, const PinLayout &pins)
{
    OptimizationProblem problem;
    problem.width = encoded.width;
    problem.height = encoded.height;
    problem.target = encoded.pixels;
    problem.pins = pins;
    problem.weights.assign(problem.target.size(), 0.0f);

    const Vec2 c = pins.center();
    const double r2 = pins.radius() * pins.radius();
    for (int y = 0; y < encoded.height; ++y) {
        for (int x = 0; x < encoded.width; ++x) {
            const double dx = x + 0.5 - c.x;
            const double dy = y + 0.5 - c.y;
            if (dx * dx + dy * dy <= r2)
                problem.weights[static_cast<std::size_t>(y) * encoded.width + x] = 1.0f;
        }
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
