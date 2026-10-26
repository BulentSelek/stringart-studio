#include "algorithms/GreedyOptimizer.h"

#include "core/Compositing.h"
#include "core/LineCache.h"

#include <QThread>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentMap>

#include <algorithm>
#include <chrono>
#include <limits>
#include <numeric>
#include <vector>

namespace sa {

namespace {

struct Candidate
{
    double delta = std::numeric_limits<double>::infinity();
    int pin = -1;

    // Strict ordering with deterministic tie-breaking on the pin index.
    [[nodiscard]] bool betterThan(const Candidate &other) const noexcept
    {
        if (pin < 0)
            return false;
        if (other.pin < 0)
            return true;
        return delta < other.delta || (delta == other.delta && pin < other.pin);
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

double GreedyOptimizer::scoreLine(std::span<const LineSample> line, std::span<const float> transmittance,
                                  std::span<const float> encoded, std::span<const float> target,
                                  std::span<const float> weights, float opacity) noexcept
{
    double sum = 0.0;
    for (const LineSample &s : line) {
        const float e = encoded[s.index];
        const float next = SrgbEncoder::encode(applyThread(transmittance[s.index], opacity, s.coverage));
        // (next - g)^2 - (e - g)^2 in factored form to avoid cancellation.
        const float change = (next - e) * (next + e - 2.0f * target[s.index]);
        sum += static_cast<double>(weights[s.index] * change);
    }
    return sum;
}

double GreedyOptimizer::weightedSse(std::span<const float> transmittance, std::span<const float> target,
                                    std::span<const float> weights) noexcept
{
    double sum = 0.0;
    for (std::size_t i = 0; i < transmittance.size(); ++i) {
        const double d = static_cast<double>(SrgbEncoder::encode(transmittance[i])) - target[i];
        sum += weights[i] * d * d;
    }
    return sum;
}

StringArtResult GreedyOptimizer::optimize(const OptimizationProblem &problem, const OptimizationConfig &config,
                                          std::stop_token stop, const ProgressCallback &progress)
{
    problem.validate();
    const PinLayout &layout = problem.pins;
    const int pinCount = layout.pinCount();
    validateConfig(config, pinCount);

    const auto start = std::chrono::steady_clock::now();
    auto elapsed = [&start] {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    };

    const int threads = config.threadCount > 0 ? config.threadCount : std::max(1, QThread::idealThreadCount());
    QThreadPool pool;
    pool.setMaxThreadCount(threads);

    const LineCache cache(layout, config.threadWidth, problem.width, problem.height, config.minPinSeparation,
                          config.lineCacheBudgetBytes, &pool);

    const double weightSum = std::accumulate(problem.weights.begin(), problem.weights.end(), 0.0);
    const double normalizer = weightSum > 0.0 ? 1.0 / weightSum : 0.0;

    StringArtResult result;
    result.width = problem.width;
    result.height = problem.height;
    result.lineCachePrecomputed = cache.isPrecomputed();
    result.lineCacheBytes = cache.memoryBytes();
    result.transmittance.assign(problem.target.size(), 1.0f);
    result.sequence.reserve(static_cast<std::size_t>(config.maxSegments) + 1);
    result.sequence.push_back(config.startPin);

    std::vector<float> &transmittance = result.transmittance;
    std::vector<float> encoded(transmittance.size(), SrgbEncoder::encode(1.0f));
    const std::span<const float> target = problem.target;
    const std::span<const float> weights = problem.weights;

    double sse = weightedSse(transmittance, target, weights);
    result.initialError = sse * normalizer;

    std::vector<bool> usedPairs;
    if (!config.allowRepeatedLines)
        usedPairs.assign(static_cast<std::size_t>(pinCount) * static_cast<std::size_t>(pinCount - 1) / 2, false);

    auto report = [&] {
        if (!progress)
            return;
        OptimizationProgress p;
        p.segments = result.segmentCount();
        p.maxSegments = config.maxSegments;
        p.error = sse * normalizer;
        p.initialError = result.initialError;
        p.threadLengthMm = result.threadLengthMm;
        p.elapsedSeconds = elapsed();
        p.width = problem.width;
        p.height = problem.height;
        p.transmittance = transmittance;
        p.sequence = result.sequence;
        progress(p);
    };

    std::vector<int> candidates;
    candidates.reserve(static_cast<std::size_t>(pinCount));
    std::vector<int> chunkIds;
    std::vector<Candidate> chunkBest;
    std::vector<LineSample> applyScratch;

    int current = config.startPin;
    result.stopReason = StopReason::MaxSegments;
    while (result.segmentCount() < config.maxSegments) {
        if (stop.stop_requested()) {
            result.stopReason = StopReason::Cancelled;
            break;
        }

        candidates.clear();
        for (int j = 0; j < pinCount; ++j) {
            if (layout.separation(current, j) < config.minPinSeparation || !layout.canConnect(current, j))
                continue;
            if (!usedPairs.empty() && usedPairs[pairIndex(current, j, pinCount)])
                continue;
            candidates.push_back(j);
        }
        if (candidates.empty()) {
            result.stopReason = StopReason::NoValidCandidates;
            break;
        }

        // Score candidates in contiguous chunks; each chunk keeps its best.
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
            for (std::size_t k = begin; k < end; ++k) {
                const int pin = candidates[k];
                const Candidate c{scoreLine(cache.line(current, pin, scratch), transmittance, encoded, target,
                                            weights, config.threadOpacity),
                                  pin};
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
        const double lengthMm = layout.distance(current, best.pin) * layout.mmPerUnit();
        if (config.maxThreadLengthMm > 0.0 && result.threadLengthMm + lengthMm > config.maxThreadLengthMm) {
            result.stopReason = StopReason::MaxThreadLength;
            break;
        }

        for (const LineSample &s : cache.line(current, best.pin, applyScratch)) {
            transmittance[s.index] = applyThread(transmittance[s.index], config.threadOpacity, s.coverage);
            encoded[s.index] = SrgbEncoder::encode(transmittance[s.index]);
        }
        sse += best.delta;
        if (!usedPairs.empty())
            usedPairs[pairIndex(current, best.pin, pinCount)] = true;
        result.threadLengthMm += lengthMm;
        result.sequence.push_back(best.pin);
        current = best.pin;
        report();
    }

    // Recompute exactly to remove accumulated rounding from incremental updates.
    sse = weightedSse(transmittance, target, weights);
    result.finalError = sse * normalizer;
    result.elapsedSeconds = elapsed();
    report();
    return result;
}

} // namespace sa
