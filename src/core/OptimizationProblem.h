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
    // the convex hull of the pins get weight 0 because no thread reaches them.
    // edgeEmphasis k >= 0 sets w_p = 1 + k * e_p inside the hull, where e_p in
    // [0, 1] is the normalized edge strength (edgeStrength). k = 0 gives the
    // plain MSE; larger k spends more thread on contours and features.
    static OptimizationProblem fromImage(const GrayImage &encoded, const PinLayout &pins, double edgeEmphasis = 0.0);

    // Sobel gradient magnitude, 3x3 box-blurred, divided by its 99th
    // percentile and clamped to [0, 1].
    static std::vector<float> edgeStrength(const GrayImage &encoded);

    // Throws std::invalid_argument if buffers and pin canvas disagree.
    void validate() const;
};

} // namespace sa
