#pragma once

#include "algorithms/IOptimizer.h"
#include "core/ColorRenderer.h"

#include <QColor>

#include <array>
#include <span>
#include <vector>

namespace sa {

// Multi-colour greedy optimization: one continuous thread per palette colour.
//
// State: linear RGB canvas C (initially the background), composited with the
// "over" model in ColorRenderer. Target: display-encoded RGB planes g_c.
// Objective: S = sum_p w_p sum_c (enc(C_pc) - g_pc)^2; the reported error is
// S / (3 sum w), comparable to the single-colour weighted MSE.
//
// Each iteration scores every valid line from every colour's current pin
// (separation, canConnect, per-colour unused unless repeats are allowed),
// exactly and only over the line's pixels, picks the best (colour, line)
// (ties: lowest colour, then lowest pin), appends it to the global step
// list and moves that colour's current pin. All threads start at
// config.startPin. Stops like GreedyOptimizer; maxSegments and
// maxThreadLengthMm count all colours together.
//
// Complexity per iteration: O(K * P * L) for K colours. Scoring is parallel
// and deterministic.
class ColorGreedyOptimizer final : public IOptimizer
{
public:
    static constexpr int kMaxColors = 8;

    // `target` holds the encoded r, g, b planes (problem.target is ignored);
    // the problem supplies size, weights and pins.
    ColorGreedyOptimizer(std::array<std::vector<float>, 3> target, std::vector<QColor> palette, QColor background);

    [[nodiscard]] std::string name() const override { return "color-greedy"; }

    [[nodiscard]] StringArtResult optimize(const OptimizationProblem &problem, const OptimizationConfig &config,
                                           std::stop_token stop = {}, const ProgressCallback &progress = {}) override;

    // Starts from this interleaved linear RGB canvas instead of the board
    // colour (e.g. the threads of lower layers). Empty = the board colour.
    void setInitialCanvas(std::vector<float> rgb) { m_initialCanvas = std::move(rgb); }

    // Weighted SSE over the three encoded channels.
    [[nodiscard]] static double weightedSse(std::span<const float> rgb, const std::array<std::vector<float>, 3> &target,
                                            std::span<const float> weights) noexcept;

private:
    std::array<std::vector<float>, 3> m_target;
    std::vector<QColor> m_palette;
    QColor m_background;
    std::vector<float> m_initialCanvas;
};

} // namespace sa
