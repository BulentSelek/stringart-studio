#include "algorithms/LayeredColorOptimizer.h"

#include "core/ColorRenderer.h"
#include "core/Compositing.h"
#include "core/LineCache.h"

#include <QThread>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentMap>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace sa {

namespace {

// Separable box blur with clamped borders.
void boxBlur(std::vector<float> &plane, int width, int height, int radius)
{
    if (radius <= 0)
        return;
    std::vector<float> line;
    auto pass = [&](int count, int length, auto at) {
        line.resize(static_cast<std::size_t>(length));
        for (int c = 0; c < count; ++c) {
            for (int i = 0; i < length; ++i)
                line[static_cast<std::size_t>(i)] = at(c, i);
            for (int i = 0; i < length; ++i) {
                double sum = 0.0;
                int n = 0;
                for (int k = std::max(0, i - radius); k <= std::min(length - 1, i + radius); ++k, ++n)
                    sum += line[static_cast<std::size_t>(k)];
                at(c, i) = static_cast<float>(sum / n);
            }
        }
    };
    pass(height, width, [&](int y, int x) -> float & { return plane[static_cast<std::size_t>(y) * width + x]; });
    pass(width, height, [&](int x, int y) -> float & { return plane[static_cast<std::size_t>(y) * width + x]; });
}

double luminance(const LinearRgb &c)
{
    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
}

std::size_t pairIndex(int a, int b, int n) noexcept
{
    if (a > b)
        std::swap(a, b);
    const auto i = static_cast<std::size_t>(a);
    return i * static_cast<std::size_t>(n) - i * (i + 1) / 2 + static_cast<std::size_t>(b - a - 1);
}

struct Layer
{
    int colour = 0;
    LinearRgb k{};
    int budget = 0;
    std::vector<int> sequence;      // pins; empty = no thread
    std::vector<float> transmittance; // per pixel, 1 = no thread
    double lengthMm = 0.0;
    StopReason stopReason = StopReason::NoImprovement;
};

struct Candidate
{
    double delta = std::numeric_limits<double>::infinity();
    int pin = -1;

