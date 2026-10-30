#pragma once

#include "core/PinGeometry.h"

#include <QThreadPool>
#include <QtConcurrent/QtConcurrentMap>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <numeric>
#include <span>
#include <vector>

namespace sa {

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

// Finds the best line from one pin, scoring the candidates in parallel.
//
// Candidates are split into contiguous chunks of about kChunkSamples
// estimated pixel samples (line length x (thread width + 2)), so short or few
// lines are scored on the calling thread instead of paying for a parallel
// dispatch, and long lines use at most 4 chunks per thread (many small chunks
// balance cores of different speed). The result does
// not depend on the chunking: each score is computed sequentially and ties go
// to the lower pin.
class CandidateSearch
{
public:
    static constexpr double kChunkSamples = 4096.0;

    CandidateSearch(QThreadPool &pool, int threads) : m_pool(pool), m_threads(threads) {}

    // `score(pin)` returns the objective change of the line current -> pin.
    template <typename Score>
    [[nodiscard]] Candidate best(const PinLayout &layout, int current, std::span<const int> candidates,
                                 double threadWidth, const Score &score)
    {
        m_cost.resize(candidates.size());
        double total = 0.0;
        for (std::size_t k = 0; k < candidates.size(); ++k) {
            m_cost[k] = layout.distance(current, candidates[k]) * (threadWidth + 2.0);
            total += m_cost[k];
        }
        const int maxChunks = m_threads > 1 ? 4 * m_threads : 1;
        const int chunkCount = std::clamp(static_cast<int>(total / kChunkSamples), 1,
                                          std::min(maxChunks, static_cast<int>(candidates.size())));

        // Chunk boundaries at equal shares of the estimated work.
        m_bounds.assign(1, 0);
        double done = 0.0;
        for (std::size_t k = 0; k < candidates.size() && static_cast<int>(m_bounds.size()) < chunkCount; ++k) {
            done += m_cost[k];
            if (done >= total * static_cast<double>(m_bounds.size()) / chunkCount)
                m_bounds.push_back(k + 1);
        }
        m_bounds.push_back(candidates.size());

        const std::size_t chunks = m_bounds.size() - 1;
        m_best.assign(chunks, Candidate{});
        auto scoreChunk = [&](std::size_t chunk) {
            Candidate best;
            for (std::size_t k = m_bounds[chunk]; k < m_bounds[chunk + 1]; ++k) {
                const Candidate c{score(candidates[k]), candidates[k]};
                if (c.betterThan(best))
                    best = c;
            }
            m_best[chunk] = best;
        };
        if (chunks > 1) {
            m_ids.resize(chunks);
            std::iota(m_ids.begin(), m_ids.end(), std::size_t{0});
            QtConcurrent::blockingMap(&m_pool, m_ids, scoreChunk);
        } else {
            scoreChunk(0);
        }
        Candidate best;
        for (const Candidate &c : m_best) {
            if (c.betterThan(best))
                best = c;
        }
        return best;
    }

private:
    QThreadPool &m_pool;
    int m_threads;
    std::vector<double> m_cost;
    std::vector<std::size_t> m_bounds;
    std::vector<std::size_t> m_ids;
    std::vector<Candidate> m_best;
};

} // namespace sa
