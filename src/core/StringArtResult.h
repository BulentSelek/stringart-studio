#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace sa {

enum class StopReason { MaxSegments, NoImprovement, NoValidCandidates, MaxThreadLength, Cancelled };

[[nodiscard]] const char *toString(StopReason reason) noexcept;

// One segment of a multi-colour result: thread `color` (palette index) runs
// from pin `from` to pin `to`. Steps are in global winding order; the steps
// of one colour form a continuous thread.
struct ColorStep
{
    int color = 0;
    int from = 0;
    int to = 0;

    friend bool operator==(const ColorStep &, const ColorStep &) = default;
};

struct StringArtResult
{
    std::vector<int> sequence;        // ordered pins; segment i joins sequence[i] and sequence[i + 1]
    int width = 0;
    int height = 0;
    std::vector<float> transmittance; // final rendered transmittance at working resolution (single colour)
    std::vector<ColorStep> colorSteps; // multi-colour only: global winding order
    std::vector<float> rgb;            // multi-colour only: interleaved linear RGB at working resolution
    double initialError = 0.0;        // weighted MSE of the empty canvas
    double finalError = 0.0;          // weighted MSE of the result
    double threadLengthMm = 0.0;
    double elapsedSeconds = 0.0;
    StopReason stopReason = StopReason::MaxSegments;
    bool lineCachePrecomputed = false;
    std::size_t lineCacheBytes = 0;

    [[nodiscard]] int segmentCount() const noexcept
    {
        if (!colorSteps.empty())
            return static_cast<int>(colorSteps.size());
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
    std::span<const float> transmittance; // single colour
    std::span<const int> sequence;
    std::span<const float> rgb;           // multi-colour: interleaved linear RGB
};

} // namespace sa
