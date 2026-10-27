#include "algorithms/PaletteSuggester.h"

#include "algorithms/ColorGreedyOptimizer.h"
#include "core/ColorRenderer.h"
#include "core/Compositing.h"
#include "core/ImageProcessor.h"
#include "core/OptimizationProblem.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>

namespace sa {

namespace {

using Vec3 = std::array<double, 3>;

double dot(const Vec3 &a, const Vec3 &b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

double norm(const Vec3 &a)
{
    return std::sqrt(dot(a, a));
}

struct Sample
{
    Vec3 direction; // unit vector from the background
    double distance = 0.0;
    double weight = 0.0; // pixel weight * distance
};

// Largest t with bg + u t inside [0, 1]^3.
double rayExit(const Vec3 &bg, const Vec3 &u)
{
    double t = std::numeric_limits<double>::max();
    for (int c = 0; c < 3; ++c) {
        if (u[c] > 1e-12)
            t = std::min(t, (1.0 - bg[c]) / u[c]);
        else if (u[c] < -1e-12)
            t = std::min(t, -bg[c] / u[c]);
    }
    return t == std::numeric_limits<double>::max() ? 0.0 : t;
}

double weightedPercentile(std::vector<std::pair<double, double>> values, double q)
{
    // values: (value, weight)
    std::sort(values.begin(), values.end());
    double total = 0.0;
    for (const auto &v : values)
        total += v.second;
    double accumulated = 0.0;
    for (const auto &v : values) {
        accumulated += v.second;
        if (accumulated >= q * total)
            return v.first;
    }
    return values.empty() ? 0.0 : values.back().first;
}

QColor toColor(const Vec3 &linear)
{
    auto channel = [](double v) {
        return std::clamp(static_cast<int>(std::lround(linearToSrgb(static_cast<float>(std::clamp(v, 0.0, 1.0)))
                                                       * 255.0)),
                          0, 255);
    };
    return QColor(channel(linear[0]), channel(linear[1]), channel(linear[2]));
}

} // namespace

std::vector<QColor> PaletteSuggester::candidates(const std::array<std::vector<float>, 3> &target,
                                                 std::span<const float> weights, int count,
                                                 const QColor &background, const PaletteSuggestionSettings &settings)
{
    if (count < 1 || count > ColorGreedyOptimizer::kMaxColors)
        throw std::invalid_argument("palette size must be 1..8");
    const std::size_t pixels = target[0].size();
    if (target[1].size() != pixels || target[2].size() != pixels || weights.size() != pixels)
        throw std::invalid_argument("target and weights must have the same size");

    const LinearRgb bgLinear = toLinearRgb(background);
    const Vec3 bg{bgLinear[0], bgLinear[1], bgLinear[2]};

    // Deterministic subsample of the weighted pixels.
    std::size_t candidates = 0;
    for (float w : weights)
        candidates += w > 0.0f ? 1 : 0;
    const std::size_t stride = std::max<std::size_t>(1, candidates / std::max<std::size_t>(1, settings.maxSamples));
    std::vector<Sample> samples;
    samples.reserve(std::min(candidates, settings.maxSamples) + 1);
    std::size_t seen = 0;
    for (std::size_t p = 0; p < pixels; ++p) {
        if (weights[p] <= 0.0f)
            continue;
        if (seen++ % stride != 0)
            continue;
        const Vec3 d{srgbToLinear(target[0][p]) - bg[0], srgbToLinear(target[1][p]) - bg[1],
                     srgbToLinear(target[2][p]) - bg[2]};
        const double distance = norm(d);
        if (distance < settings.minDistance)
            continue;
        samples.push_back({{d[0] / distance, d[1] / distance, d[2] / distance}, distance, weights[p] * distance});
    }

    // The opposite extreme of the background is always a candidate.
    const double bgLuminance = 0.2126 * bg[0] + 0.7152 * bg[1] + 0.0722 * bg[2];
    const QColor opposite = bgLuminance > 0.5 ? QColor(Qt::black) : QColor(Qt::white);
    if (samples.empty())
        return {opposite};
    const int clusters = count + settings.extraClusters;

    // k-means++ seeding (fixed seed), distance 1 - cos.
    std::mt19937 random(12345);
    std::vector<Vec3> centres;
    {
        std::vector<double> score(samples.size());
        for (std::size_t i = 0; i < samples.size(); ++i)
            score[i] = samples[i].weight;
        std::discrete_distribution<std::size_t> first(score.begin(), score.end());
        centres.push_back(samples[first(random)].direction);
        while (static_cast<int>(centres.size()) < clusters) {
            double total = 0.0;
            for (std::size_t i = 0; i < samples.size(); ++i) {
                double nearest = std::numeric_limits<double>::max();
                for (const Vec3 &c : centres)
                    nearest = std::min(nearest, 1.0 - dot(samples[i].direction, c));
                score[i] = samples[i].weight * std::max(0.0, nearest) * std::max(0.0, nearest);
                total += score[i];
            }
            if (total <= 1e-12)
                break; // fewer distinct directions than requested colours
            std::discrete_distribution<std::size_t> next(score.begin(), score.end());
            centres.push_back(samples[next(random)].direction);
        }
    }

    // Weighted spherical k-means.
    std::vector<int> assignment(samples.size(), -1);
    for (int iteration = 0; iteration < settings.iterations; ++iteration) {
        bool changed = false;
        for (std::size_t i = 0; i < samples.size(); ++i) {
            int best = 0;
            double bestDot = -2.0;
            for (std::size_t c = 0; c < centres.size(); ++c) {
                const double d = dot(samples[i].direction, centres[c]);
                if (d > bestDot) {
                    bestDot = d;
                    best = static_cast<int>(c);
                }
            }
            changed |= assignment[i] != best;
            assignment[i] = best;
        }
        std::vector<Vec3> sums(centres.size(), Vec3{0, 0, 0});
        for (std::size_t i = 0; i < samples.size(); ++i) {
            for (int k = 0; k < 3; ++k)
                sums[static_cast<std::size_t>(assignment[i])][k] += samples[i].weight * samples[i].direction[k];
        }
        for (std::size_t c = 0; c < centres.size(); ++c) {
            const double n = norm(sums[c]);
            if (n > 1e-12)
                centres[c] = {sums[c][0] / n, sums[c][1] / n, sums[c][2] / n};
        }
        if (!changed && iteration > 0)
            break;
    }

    // Two colours on each cluster's ray: saturated and just deep enough.
    std::vector<QColor> pool{opposite};
    auto add = [&pool](const QColor &colour) {
        if (std::find(pool.begin(), pool.end(), colour) == pool.end())
            pool.push_back(colour);
    };
    for (std::size_t c = 0; c < centres.size(); ++c) {
        std::vector<std::pair<double, double>> distances;
        for (std::size_t i = 0; i < samples.size(); ++i) {
            if (assignment[i] == static_cast<int>(c))
                distances.emplace_back(samples[i].distance, samples[i].weight);
        }
        if (distances.empty())
            continue;
        const double exit = rayExit(bg, centres[c]);
        const double depth = std::min(exit, weightedPercentile(std::move(distances), 0.95) / settings.reach);
        for (double t : {exit, depth})
            add(toColor({bg[0] + centres[c][0] * t, bg[1] + centres[c][1] * t, bg[2] + centres[c][2] * t}));
    }
    return pool;
}

std::vector<QColor> PaletteSuggester::suggest(const RgbImage &image, const FrameSpec &frame,
                                              const OptimizationConfig &config, int workingSize, int count,
                                              const QColor &background, std::stop_token stop,
                                              const Progress &progress, const PaletteSuggestionSettings &settings)
{
    if (image.isNull())
        throw std::invalid_argument("no target image");
    if (count < 1 || count > ColorGreedyOptimizer::kMaxColors)
        throw std::invalid_argument("palette size must be 1..8");

    // Proxy frame: fewer pins (custom frames keep every k-th pin).
    FrameSpec proxyFrame = frame;
    const int fullPins = frame.kind == PinLayoutKind::Custom ? static_cast<int>(frame.customPinsMm.size())
                                                             : frame.pinCount;
    int proxyPins = std::min(fullPins, settings.maxProxyPins);
    if (frame.kind == PinLayoutKind::Custom) {
        const auto stride = static_cast<std::size_t>((fullPins + settings.maxProxyPins - 1) / settings.maxProxyPins);
        proxyFrame.customPinsMm.clear();
        for (std::size_t i = 0; i < frame.customPinsMm.size(); i += stride)
            proxyFrame.customPinsMm.push_back(frame.customPinsMm[i]);
        proxyPins = static_cast<int>(proxyFrame.customPinsMm.size());
    } else {
        proxyFrame.pinCount = proxyPins;
    }
    const int proxySize = std::min(settings.proxySize, workingSize);
    const PinLayout layout = PinLayout::fromFrame(proxyFrame, proxySize);

    // Proxy target: the processed image resampled to the proxy canvas.
    const QImage scaled = ImageProcessor::toQImage(image)
                              .scaled(layout.canvasWidth(), layout.canvasHeight(), Qt::IgnoreAspectRatio,
                                      Qt::SmoothTransformation)
                              .convertToFormat(QImage::Format_RGB32);
    std::array<std::vector<float>, 3> target;
    const std::size_t pixels = static_cast<std::size_t>(layout.canvasWidth()) * layout.canvasHeight();
    for (auto &plane : target)
        plane.resize(pixels);
    for (int y = 0; y < layout.canvasHeight(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(scaled.constScanLine(y));
        for (int x = 0; x < layout.canvasWidth(); ++x) {
            const std::size_t p = static_cast<std::size_t>(y) * layout.canvasWidth() + x;
            target[0][p] = qRed(line[x]) / 255.0f;
            target[1][p] = qGreen(line[x]) / 255.0f;
            target[2][p] = qBlue(line[x]) / 255.0f;
        }
    }
    OptimizationProblem problem;
    problem.width = layout.canvasWidth();
    problem.height = layout.canvasHeight();
    problem.weights = layout.reachableMask();
    problem.target.assign(pixels, 1.0f); // unused by the colour optimizer
    problem.pins = layout;

    // Same thread density: passes per pixel scale with segments / canvas size.
    OptimizationConfig proxyConfig = config;
    proxyConfig.maxSegments = std::max(
        30, static_cast<int>(std::lround(static_cast<double>(config.maxSegments) * proxySize / workingSize)));
    proxyConfig.minPinSeparation = std::max(
        1, static_cast<int>(std::lround(static_cast<double>(config.minPinSeparation) * proxyPins / fullPins)));
    proxyConfig.minPinSeparation = std::min(proxyConfig.minPinSeparation, proxyPins / 2);
    proxyConfig.startPin = 0;
    proxyConfig.maxThreadLengthMm = 0.0;
    validateConfig(proxyConfig, proxyPins);

    const std::vector<QColor> pool = candidates(target, problem.weights, count, background, settings);
    const int picks = std::min<int>(count, static_cast<int>(pool.size()));
    const int poolSize = static_cast<int>(pool.size());
    int total = picks * (poolSize - picks); // swap pass
    for (int slot = 0; slot < picks; ++slot)
        total += poolSize - slot;            // forward selection
    int done = 0;
    auto evaluate = [&](const std::vector<QColor> &palette) {
        ColorGreedyOptimizer optimizer(target, palette, background);
        const double error = optimizer.optimize(problem, proxyConfig, stop).finalError;
        if (progress)
            progress(++done, total);
        return error;
    };
    auto contains = [](const std::vector<QColor> &palette, const QColor &c) {
        return std::find(palette.begin(), palette.end(), c) != palette.end();
    };

    // Forward selection: each slot gets the best colour given the earlier ones.
    std::vector<QColor> chosen;
    double chosenError = std::numeric_limits<double>::max();
    for (int slot = 0; slot < picks; ++slot) {
        double bestError = std::numeric_limits<double>::max();
        QColor best;
        for (const QColor &candidate : pool) {
            if (contains(chosen, candidate))
                continue;
            if (stop.stop_requested())
                return chosen;
            std::vector<QColor> palette = chosen;
            palette.push_back(candidate);
            const double error = evaluate(palette);
            if (error < bestError) { // ties: earlier candidate
                bestError = error;
                best = candidate;
            }
        }
        chosen.push_back(best);
        chosenError = bestError;
    }

    // One swap pass: an early pick (often black, which explains every dark
    // colour a little) can block a better combination of later colours.
    for (int slot = 0; slot < picks; ++slot) {
        const std::vector<QColor> before = chosen; // only chosen[slot] changes in this loop
        for (const QColor &candidate : pool) {
            if (contains(before, candidate))
                continue;
            if (stop.stop_requested())
                return chosen;
            std::vector<QColor> palette = chosen;
            palette[static_cast<std::size_t>(slot)] = candidate;
            const double error = evaluate(palette);
            if (error < chosenError) {
                chosenError = error;
                chosen = std::move(palette);
            }
        }
    }
    return chosen;
}

} // namespace sa
