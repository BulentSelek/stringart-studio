#pragma once

#include "algorithms/IOptimizer.h"

#include <QColor>

#include <array>
#include <span>
#include <vector>

namespace sa {

enum class LayerOrder {
    Auto,        // builds both orders below and keeps the one with the lower error
    LightToDark, // lightest colour wound first (bottom), darkest last (on top)
    DarkToLight, // darkest first, lighter colours on top
    Palette      // palette order: first colour at the bottom
};

struct LayeredColorSettings
{
    LayerOrder order = LayerOrder::Auto;
    int refinePasses = 2;    // re-optimization sweeps over all layers after the first build
    double blurRadius = 1.0; // box radius (pixels) of the dithered colour maps (segment budgets)
    double initialShare = 0.5;  // part of each colour's budget used by the first build
};

// Multi-colour string art wound in layers: each palette colour is one
// continuous thread wound completely before the next colour starts, so the
// spool changes only between layers (the order a person can actually wind).
//
// Model. With "over" compositing a single-colour layer with transmittance T
// maps the canvas x below it to x T + k (1 - T): per pixel an affine map.
// So for layer c, with R the composite of the layers below and the layers
// above folded into final = A * in + B (A = product of their T, B = their
// colours), the picture is
//
//     F = A * (R * T_c + k_c * (1 - T_c)) + B.
//
// Layer greedy. A layer is built like Algorithm A, one continuous thread from
// config.startPin, but each candidate line is scored by the exact change of
// the full RGB objective S = sum w sum_ch (enc(F) - g)^2 through that map,
// over the line's pixels only. Same constraints as the other optimizers
// (separation, canConnect, repeated lines, thread length).
//
// Steps:
// 1. Segment budgets: the target is dithered (serpentine Floyd-Steinberg in
//    linear RGB) to the palette plus the board colour; maxSegments is split
//    in proportion to each colour's (blurred) share. `separate` exposes the
//    maps. A layer can stop earlier when no line improves the picture.
// 2. Build the layers bottom to top (A = 1, B = 0: nothing above yet), each
//    with `initialShare` of its budget (all of it without refinement).
// 3. Refinement: each pass rebuilds every layer from scratch with all other
//    layers fixed (block coordinate descent), allowed to use every segment
//    the other layers leave free, and keeps the new layer only if the error
//    drops, so the error never increases. The held-back share thus goes to
//    the colours where it lowers the error most, rather than by area.
// 4. Auto order builds and refines both light-to-dark and dark-to-light and
//    keeps the better result.
//
// Steps are returned layer by layer (bottom first); ColorRenderer::renderRgb
// of them reproduces the result.
class LayeredColorOptimizer final : public IOptimizer
{
public:
    static constexpr int kMaxColors = 8;

    // `target`: display-encoded r, g, b planes (problem.target is ignored).
    LayeredColorOptimizer(std::array<std::vector<float>, 3> target, std::vector<QColor> palette, QColor background,
                          LayeredColorSettings settings = {});

    [[nodiscard]] std::string name() const override { return "layered-color"; }

    [[nodiscard]] StringArtResult optimize(const OptimizationProblem &problem, const OptimizationConfig &config,
                                           std::stop_token stop = {}, const ProgressCallback &progress = {}) override;

    // Density maps d_c (one per palette colour), width x height.
    [[nodiscard]] static std::vector<std::vector<float>> separate(const std::array<std::vector<float>, 3> &target,
                                                                  std::span<const float> weights, int width,
                                                                  int height, std::span<const QColor> palette,
                                                                  const QColor &background, double blurRadius);

    // Palette indices from the bottom layer to the top layer (Auto: light to dark).
    [[nodiscard]] static std::vector<int> layerOrder(std::span<const QColor> palette, LayerOrder order);

    // Weighted SSE over the three display-encoded channels of an interleaved
    // linear RGB buffer; the reported error divides it by 3 * sum(weights).
    [[nodiscard]] static double weightedSse(std::span<const float> rgb, const std::array<std::vector<float>, 3> &target,
                                            std::span<const float> weights) noexcept;

private:
    std::array<std::vector<float>, 3> m_target;
    std::vector<QColor> m_palette;
    QColor m_background;
    LayeredColorSettings m_settings;
};

} // namespace sa