    [[nodiscard]] bool betterThan(const Candidate &other) const noexcept
    {
        if (pin < 0)
            return false;
        if (other.pin < 0)
            return true;
        return delta < other.delta || (delta == other.delta && pin < other.pin);
    }
};

// Everything a layer build needs that does not change while it runs.
struct Shared
{
    const OptimizationProblem &problem;
    const OptimizationConfig &config;
    const std::array<std::vector<float>, 3> &target;
    const LineCache &cache;
    QThreadPool &pool;
    int threads;
    std::size_t pixels;
};

// Canvas below layer `index`: the board, then layers 0 .. index-1.
std::vector<float> below(const std::vector<Layer> &layers, std::size_t index, const LinearRgb &board,
                         std::size_t pixels)
{
    std::vector<float> rgb(pixels * 3);
    for (std::size_t p = 0; p < pixels; ++p)
        std::copy(board.begin(), board.end(), rgb.begin() + static_cast<std::ptrdiff_t>(3 * p));
    for (std::size_t l = 0; l < index; ++l) {
        const Layer &layer = layers[l];
        for (std::size_t p = 0; p < pixels; ++p) {
            const float t = layer.transmittance[p];
            for (std::size_t c = 0; c < 3; ++c)
                rgb[3 * p + c] = rgb[3 * p + c] * t + layer.k[c] * (1.0f - t);
        }
    }
    return rgb;
}

// Layers above `index` as the per-pixel affine map final = a * in + b.
void above(const std::vector<Layer> &layers, std::size_t index, std::size_t pixels, std::vector<float> &a,
           std::vector<float> &b)
{
    a.assign(pixels, 1.0f);
    b.assign(pixels * 3, 0.0f);
    for (std::size_t l = index + 1; l < layers.size(); ++l) {
        const Layer &layer = layers[l];
        for (std::size_t p = 0; p < pixels; ++p) {
            const float t = layer.transmittance[p];
            a[p] *= t;
            for (std::size_t c = 0; c < 3; ++c)
                b[3 * p + c] = b[3 * p + c] * t + layer.k[c] * (1.0f - t);
        }
    }
}

using LayerProgress = std::function<void(int segments, double lengthMm, std::span<const float> finalRgb)>;

// Builds one layer from scratch in the context (below, a, b). `finalRgb`
// receives the resulting picture (all layers). `lengthLimitMm` <= 0: none.
Layer buildLayer(const Shared &s, int colour, const LinearRgb &k, int budget, const std::vector<float> &belowRgb,
                 const std::vector<float> &a, const std::vector<float> &b, double lengthLimitMm,
                 std::vector<float> &finalRgb, std::stop_token stop, const LayerProgress &progress)
{
    const PinLayout &layout = s.problem.pins;
    const int pinCount = layout.pinCount();
    const float opacity = s.config.threadOpacity;
    const std::span<const float> weights = s.problem.weights;

    Layer layer;
    layer.colour = colour;
    layer.k = k;
    layer.budget = budget;
    layer.transmittance.assign(s.pixels, 1.0f);

    // d = a * (below - k): dF/dT per channel. The layer starts empty (T = 1).
    std::vector<float> d(s.pixels * 3);
    finalRgb.resize(s.pixels * 3);
    std::vector<float> encoded(s.pixels * 3);
    for (std::size_t p = 0; p < s.pixels; ++p) {
        for (std::size_t c = 0; c < 3; ++c) {
            d[3 * p + c] = a[p] * (belowRgb[3 * p + c] - k[c]);
            finalRgb[3 * p + c] = a[p] * belowRgb[3 * p + c] + b[3 * p + c];
            encoded[3 * p + c] = SrgbEncoder::encode(finalRgb[3 * p + c]);
        }
    }
    if (budget <= 0)
        return layer;

    auto score = [&](std::span<const LineSample> line) {
        double sum = 0.0;
        for (const LineSample &sample : line) {
            const std::size_t p = sample.index;
            if (weights[p] <= 0.0f)
                continue;
            const float dt = -layer.transmittance[p] * opacity * sample.coverage;
            float change = 0.0f;
            for (std::size_t c = 0; c < 3; ++c) {
                const float e = encoded[3 * p + c];
                const float next = SrgbEncoder::encode(finalRgb[3 * p + c] + d[3 * p + c] * dt);
                change += (next - e) * (next + e - 2.0f * s.target[c][p]);
            }
            sum += static_cast<double>(weights[p] * change);
        }
        return sum;
    };

    std::vector<bool> used;
    if (!s.config.allowRepeatedLines)
        used.assign(static_cast<std::size_t>(pinCount) * static_cast<std::size_t>(pinCount - 1) / 2, false);
    std::vector<int> candidates;
    std::vector<int> chunkIds;
    std::vector<Candidate> chunkBest;
    std::vector<LineSample> applyScratch;

    int current = s.config.startPin;
    layer.sequence.push_back(current);
    while (static_cast<int>(layer.sequence.size()) - 1 < budget) {
        if (stop.stop_requested()) {
            layer.stopReason = StopReason::Cancelled;
            break;
        }
        candidates.clear();
        for (int j = 0; j < pinCount; ++j) {
            if (layout.separation(current, j) < s.config.minPinSeparation || !layout.canConnect(current, j))
                continue;
            if (!used.empty() && used[pairIndex(current, j, pinCount)])
                continue;
            candidates.push_back(j);
        }
        if (candidates.empty()) {
            layer.stopReason = StopReason::NoValidCandidates;
            break;
        }
        const int chunkCount = std::min<int>(static_cast<int>(candidates.size()), s.threads * 4);
        const std::size_t chunkSize = (candidates.size() + chunkCount - 1) / chunkCount;
        chunkIds.resize(static_cast<std::size_t>(chunkCount));
        std::iota(chunkIds.begin(), chunkIds.end(), 0);
        chunkBest.assign(static_cast<std::size_t>(chunkCount), Candidate{});
        auto scoreChunk = [&](int chunk) {
            thread_local std::vector<LineSample> scratch;
            const std::size_t begin = static_cast<std::size_t>(chunk) * chunkSize;
            const std::size_t end = std::min(candidates.size(), begin + chunkSize);
            Candidate best;
            for (std::size_t i = begin; i < end; ++i) {
                const Candidate c{score(s.cache.line(current, candidates[i], scratch)), candidates[i]};
                if (c.betterThan(best))
                    best = c;
            }
            chunkBest[static_cast<std::size_t>(chunk)] = best;
        };
        if (s.threads > 1 && chunkCount > 1)
            QtConcurrent::blockingMap(&s.pool, chunkIds, scoreChunk);
        else
            std::for_each(chunkIds.begin(), chunkIds.end(), scoreChunk);
        Candidate best;
        for (const Candidate &c : chunkBest) {
            if (c.betterThan(best))
                best = c;
        }
        if (!(best.delta < -s.config.minImprovement)) {
            layer.stopReason = StopReason::NoImprovement;
            break;
        }
        const double lengthMm = layout.distance(current, best.pin) * layout.mmPerUnit();
        if (lengthLimitMm > 0.0 && layer.lengthMm + lengthMm > lengthLimitMm) {
            layer.stopReason = StopReason::MaxThreadLength;
            break;
        }
        for (const LineSample &sample : s.cache.line(current, best.pin, applyScratch)) {
            const std::size_t p = sample.index;
            const float dt = -layer.transmittance[p] * opacity * sample.coverage;
            layer.transmittance[p] += dt;
            for (std::size_t c = 0; c < 3; ++c) {
                finalRgb[3 * p + c] += d[3 * p + c] * dt;
                encoded[3 * p + c] = SrgbEncoder::encode(finalRgb[3 * p + c]);
            }
        }
        if (!used.empty())
            used[pairIndex(current, best.pin, pinCount)] = true;
        layer.lengthMm += lengthMm;
        layer.sequence.push_back(best.pin);
        current = best.pin;
        if (progress)
            progress(static_cast<int>(layer.sequence.size()) - 1, layer.lengthMm, finalRgb);
    }
    if (static_cast<int>(layer.sequence.size()) - 1 >= budget)
        layer.stopReason = StopReason::MaxSegments;
    if (layer.sequence.size() < 2)
        layer.sequence.clear();
    // Exact final picture (removes rounding drift of the incremental updates).
    for (std::size_t p = 0; p < s.pixels; ++p) {
        const float t = layer.transmittance[p];
        for (std::size_t c = 0; c < 3; ++c)
            finalRgb[3 * p + c] = a[p] * (belowRgb[3 * p + c] * t + k[c] * (1.0f - t)) + b[3 * p + c];
    }
    return layer;
}

} // namespace

