#pragma once

#include "algorithms/IOptimizer.h"
#include "core/LineRasterizer.h"

#include <span>

namespace sa {

// Algorithm A: greedy residual optimization of a continuous thread.
//
// State: linear transmittance T (initially 1), its encoding E = enc(T),
// target gray g and weights w (see OptimizationProblem).
// Objective: S(T) = sum_p w_p (enc(T_p) - g_p)^2  (E = S / sum w is reported).
//
// Each iteration evaluates every valid line (current, j) (separation >=
// minPinSeparation, PinLayout::canConnect, unused unless repeats are allowed) and computes the
// exact change of S if that line were added, touching only its pixels:
//
//     dS = sum_{p in line} w_p [ (enc(T_p (1 - a c_p)) - g_p)^2 - (E_p - g_p)^2 ]
//
// The line with the most negative dS is appended (ties: lowest pin index),
// T is updated in place, and the walk continues from the chosen pin. The
// run stops at maxSegments, maxThreadLength, when no valid line remains, or
// when the best dS > -minImprovement (no further improvement).
//
// Complexity per iteration: O(P * L), P = valid candidate pins, L = pixels
// per line (~ line length * (width + 1)). Candidates are scored in parallel;
// each score is computed sequentially, so results do not depend on the
// thread count. Memory: O(W * H) + optional line cache (see LineCache).
class GreedyOptimizer final : public IOptimizer
{
public:
    [[nodiscard]] std::string name() const override { return "greedy"; }

    [[nodiscard]] StringArtResult optimize(const OptimizationProblem &problem, const OptimizationConfig &config,
                                           std::stop_token stop = {}, const ProgressCallback &progress = {}) override;

    // Exact dS for adding `line`; `encoded` must equal enc(transmittance).
    [[nodiscard]] static double scoreLine(std::span<const LineSample> line, std::span<const float> transmittance,
                                          std::span<const float> encoded, std::span<const float> target,
                                          std::span<const float> weights, float opacity) noexcept;

    // Weighted SSE S(T) computed from linear transmittance.
    [[nodiscard]] static double weightedSse(std::span<const float> transmittance, std::span<const float> target,
                                            std::span<const float> weights) noexcept;
};

} // namespace sa
