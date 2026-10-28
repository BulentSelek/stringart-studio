#pragma once

#include "core/OptimizationConfig.h"
#include "core/OptimizationProblem.h"
#include "core/StringArtResult.h"

#include <functional>
#include <stop_token>

namespace sa {

// Called after every accepted segment (and once at the end) on the optimizer
// thread. Implementations should be cheap; they may block (e.g. to pause).
using ProgressCallback = std::function<void(const OptimizationProgress &)>;

class IOptimizer
{
public:
    virtual ~IOptimizer() = default;

    // Throws std::invalid_argument for an invalid problem or config.
    // Cancellation via `stop` returns the partial result with StopReason::Cancelled.
    [[nodiscard]] virtual StringArtResult optimize(const OptimizationProblem &problem, const OptimizationConfig &config,
                                                   std::stop_token stop = {}, const ProgressCallback &progress = {}) = 0;
};

} // namespace sa