LayeredColorOptimizer::LayeredColorOptimizer(std::array<std::vector<float>, 3> target, std::vector<QColor> palette,
                                             QColor background, LayeredColorSettings settings)
    : m_target(std::move(target)), m_palette(std::move(palette)), m_background(background), m_settings(settings)
{
}

double LayeredColorOptimizer::weightedSse(std::span<const float> rgb, const std::array<std::vector<float>, 3> &target,
                                          std::span<const float> weights) noexcept
{
    double sum = 0.0;
    for (std::size_t p = 0; p < weights.size(); ++p) {
        if (weights[p] <= 0.0f)
            continue;
        double pixel = 0.0;
        for (std::size_t c = 0; c < 3; ++c) {
            const double d = static_cast<double>(SrgbEncoder::encode(rgb[3 * p + c])) - target[c][p];
            pixel += d * d;
        }
        sum += weights[p] * pixel;
    }
    return sum;
}

std::vector<int> LayeredColorOptimizer::layerOrder(std::span<const QColor> palette, LayerOrder order)
{
    std::vector<int> indices(palette.size());
    std::iota(indices.begin(), indices.end(), 0);
    if (order != LayerOrder::Palette) {
        const bool lightFirst = order != LayerOrder::DarkToLight;
        std::stable_sort(indices.begin(), indices.end(), [&palette, lightFirst](int a, int b) {
            const double la = luminance(toLinearRgb(palette[static_cast<std::size_t>(a)]));
            const double lb = luminance(toLinearRgb(palette[static_cast<std::size_t>(b)]));
            return lightFirst ? la > lb : la < lb;
        });
    }
    return indices;
}

