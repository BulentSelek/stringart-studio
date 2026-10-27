#pragma once

#include "core/GrayImage.h"
#include "core/OptimizationConfig.h"
#include "core/PinGeometry.h"

#include <QColor>

#include <array>
#include <cstddef>
#include <functional>
#include <span>
#include <stop_token>
#include <vector>

namespace sa {

struct PaletteSuggestionSettings
{
    // Candidate pool.
    std::size_t maxSamples = 40000;
    int iterations = 50;
    int extraClusters = 2;     // clusters beyond the requested colour count
    double reach = 0.7;        // coverage assumed for the "just deep enough" variant
    double minDistance = 0.02; // linear RGB distance below which a pixel counts as background
    // Proxy runs used to score palettes.
    int proxySize = 112;       // long side of the proxy canvas in pixels
    int maxProxyPins = 120;
};

// Suggests thread colours for the multi-colour mode from the target image.
//
// 1. Candidate pool (`candidates`). With "over" compositing one thread colour
//    k covering a fraction beta of a pixel gives C = bg + beta (k - bg) in
//    linear RGB, so a pixel explained by one colour lies on the ray from the
//    background towards that colour. The directions u = (C - bg) / |C - bg|
//    of the target pixels are clustered (weighted spherical k-means, weight =
//    pixel weight * |C - bg|, deterministic k-means++ seeding). Each cluster
//    contributes two colours on its ray: where the ray leaves the RGB cube
//    (most saturated) and the "just deep enough" colour at the 95th
//    percentile of |C - bg| divided by `reach`. Black (or white on a dark
//    background) is always a candidate.
// 2. Selection (`suggest`). Colours are chosen one at a time: each slot gets
//    the candidate that, together with the colours chosen so far, gives the
//    lowest error of a real ColorGreedyOptimizer run on a small proxy of the
//    problem (canvas `proxySize`, at most `maxProxyPins` pins, segment count
//    scaled so the thread density matches), followed by one pass that tries
//    to replace each chosen colour by every other candidate. This scores
//    mixtures of colours, which the clustering alone cannot.
//
// The result is a good starting point under the model, not a global optimum:
// the pool is finite and the selection is greedy.
class PaletteSuggester
{
public:
    // `target`: display-encoded r, g, b planes in [0, 1]; `weights`: per pixel,
    // 0 outside the region threads can reach. Distinct colours, at most
    // 2 * (count + extraClusters) + 1. Throws std::invalid_argument for
    // mismatched sizes or count outside 1..8.
    [[nodiscard]] static std::vector<QColor> candidates(const std::array<std::vector<float>, 3> &target,
                                                        std::span<const float> weights, int count,
                                                        const QColor &background,
                                                        const PaletteSuggestionSettings &settings = {});

    // (evaluated, total) proxy runs.
    using Progress = std::function<void(int, int)>;

    // `image`: the processed colour target at any resolution (padding already
    // white); `frame` and `config` describe the intended run. Returns `count`
    // colours in order of selection, or fewer when the pool is smaller or the
    // search is stopped (then the colours chosen so far).
    [[nodiscard]] static std::vector<QColor> suggest(const RgbImage &image, const FrameSpec &frame,
                                                     const OptimizationConfig &config, int workingSize, int count,
                                                     const QColor &background, std::stop_token stop = {},
                                                     const Progress &progress = {},
                                                     const PaletteSuggestionSettings &settings = {});
};

} // namespace sa
