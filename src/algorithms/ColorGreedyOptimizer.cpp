#include "algorithms/ColorGreedyOptimizer.h"

#include "core/Compositing.h"
#include "core/LineCache.h"

#include <QThread>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentMap>

#include <algorithm>
#include <chrono>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace sa {

namespace {

struct Candidate
{
    double delta = std::numeric_limits<double>::infinity();
    int color = -1;
    int pin = -1;

    [[nodiscard]] bool betterThan(const Candidate &other) const noexcept
    {
        if (pin < 0)
            return false;
        if (other.pin < 0)
            return true;
        if (delta != other.delta)
            return delta < other.delta;
        return color < other.color || (color == other.color && pin < other.pin);
    }
};

std::size_t pairIndex(int a, int b, int n) noexcept
{
    if (a > b)
        std::swap(a, b);
    const auto i = static_cast<std::size_t>(a);
    return i * static_cast<std::size_t>(n) - i * (i + 1) / 2 + static_cast<std::size_t>(b - a - 1);
}

} // namespace

ColorGreedyOptimizer::ColorGreedyOptimizer(std::array<std::vector<float>, 3> target, std::vector<QColor> palette,
                                           QColor background)
    : m_target(std::move(target)), m_palette(std::move(palette)), m_background(background)
{
}