std::vector<std::vector<float>> LayeredColorOptimizer::separate(const std::array<std::vector<float>, 3> &target,
                                                                std::span<const float> weights, int width,
                                                                int height, std::span<const QColor> palette,
                                                                const QColor &background, double blurRadius)
{
    const std::size_t pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (const auto &plane : target) {
        if (plane.size() != pixels)
            throw std::invalid_argument("target planes do not match the canvas size");
    }
    if (weights.size() != pixels)
        throw std::invalid_argument("weights do not match the canvas size");

    // Choices: the palette, then the board.
    std::vector<LinearRgb> choices;
    for (const QColor &c : palette)
        choices.push_back(toLinearRgb(c));
    choices.push_back(toLinearRgb(background));
    const int board = static_cast<int>(palette.size());

    std::vector<std::vector<float>> density(palette.size(), std::vector<float>(pixels, 0.0f));
    // Error rows (3 channels) for the current and the next line, padded by one pixel.
    std::vector<float> current(static_cast<std::size_t>(width + 2) * 3, 0.0f);
    std::vector<float> next(current.size(), 0.0f);
    for (int y = 0; y < height; ++y) {
        const bool leftToRight = y % 2 == 0;
        std::fill(next.begin(), next.end(), 0.0f);
        for (int step = 0; step < width; ++step) {
            const int x = leftToRight ? step : width - 1 - step;
            const std::size_t p = static_cast<std::size_t>(y) * width + x;
            float *error = &current[static_cast<std::size_t>(x + 1) * 3];
            if (weights[p] <= 0.0f) {
                error[0] = error[1] = error[2] = 0.0f; // outside the frame: board, no diffusion
                continue;
            }
            float want[3];
            for (std::size_t c = 0; c < 3; ++c)
                want[c] = srgbToLinear(target[c][p]) + error[c];
            int best = board;
            float bestDistance = std::numeric_limits<float>::max();
            for (int k = 0; k <= board; ++k) {
                const LinearRgb &choice = choices[static_cast<std::size_t>(k)];
                float dist = 0.0f;
                for (std::size_t c = 0; c < 3; ++c)
                    dist += (want[c] - choice[c]) * (want[c] - choice[c]);
                if (dist < bestDistance) {
                    bestDistance = dist;
                    best = k;
                }
            }
            if (best != board)
                density[static_cast<std::size_t>(best)][p] = 1.0f;
            const int forward = leftToRight ? 1 : -1;
            for (std::size_t c = 0; c < 3; ++c) {
                const float e = want[c] - choices[static_cast<std::size_t>(best)][c];
                current[static_cast<std::size_t>(x + 1 + forward) * 3 + c] += e * 7.0f / 16.0f;
                next[static_cast<std::size_t>(x + 1 - forward) * 3 + c] += e * 3.0f / 16.0f;
                next[static_cast<std::size_t>(x + 1) * 3 + c] += e * 5.0f / 16.0f;
                next[static_cast<std::size_t>(x + 1 + forward) * 3 + c] += e * 1.0f / 16.0f;
            }
        }
        std::swap(current, next);
    }
    const int radius = static_cast<int>(std::lround(blurRadius));
    for (auto &plane : density)
        boxBlur(plane, width, height, radius);
    return density;
}

