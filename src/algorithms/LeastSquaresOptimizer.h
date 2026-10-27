#pragma once

#include "core/OptimizationConfig.h"
#include "core/OptimizationProblem.h"

#include <stop_token>
#include <utility>
#include <vector>

namespace sa {

struct LeastSquaresSettings
{
    int maxIterations = 60;      // coordinate-descent sweeps
    double maxWeight = 4.0;      // upper bound of a line weight (threads per line)
    double tolerance = 1e-6;     // stop when the relative objective change of a sweep is below this
    double minTransmittance = 1e-3; // clamps the target before taking -ln
};

struct LeastSquaresResult
{
    std::vector<std::pair<int, int>> lines; // candidate pin pairs (a < b)
    std::vector<double> initialWeights;     // how often each line occurs in the sequence
    std::vector<double> weights;            // optimized weights in [0, maxWeight]
    std::vector<float> transmittance;       // virtual reconstruction T = exp(-sum x_l d_l)
    int width = 0;
    int height = 0;
    double initialObjective = 0.0;          // weighted density SSE with the initial weights
    double finalObjective = 0.0;
    double virtualError = 0.0;              // standard objective (weighted MSE, sRGB) of the virtual image
    int iterations = 0;
    bool cancelled = false;
};

// Algorithm B: bounded least-squares weights for a fixed set of lines.
//
// This is NOT a manufacturable result. It answers "how well could these
// lines reproduce the image if each could carry any amount of thread in
// [0, maxWeight]?" and therefore gives a virtual upper bound on quality for
// the line set; the physical result is always the integer, continuous
// sequence produced by Algorithms A and C.
//
// Model (optical density, where threads add up exactly):
//   d_l(p) = -ln(1 - a c_l(p))           density of one thread along line l
//   D(p)   = sum_l x_l d_l(p)            virtual density, T = exp(-D)
//   D*(p)  = -ln(max(t(p), tmin))        target density, t = linear-light target
//   minimize  sum_p w_p (D(p) - D*(p))^2   subject to 0 <= x_l <= maxWeight
// The problem is a convex box-constrained quadratic program; it is solved by
// cyclic projected coordinate descent, x_l <- clamp(x_l - g_l / h_l, 0, xmax)
// with g_l = sum w d_l r and h_l = sum w d_l^2 (r = D - D*), which decreases
// the objective monotonically and converges to the KKT point.
// Complexity per sweep: O(sum of line lengths). Requires opacity < 1.
class LeastSquaresOptimizer
{
public:
    // Candidate lines are the distinct segments of `sequence`; the initial
    // weights are their multiplicities. Throws std::invalid_argument for an
    // invalid problem, config or sequence.
    [[nodiscard]] static LeastSquaresResult solve(const OptimizationProblem &problem, const OptimizationConfig &config,
                                                  const std::vector<int> &sequence,
                                                  const LeastSquaresSettings &settings = {}, std::stop_token stop = {});
};

} // namespace sa
