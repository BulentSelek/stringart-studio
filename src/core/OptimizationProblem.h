#pragma once

#include "core/GrayImage.h"
#include "core/PinGeometry.h"

#include <vector>

namespace sa {

// Input of an optimizer. All buffers are width * height, row-major.
//
// Objective (weighted mean squared error in display-encoded space):
//
//     E(T) = sum_p w_p * (enc(T_p) - g_p)^2 / sum_p w_p
//
// where T is the physically composited linear transmittance (Compositing.h),
// enc() the sRGB transfer function (SrgbEncoder) and g the display-encoded
// target gray. Threads are composited in linear light, but the error is
// measured where differences are perceptually more uniform; measuring in
// linear light under-weights dark regions (hair, eyes) and gives flat,
// low-contrast portraits.
struct OptimizationProblem
{
    int width = 0;
    int height = 0;
    std::vector<float> target;  // display-encoded (sRGB) target gray in [0, 1]
    std::vector<float> weights; // non-negative per-pixel weights
    PinLayout pins;

    // Builds the problem from a display-encoded working image. Pixels outside
    // the pin circle get weight 0 because no thread can reach them.
    static OptimizationProblem fromImage(const GrayImage &encoded, const PinLayout &pins);

    // Throws std::invalid_argument if buffers and pin canvas disagree.
    void validate() const;
};

} // namespace sa