StringArtResult LayeredColorOptimizer::optimize(const OptimizationProblem &problem, const OptimizationConfig &config,
                                                std::stop_token stop, const ProgressCallback &progress)
{
    const PinLayout &layout = problem.pins;
    validateConfig(config, layout.pinCount());
    const std::size_t pixels = static_cast<std::size_t>(problem.width) * static_cast<std::size_t>(problem.height);
    if (problem.width <= 0 || problem.height <= 0 || problem.weights.size() != pixels
        || layout.canvasWidth() != problem.width || layout.canvasHeight() != problem.height)
        throw std::invalid_argument("problem size, weights and pin canvas must agree");
    if (m_palette.empty() || static_cast<int>(m_palette.size()) > kMaxColors)
        throw std::invalid_argument("the palette needs 1 to 8 colours");
    if (!m_background.isValid())
        throw std::invalid_argument("invalid background colour");
    if (m_settings.refinePasses < 0)
        throw std::invalid_argument("refinePasses must not be negative");
    // A single layer has no other layers to adapt to.
    const int refinePasses = m_palette.size() > 1 ? m_settings.refinePasses : 0;

    const auto start = std::chrono::steady_clock::now();
    auto elapsed = [&start] {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    };
    const int threads = config.threadCount > 0 ? config.threadCount : std::max(1, QThread::idealThreadCount());
    QThreadPool pool;
    pool.setMaxThreadCount(threads);
    const LineCache cache(layout, config.threadWidth, problem.width, problem.height, config.minPinSeparation,
                          config.lineCacheBudgetBytes, &pool);
    const Shared shared{problem, config, m_target, cache, pool, threads, pixels};

    // Segment budgets from the colour separation.
    const std::vector<std::vector<float>> density = separate(m_target, problem.weights, problem.width,
                                                             problem.height, m_palette, m_background,
                                                             m_settings.blurRadius);
    std::vector<double> mass(m_palette.size(), 0.0);
    for (std::size_t c = 0; c < m_palette.size(); ++c) {
        for (std::size_t p = 0; p < pixels; ++p)
            mass[c] += problem.weights[p] > 0.0f ? density[c][p] : 0.0f;
    }
    const double totalMass = std::accumulate(mass.begin(), mass.end(), 0.0);
    auto budgetOf = [&](int colour) {
        if (totalMass <= 0.0)
            return 0;
        const double share = mass[static_cast<std::size_t>(colour)] / totalMass;
        return static_cast<int>(std::lround(config.maxSegments * share));
    };

    const double weightSum = std::accumulate(problem.weights.begin(), problem.weights.end(), 0.0);
    const double normalizer = weightSum > 0.0 ? 1.0 / (3.0 * weightSum) : 0.0;
    const LinearRgb board = toLinearRgb(m_background);
    auto errorOf = [&](std::span<const float> rgb) {
        return weightedSse(rgb, m_target, problem.weights) * normalizer;
    };
    std::vector<float> empty = below({}, 0, board, pixels);
    const double initialError = errorOf(empty);

    auto lastPreview = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    auto report = [&](int segments, double lengthMm, std::span<const float> rgb, bool force) {
        if (!progress)
            return;
        const auto now = std::chrono::steady_clock::now();
        if (!force && now - lastPreview < std::chrono::milliseconds(100))
            return;
        lastPreview = now;
        OptimizationProgress q;
        q.segments = segments;
        q.maxSegments = config.maxSegments;
        q.error = errorOf(rgb);
        q.initialError = initialError;
        q.threadLengthMm = lengthMm;
        q.elapsedSeconds = elapsed();
        q.width = problem.width;
        q.height = problem.height;
        q.rgb = rgb;
        progress(q);
    };
    auto totals = [](const std::vector<Layer> &layers, std::size_t skip) {
        std::pair<int, double> t{0, 0.0};
        for (std::size_t l = 0; l < layers.size(); ++l) {
            if (l == skip || layers[l].sequence.size() < 2)
                continue;
            t.first += static_cast<int>(layers[l].sequence.size()) - 1;
            t.second += layers[l].lengthMm;
        }
        return t;
    };

    // Builds and refines the layers in one order; returns them and the picture.
    std::vector<float> scratchA;
    std::vector<float> scratchB;
    auto run = [&](const std::vector<int> &order, std::vector<float> &picture, double &error) {
        std::vector<Layer> layers;
        for (int colour : order) {
            Layer layer;
            layer.colour = colour;
            layer.k = toLinearRgb(m_palette[static_cast<std::size_t>(colour)]);
            // Without refinement nothing would use a held-back share.
            const double share = refinePasses > 0 ? m_settings.initialShare : 1.0;
            layer.budget = static_cast<int>(std::lround(share * budgetOf(colour)));
            layer.transmittance.assign(pixels, 1.0f);
            layers.push_back(std::move(layer));
        }
        auto rebuild = [&](std::size_t index, std::vector<float> &out, bool flexible) {
            const std::vector<float> under = below(layers, index, board, pixels);
            above(layers, index, pixels, scratchA, scratchB);
            const auto [otherSegments, otherLength] = totals(layers, index);
            const double limit = config.maxThreadLengthMm > 0.0 ? config.maxThreadLengthMm - otherLength : 0.0;
            if (config.maxThreadLengthMm > 0.0 && limit <= 0.0) {
                Layer none = layers[index];
                none.sequence.clear();
                none.transmittance.assign(pixels, 1.0f);
                none.lengthMm = 0.0;
                none.stopReason = StopReason::MaxThreadLength;
                return none;
            }
            const Layer &old = layers[index];
            // First build: the layer's share of the budget; refinement: any
            // segments the other layers leave unused.
            const int budget = flexible ? config.maxSegments - otherSegments : old.budget;
            return buildLayer(shared, old.colour, old.k, budget, under, scratchA, scratchB, limit, out, stop,
                              [&](int segments, double lengthMm, std::span<const float> rgb) {
                                  report(otherSegments + segments, otherLength + lengthMm, rgb, false);
                              });
        };
        // Bottom to top; layers above are still empty.
        for (std::size_t i = 0; i < layers.size() && !stop.stop_requested(); ++i)
            layers[i] = rebuild(i, picture, false);
        error = errorOf(picture);
        // Block coordinate descent: rebuild each layer with the others fixed.
        std::vector<float> candidate;
        for (int pass = 0; pass < refinePasses && !stop.stop_requested(); ++pass) {
            bool improved = false;
            for (std::size_t i = 0; i < layers.size() && !stop.stop_requested(); ++i) {
                Layer rebuilt = rebuild(i, candidate, true);
                if (rebuilt.stopReason == StopReason::Cancelled)
                    break;
                const double e = errorOf(candidate);
                if (e < error) {
                    error = e;
                    layers[i] = std::move(rebuilt);
                    picture.swap(candidate);
                    improved = true;
                }
            }
            if (!improved)
                break;
        }
        return layers;
    };

    std::vector<std::vector<int>> orders;
    if (m_settings.order == LayerOrder::Auto) {
        orders.push_back(layerOrder(m_palette, LayerOrder::LightToDark));
        orders.push_back(layerOrder(m_palette, LayerOrder::DarkToLight));
        if (orders[1] == orders[0])
            orders.pop_back();
    } else {
        orders.push_back(layerOrder(m_palette, m_settings.order));
    }
    std::vector<Layer> bestLayers;
    std::vector<float> bestPicture = empty;
    double bestError = std::numeric_limits<double>::infinity();
    for (const std::vector<int> &order : orders) {
        if (stop.stop_requested() && !bestLayers.empty())
            break;
        std::vector<float> picture = empty;
        double error = initialError;
        std::vector<Layer> layers = run(order, picture, error);
        if (error < bestError) {
            bestError = error;
            bestLayers = std::move(layers);
            bestPicture = std::move(picture);
        }
    }

    StringArtResult result;
    result.width = problem.width;
    result.height = problem.height;
    result.lineCachePrecomputed = cache.isPrecomputed();
    result.lineCacheBytes = cache.memoryBytes();
    result.initialError = initialError;
    result.rgb = std::move(bestPicture);
    result.stopReason = StopReason::NoImprovement;
    for (const Layer &layer : bestLayers) {
        for (std::size_t i = 0; i + 1 < layer.sequence.size(); ++i)
            result.colorSteps.push_back({layer.colour, layer.sequence[i], layer.sequence[i + 1]});
        result.threadLengthMm += layer.lengthMm;
        if (layer.stopReason == StopReason::MaxThreadLength)
            result.stopReason = StopReason::MaxThreadLength;
    }
    if (stop.stop_requested())
        result.stopReason = StopReason::Cancelled;
    else if (result.stopReason == StopReason::NoImprovement
             && static_cast<int>(result.colorSteps.size()) >= config.maxSegments)
        result.stopReason = StopReason::MaxSegments;
    result.finalError = errorOf(result.rgb);
    result.elapsedSeconds = elapsed();
    report(static_cast<int>(result.colorSteps.size()), result.threadLengthMm, result.rgb, true);
    return result;
}

} // namespace sa
