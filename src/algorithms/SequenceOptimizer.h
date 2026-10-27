#pragma once

#include "algorithms/IOptimizer.h"

#include <vector>

namespace sa {

struct SequenceRefinementSettings
{
    int maxPasses = 3;     // full sweeps over the sequence
    int neighborhood = 20; // relocation candidates within +-neighborhood perimeter steps; 0 = all pins
    bool changeSegmentCount = true; // allow Delete and Insert moves (bounded by maxSegments)
};

// Algorithm C: local search on an existing continuous sequence.
//
// Moves at position i with neighbours a = s[i-1], b = s[i], c = s[i+1]:
//   Relocate: (a,b),(b,c) -> (a,p),(p,c) for p near b (last position: (a,b) -> (a,p))
//   Delete:   (a,b),(b,c) -> (a,c)             removes a segment greedy overused
//   Insert:   (a,b)       -> (a,p),(p,b)       while below maxSegments, any p
// The start pin is fixed and every move keeps consecutive segments sharing
// an endpoint, so the result stays one continuous thread. Moves respect
// minPinSeparation, PinLayout::canConnect, repeated-line rules and
// maxThreadLengthMm. Delete/Insert can be disabled (changeSegmentCount).
//
// Objective: the same weighted SSE as GreedyOptimizer, S = sum w (enc(T) - g)^2.
// The change of S for a move is exact: with the multiplicative model a
// segment is removed by dividing by (1 - a c) and added by multiplying, so
// only the pixels of the four affected lines are evaluated. The best
// improving candidate is applied (ties: lowest pin index); positions are
// visited in order, and passes repeat until a pass makes no improvement or
// maxPasses is reached. T is re-rendered exactly after every pass to remove
// rounding drift. Requires threadOpacity < 1 (division).
//
// Complexity per pass: O(n * K * L), n = segments, K = candidates per
// position (2 * neighborhood relocations, plus all pins for insertions while
// below the segment budget), L = pixels per line. Candidates
// are scored in parallel; results do not depend on the thread count.
class SequenceOptimizer final : public IOptimizer
{
public:
    SequenceOptimizer(std::vector<int> initialSequence, SequenceRefinementSettings settings = {});

    [[nodiscard]] std::string name() const override { return "sequence-refinement"; }

    // The returned result has StopReason::NoImprovement when a pass made no
    // change (converged), MaxPasses when maxPasses was reached, or Cancelled.
    // Progress reports `segments` as the position within the current pass.
    [[nodiscard]] StringArtResult optimize(const OptimizationProblem &problem, const OptimizationConfig &config,
                                           std::stop_token stop = {}, const ProgressCallback &progress = {}) override;

    [[nodiscard]] int passes() const noexcept { return m_passes; }
    [[nodiscard]] int improvements() const noexcept { return m_improvements; }

private:
    std::vector<int> m_initial;
    SequenceRefinementSettings m_settings;
    int m_passes = 0;
    int m_improvements = 0;
};

} // namespace sa
