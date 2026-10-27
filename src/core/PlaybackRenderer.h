#pragma once

#include "core/PinGeometry.h"

#include <cstddef>
#include <span>
#include <vector>

namespace sa {

// Renders the transmittance after the first n segments of a finished
// sequence, for winding playback. It never re-optimizes: it replays the given
// sequence with the same compositing as ThreadRenderer, so seek(n) equals
// ThreadRenderer::renderTransmittance(..., segmentLimit = n) bit for bit.
//
// Checkpoints of the buffer are stored every `interval` segments (chosen to
// fit `memoryBudgetBytes`), so any seek replays at most one interval; moving
// forward from the current position replays only the new segments.
class PlaybackRenderer
{
public:
    PlaybackRenderer(PinLayout layout, std::vector<int> sequence, double threadWidth, float opacity,
                     std::size_t memoryBudgetBytes = std::size_t{64} << 20);

    [[nodiscard]] int segmentCount() const noexcept;
    [[nodiscard]] int position() const noexcept { return m_position; }
    [[nodiscard]] int checkpointInterval() const noexcept { return m_interval; }
    [[nodiscard]] const PinLayout &layout() const noexcept { return m_layout; }
    [[nodiscard]] const std::vector<int> &sequence() const noexcept { return m_sequence; }

    // Transmittance after `segments` segments (clamped to [0, segmentCount]).
    // The span stays valid until the next call.
    std::span<const float> seek(int segments);

private:
    void apply(int segment);

    PinLayout m_layout;
    std::vector<int> m_sequence;
    double m_width;
    float m_opacity;
    int m_interval = 1;
    std::vector<std::vector<float>> m_checkpoints; // m_checkpoints[k] = state after k * m_interval segments
    std::vector<float> m_current;
    int m_position = 0;
};

} // namespace sa
