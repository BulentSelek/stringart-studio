#include "algorithms/SequenceOptimizer.h"

#include "algorithms/GreedyOptimizer.h"
#include "core/Compositing.h"
#include "core/LineCache.h"
#include "core/ThreadRenderer.h"

#include <QThread>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentMap>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <unordered_map>

namespace sa {

namespace {

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

// Per-thread sparse factor map over the canvas: factor[p] multiplies T[p].
struct FactorMap
{
    std::vector<float> factor;
    std::vector<std::uint8_t> marked;
    std::vector<std::uint32_t> touched;

    void ensure(std::size_t size)
    {
        if (factor.size() != size) {
            factor.assign(size, 1.0f);
            marked.assign(size, 0);
            touched.clear();
        }
    }

    // Removing a segment divides by (1 - a c); adding multiplies.
    void apply(std::span<const LineSample> line, float opacity, bool remove)
    {
        for (const LineSample &s : line) {
            if (!marked[s.index]) {
                marked[s.index] = 1;
                touched.push_back(s.index);
            }
            const float f = 1.0f - opacity * s.coverage;
            factor[s.index] = remove ? factor[s.index] / f : factor[s.index] * f;
        }
    }

    void reset()
    {
        for (std::uint32_t i : touched) {
            factor[i] = 1.0f;
            marked[i] = 0;
        }
        touched.clear();
    }
};

enum class MoveKind { Relocate, Delete, Insert };

struct Move
{
    MoveKind kind;
    int pin; // new pin for Relocate / Insert
};

struct Pair
{
    int a;
    int b;
};

std::uint64_t pairKey(int a, int b, int n)
{
    if (a > b)
        std::swap(a, b);
    return static_cast<std::uint64_t>(a) * static_cast<std::uint64_t>(n) + static_cast<std::uint64_t>(b);
}

} // namespace

SequenceOptimizer::SequenceOptimizer(std::vector<int> initialSequence, SequenceRefinementSettings settings)
    : m_initial(std::move(initialSequence)), m_settings(settings)
{
}

StringArtResult SequenceOptimizer::optimize(const OptimizationProblem &problem, const OptimizationConfig &config,
                                            std::stop_token stop, const ProgressCallback &progress)
{
    problem.validate();
    const PinLayout &layout = problem.pins;
    const int pinCount = layout.pinCount();
    validateConfig(config, pinCount);
    if (config.threadOpacity >= 0.999f)
        throw std::invalid_argument("sequence refinement requires thread opacity below 1");
    if (m_settings.maxPasses < 0 || m_settings.neighborhood < 0)
        throw std::invalid_argument("refinement passes and neighbourhood must not be negative");
    if (m_initial.empty())
        throw std::invalid_argument("refinement needs an initial sequence");

    auto valid = [&](int a, int b) {
        return a >= 0 && b >= 0 && a < pinCount && b < pinCount && layout.canConnect(a, b)
            && layout.separation(a, b) >= config.minPinSeparation;
    };
    std::unordered_map<std::uint64_t, int> pairCount;
    for (std::size_t i = 0; i < m_initial.size(); ++i) {
        if (m_initial[i] < 0 || m_initial[i] >= pinCount)
            throw std::invalid_argument("initial sequence references a pin outside the layout");
        if (i > 0) {
            if (!valid(m_initial[i - 1], m_initial[i]))
                throw std::invalid_argument("initial sequence violates the connection rules");
            ++pairCount[pairKey(m_initial[i - 1], m_initial[i], pinCount)];
        }
    }

    const auto start = std::chrono::steady_clock::now();
    auto elapsed = [&start] {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    };
    const int threads = config.threadCount > 0 ? config.threadCount : std::max(1, QThread::idealThreadCount());
    QThreadPool pool;
    pool.setMaxThreadCount(threads);
    const LineCache cache(layout, config.threadWidth, problem.width, problem.height, config.minPinSeparation,
                          config.lineCacheBudgetBytes, &pool);

    const std::span<const float> target = problem.target;
    const std::span<const float> weights = problem.weights;
    const double weightSum = std::accumulate(problem.weights.begin(), problem.weights.end(), 0.0);
    const double normalizer = weightSum > 0.0 ? 1.0 / weightSum : 0.0;
    const float opacity = config.threadOpacity;
    const double mm = layout.mmPerUnit();

    StringArtResult result;
    result.width = problem.width;
    result.height = problem.height;
    result.lineCachePrecomputed = cache.isPrecomputed();
    result.lineCacheBytes = cache.memoryBytes();
    std::vector<int> &sequence = result.sequence;
    sequence = m_initial;

    std::vector<float> &transmittance = result.transmittance;
    std::vector<float> encoded;
    double sse = 0.0;
    auto rerender = [&] {
        transmittance = ThreadRenderer::renderTransmittance(layout, sequence, config.threadWidth, opacity);
        encoded.resize(transmittance.size());
        std::transform(transmittance.begin(), transmittance.end(), encoded.begin(),
                       [](float t) { return SrgbEncoder::encode(t); });
        sse = GreedyOptimizer::weightedSse(transmittance, target, weights);
    };
    rerender();
    result.initialError = sse * normalizer;

    double length = 0.0;
    for (std::size_t i = 1; i < sequence.size(); ++i)
        length += layout.distance(sequence[i - 1], sequence[i]) * mm;

    // Exact change of S when `factors` is applied to T.
    auto scoreFactors = [&](const FactorMap &map) {
        double sum = 0.0;
        for (std::uint32_t i : map.touched) {
            const float e = encoded[i];
            const float next = SrgbEncoder::encode(transmittance[i] * map.factor[i]);
            sum += static_cast<double>(weights[i] * (next - e) * (next + e - 2.0f * target[i]));
        }
        return sum;
    };

    auto report = [&](int position, int total) {
        if (!progress)
            return;
        OptimizationProgress p;
        p.segments = position;
        p.maxSegments = total;
        p.error = sse * normalizer;
        p.initialError = result.initialError;
        p.threadLengthMm = length;
        p.elapsedSeconds = elapsed();
        p.width = problem.width;
        p.height = problem.height;
        p.transmittance = transmittance;
        p.sequence = sequence;
        progress(p);
    };

    // Accept only clear improvements so rounding cannot cause cycling.
    const double minGain = std::max(config.minImprovement, 1e-9 * std::max(1.0, weightSum));
    const bool resize = m_settings.changeSegmentCount;
    std::vector<Move> moves;
    std::vector<int> chunkIds;
    std::vector<Candidate> chunkBest;
    FactorMap applyMap;
    std::vector<LineSample> scratch;

    // Pairs removed and added by a move at position i (sequence[i] is b).
    auto edit = [&](const Move &move, std::size_t i, Pair *removed, int &removedCount, Pair *added,
                    int &addedCount) {
        const int a = sequence[i - 1];
        const int b = sequence[i];
        const int c = i + 1 < sequence.size() ? sequence[i + 1] : -1;
        removedCount = 0;
        addedCount = 0;
        switch (move.kind) {
        case MoveKind::Relocate:
            removed[removedCount++] = {a, b};
            added[addedCount++] = {a, move.pin};
            if (c >= 0) {
                removed[removedCount++] = {b, c};
                added[addedCount++] = {move.pin, c};
            }
            break;
        case MoveKind::Delete:
            removed[removedCount++] = {a, b};
            removed[removedCount++] = {b, c};
            added[addedCount++] = {a, c};
            break;
        case MoveKind::Insert:
            removed[removedCount++] = {a, b};
            added[addedCount++] = {a, move.pin};
            added[addedCount++] = {move.pin, b};
            break;
        }
    };

    // Connection, repeat and length rules for a move.
    auto allowed = [&](const Move &move, std::size_t i) {
        Pair removed[2];
        Pair added[2];
        int nr = 0;
        int na = 0;
        edit(move, i, removed, nr, added, na);
        double delta = 0.0;
        for (int r = 0; r < nr; ++r)
            delta -= layout.distance(removed[r].a, removed[r].b) * mm;
        for (int j = 0; j < na; ++j) {
            if (!valid(added[j].a, added[j].b))
                return false;
            delta += layout.distance(added[j].a, added[j].b) * mm;
            if (!config.allowRepeatedLines) {
                const std::uint64_t key = pairKey(added[j].a, added[j].b, pinCount);
                const auto it = pairCount.find(key);
                int uses = it == pairCount.end() ? 0 : it->second;
                for (int r = 0; r < nr; ++r)
                    uses -= key == pairKey(removed[r].a, removed[r].b, pinCount);
                for (int o = 0; o < j; ++o)
                    uses += key == pairKey(added[o].a, added[o].b, pinCount);
                if (uses > 0)
                    return false;
            }
        }
        return !(config.maxThreadLengthMm > 0.0 && length + delta > config.maxThreadLengthMm);
    };

    auto applyToMap = [&](FactorMap &map, const Move &move, std::size_t i, std::vector<LineSample> &buffer) {
        Pair removed[2];
        Pair added[2];
        int nr = 0;
        int na = 0;
        edit(move, i, removed, nr, added, na);
        for (int r = 0; r < nr; ++r)
            map.apply(cache.line(removed[r].a, removed[r].b, buffer), opacity, true);
        for (int j = 0; j < na; ++j)
            map.apply(cache.line(added[j].a, added[j].b, buffer), opacity, false);
    };

    result.stopReason = StopReason::MaxPasses;
    m_passes = 0;
    m_improvements = 0;
    bool cancelled = false;
    for (int pass = 0; pass < m_settings.maxPasses && !cancelled && sequence.size() > 1; ++pass) {
        ++m_passes;
        int improvedThisPass = 0;
        for (std::size_t i = 1; i < sequence.size(); ++i) {
            if (stop.stop_requested()) {
                cancelled = true;
                break;
            }
            const int b = sequence[i];
            const int segments = static_cast<int>(sequence.size()) - 1;

            moves.clear();
            auto consider = [&](Move move) {
                if (allowed(move, i))
                    moves.push_back(move);
            };
            const int k = m_settings.neighborhood;
            const bool allPins = k == 0 || 2 * k >= pinCount - 1;
            for (int d = allPins ? 0 : -k; d <= (allPins ? pinCount - 1 : k); ++d) {
                const int p = allPins ? d : ((b + d) % pinCount + pinCount) % pinCount;
                if (p != b)
                    consider({MoveKind::Relocate, p});
            }
            if (resize) {
                if (i + 1 < sequence.size() && segments > 1)
                    consider({MoveKind::Delete, -1});
                if (segments < config.maxSegments) {
                    for (int p = 0; p < pinCount; ++p)
                        consider({MoveKind::Insert, p});
                }
            }
            if (moves.empty())
                continue;

            const int chunkCount = std::min<int>(static_cast<int>(moves.size()), threads * 2);
            const std::size_t chunkSize = (moves.size() + chunkCount - 1) / chunkCount;
            chunkIds.resize(static_cast<std::size_t>(chunkCount));
            std::iota(chunkIds.begin(), chunkIds.end(), 0);
            chunkBest.assign(static_cast<std::size_t>(chunkCount), Candidate{});
            auto scoreChunk = [&](int chunk) {
                thread_local FactorMap map;
                thread_local std::vector<LineSample> buffer;
                map.ensure(transmittance.size());
                const std::size_t begin = static_cast<std::size_t>(chunk) * chunkSize;
                const std::size_t end = std::min(moves.size(), begin + chunkSize);
                Candidate best;
                for (std::size_t j = begin; j < end; ++j) {
                    applyToMap(map, moves[j], i, buffer);
                    const Candidate candidate{scoreFactors(map), static_cast<int>(j)};
                    map.reset();
                    if (candidate.betterThan(best))
                        best = candidate;
                }
                chunkBest[static_cast<std::size_t>(chunk)] = best;
            };
            if (threads > 1 && chunkCount > 1)
                QtConcurrent::blockingMap(&pool, chunkIds, scoreChunk);
            else
                std::for_each(chunkIds.begin(), chunkIds.end(), scoreChunk);

            Candidate best;
            for (const Candidate &candidate : chunkBest) {
                if (candidate.betterThan(best))
                    best = candidate;
            }
            if (!(best.delta < -minGain))
                continue;

            // Apply the move with the same factors used for scoring.
            const Move move = moves[static_cast<std::size_t>(best.pin)];
            applyMap.ensure(transmittance.size());
            applyToMap(applyMap, move, i, scratch);
            for (std::uint32_t idx : applyMap.touched) {
                transmittance[idx] *= applyMap.factor[idx];
                encoded[idx] = SrgbEncoder::encode(transmittance[idx]);
            }
            applyMap.reset();
            sse += best.delta;

            Pair removed[2];
            Pair added[2];
            int nr = 0;
            int na = 0;
            edit(move, i, removed, nr, added, na);
            for (int r = 0; r < nr; ++r) {
                --pairCount[pairKey(removed[r].a, removed[r].b, pinCount)];
                length -= layout.distance(removed[r].a, removed[r].b) * mm;
            }
            for (int j = 0; j < na; ++j) {
                ++pairCount[pairKey(added[j].a, added[j].b, pinCount)];
                length += layout.distance(added[j].a, added[j].b) * mm;
            }
            switch (move.kind) {
            case MoveKind::Relocate:
                sequence[i] = move.pin;
                break;
            case MoveKind::Delete:
                sequence.erase(sequence.begin() + static_cast<std::ptrdiff_t>(i));
                --i; // revisit the pin that moved into position i
                break;
            case MoveKind::Insert:
                sequence.insert(sequence.begin() + static_cast<std::ptrdiff_t>(i), move.pin);
                break;
            }
            ++improvedThisPass;
            ++m_improvements;
            if (i % 64 == 0)
                report(static_cast<int>(i), static_cast<int>(sequence.size()) - 1);
        }
        // Re-render exactly; incremental division/multiplication drifts slightly.
        rerender();
        report(static_cast<int>(sequence.size()) - 1, static_cast<int>(sequence.size()) - 1);
        if (improvedThisPass == 0 && !cancelled) {
            result.stopReason = StopReason::NoImprovement;
            break;
        }
    }
    if (cancelled)
        result.stopReason = StopReason::Cancelled;
    else if (sequence.size() < 2 || m_settings.maxPasses == 0)
        result.stopReason = StopReason::NoImprovement;

    result.finalError = sse * normalizer;
    result.threadLengthMm = length;
    result.elapsedSeconds = elapsed();
    report(static_cast<int>(sequence.size()) - 1, static_cast<int>(sequence.size()) - 1);
    return result;
}

} // namespace sa
