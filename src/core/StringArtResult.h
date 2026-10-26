#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace sa {

enum class StopReason { MaxSegments, NoImprovement, NoValidCandidates, MaxThreadLength, Cancelled };

[[nodiscard]] const char *toString(StopReason reason) noexcept;

struct StringArtResult
{
    std::vector<int> sequence;        // ordered pins; segment i joins sequence[i] and sequence[i + 1]
    int width = 0;
    int height = 0;
    std::vector<float> transmittance; // final rendered transmittance at working resolution
    double initialError = 0.0;        // weighted MSE of the empty canvas
    double finalError = 0.0;          // weighted MSE of the result
    double threadLengthMm = 0.0;
    double elapsedSeconds = 0.0;
    StopReason stopReason = StopReason::MaxSegments;
    bool lineCachePrecomputed = false;
    std::size_t lineCacheBytes = 0;

    [[nodiscard]] int segmentCount() const noexcept
    {
        return sequence.empty() ? 0 : static_cast<int>(sequence.size()) - 1;
    }
};

// Snapshot passed to progress callbacks. The spans point into optimizer-owned
// buffers and are only valid during the callback.
struct OptimizationProgress
{
    int segments = 0;
    int maxSegments = 0;
    double error = 0.0;
    double initialError = 0.0;
    double threadLengthMm = 0.0;
    double elapsedSeconds = 0.0;
    int width = 0;
    int height = 0;
    std::span<const float> transmittance;
    std::span<const int> sequence;
};

} // namespace sa
