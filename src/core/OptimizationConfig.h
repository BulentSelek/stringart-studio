#pragma once

#include <cstddef>

namespace sa {

struct OptimizationConfig
{
    int maxSegments = 4000;            // hard limit on thread segments
    double maxThreadLengthMm = 0.0;    // 0 disables the limit
    int minPinSeparation = 20;         // minimum perimeter steps between connected pins
    float threadOpacity = 0.2f;        // a in T' = T * (1 - a * c), in [0, 1]
    double threadWidth = 1.0;          // thread width in working-image pixels
    bool allowRepeatedLines = false;   // allow the same pin pair more than once
    int startPin = 0;
    double minImprovement = 0.0;       // stop when the best weighted SSE decrease is <= this
    int threadCount = 0;               // candidate-evaluation threads, 0 = automatic
    std::size_t lineCacheBudgetBytes = std::size_t{1} << 30; // precompute lines if they fit
};

// Throws std::invalid_argument describing the first invalid field.
void validateConfig(const OptimizationConfig &config, int pinCount);

// Threads for candidate evaluation: config.threadCount, or for 0 all cores up
// to kMaxAutoThreads. Scoring is memory-bound; beyond 16 threads runs were no
// faster but used up to 3x the CPU time (docs/BENCHMARKS.md).
inline constexpr int kMaxAutoThreads = 16;
[[nodiscard]] int threadCount(const OptimizationConfig &config);

} // namespace sa