double ColorGreedyOptimizer::weightedSse(std::span<const float> rgb, const std::array<std::vector<float>, 3> &target,
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

StringArtResult ColorGreedyOptimizer::optimize(const OptimizationProblem &problem, const OptimizationConfig &config,
                                               std::stop_token stop, const ProgressCallback &progress)
{
    const PinLayout &layout = problem.pins;
    const int pinCount = layout.pinCount();
    validateConfig(config, pinCount);
    const std::size_t pixels = static_cast<std::size_t>(problem.width) * static_cast<std::size_t>(problem.height);
    if (problem.width <= 0 || problem.height <= 0 || problem.weights.size() != pixels
        || layout.canvasWidth() != problem.width || layout.canvasHeight() != problem.height)
        throw std::invalid_argument("problem size, weights and pin canvas must agree");
    for (const auto &plane : m_target) {
        if (plane.size() != pixels)
            throw std::invalid_argument("colour target does not match the image size");
    }
    if (m_palette.empty() || static_cast<int>(m_palette.size()) > kMaxColors)
        throw std::invalid_argument("the palette must contain 1 to 8 colours");

    const auto start = std::chrono::steady_clock::now();
    auto elapsed = [&start] {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    };
    const int threads = config.threadCount > 0 ? config.threadCount : std::max(1, QThread::idealThreadCount());
    QThreadPool pool;
    pool.setMaxThreadCount(threads);
    const LineCache cache(layout, config.threadWidth, problem.width, problem.height, config.minPinSeparation,
                          config.lineCacheBudgetBytes, &pool);

    const int colorCount = static_cast<int>(m_palette.size());
    std::vector<LinearRgb> colors;
    for (const QColor &c : m_palette)
        colors.push_back(toLinearRgb(c));
    const LinearRgb background = toLinearRgb(m_background);
    const std::span<const float> weights = problem.weights;
    const double weightSum = std::accumulate(problem.weights.begin(), problem.weights.end(), 0.0);
    const double normalizer = weightSum > 0.0 ? 1.0 / (3.0 * weightSum) : 0.0;
    const float opacity = config.threadOpacity;

    StringArtResult result;
    result.width = problem.width;
    result.height = problem.height;
    result.lineCachePrecomputed = cache.isPrecomputed();
    result.lineCacheBytes = cache.memoryBytes();
    std::vector<float> &rgb = result.rgb;
    rgb.resize(pixels * 3);
    for (std::size_t p = 0; p < pixels; ++p)
        std::copy(background.begin(), background.end(), rgb.begin() + static_cast<std::ptrdiff_t>(3 * p));
    std::vector<float> encoded(rgb.size());
    std::transform(rgb.begin(), rgb.end(), encoded.begin(), [](float v) { return SrgbEncoder::encode(v); });

    double sse = weightedSse(rgb, m_target, weights);
    result.initialError = sse * normalizer;

    const std::size_t pairCount = static_cast<std::size_t>(pinCount) * static_cast<std::size_t>(pinCount - 1) / 2;
    std::vector<std::vector<bool>> used(static_cast<std::size_t>(colorCount));
    if (!config.allowRepeatedLines) {
        for (auto &u : used)
            u.assign(pairCount, false);
    }
    std::vector<int> current(static_cast<std::size_t>(colorCount), config.startPin);

    auto report = [&] {
        if (!progress)
            return;
        OptimizationProgress p;
        p.segments = static_cast<int>(result.colorSteps.size());
        p.maxSegments = config.maxSegments;
        p.error = sse * normalizer;
        p.initialError = result.initialError;
        p.threadLengthMm = result.threadLengthMm;
        p.elapsedSeconds = elapsed();
        p.width = problem.width;
        p.height = problem.height;
        p.rgb = rgb;
        progress(p);
    };

    // Exact change of S for drawing `line` in colour k.
    auto score = [&](std::span<const LineSample> line, const LinearRgb &k) {
        double sum = 0.0;
        for (const LineSample &s : line) {
            const float beta = opacity * s.coverage;
            const std::size_t base = 3 * static_cast<std::size_t>(s.index);
            float change = 0.0f;
            for (std::size_t c = 0; c < 3; ++c) {
                const float e = encoded[base + c];
                const float next = SrgbEncoder::encode(rgb[base + c] + (k[c] - rgb[base + c]) * beta);
                change += (next - e) * (next + e - 2.0f * m_target[c][s.index]);
            }
            sum += static_cast<double>(weights[s.index] * change);
        }
        return sum;
    };

    struct Move
    {
        int color;
        int pin;
    };
    std::vector<Move> candidates;
    std::vector<int> chunkIds;
    std::vector<Candidate> chunkBest;
    std::vector<LineSample> applyScratch;

    result.stopReason = StopReason::MaxSegments;
    while (static_cast<int>(result.colorSteps.size()) < config.maxSegments) {
        if (stop.stop_requested()) {
            result.stopReason = StopReason::Cancelled;
            break;
        }
        candidates.clear();
        for (int c = 0; c < colorCount; ++c) {
            const int from = current[static_cast<std::size_t>(c)];
            for (int j = 0; j < pinCount; ++j) {
                if (layout.separation(from, j) < config.minPinSeparation || !layout.canConnect(from, j))
                    continue;
                if (!config.allowRepeatedLines && used[static_cast<std::size_t>(c)][pairIndex(from, j, pinCount)])
                    continue;
                candidates.push_back({c, j});
            }
        }
        if (candidates.empty()) {
            result.stopReason = StopReason::NoValidCandidates;
            break;
        }

        const int chunkCount = std::min<int>(static_cast<int>(candidates.size()), threads * 4);
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
                const Move &m = candidates[i];
                const int from = current[static_cast<std::size_t>(m.color)];
                const Candidate c{score(cache.line(from, m.pin, scratch), colors[static_cast<std::size_t>(m.color)]),
                                  m.color, m.pin};
                if (c.betterThan(best))
                    best = c;
            }
            chunkBest[static_cast<std::size_t>(chunk)] = best;
        };
        if (threads > 1 && chunkCount > 1)
            QtConcurrent::blockingMap(&pool, chunkIds, scoreChunk);
        else
            std::for_each(chunkIds.begin(), chunkIds.end(), scoreChunk);

        Candidate best;
        for (const Candidate &c : chunkBest) {
            if (c.betterThan(best))
                best = c;
        }
        if (!(best.delta < -config.minImprovement)) {
            result.stopReason = StopReason::NoImprovement;
            break;
        }
        const int from = current[static_cast<std::size_t>(best.color)];
        const double lengthMm = layout.distance(from, best.pin) * layout.mmPerUnit();
        if (config.maxThreadLengthMm > 0.0 && result.threadLengthMm + lengthMm > config.maxThreadLengthMm) {
            result.stopReason = StopReason::MaxThreadLength;
            break;
        }

        const LinearRgb &k = colors[static_cast<std::size_t>(best.color)];
        for (const LineSample &s : cache.line(from, best.pin, applyScratch)) {
            float *pixel = &rgb[3 * static_cast<std::size_t>(s.index)];
            applyThreadOver(pixel, k, opacity * s.coverage);
            for (std::size_t c = 0; c < 3; ++c)
                encoded[3 * static_cast<std::size_t>(s.index) + c] = SrgbEncoder::encode(pixel[c]);
        }
        sse += best.delta;
        if (!config.allowRepeatedLines)
            used[static_cast<std::size_t>(best.color)][pairIndex(from, best.pin, pinCount)] = true;
        result.colorSteps.push_back({best.color, from, best.pin});
        result.threadLengthMm += lengthMm;
        current[static_cast<std::size_t>(best.color)] = best.pin;
        report();
    }

    sse = weightedSse(rgb, m_target, weights);
    result.finalError = sse * normalizer;
    result.elapsedSeconds = elapsed();
    report();
    return result;
}

} // namespace sa
