#pragma once

#include "core/ColorRenderer.h"
#include "core/PinGeometry.h"
#include "core/StringArtResult.h"

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
//
// If the caller already has the final buffer (e.g. from the optimizer), it
// can pass it as `finalState`: construction is then O(1) and the checkpoints
// are built on the first seek away from the end, so finishing a run never
// stalls the GUI thread.
class PlaybackRenderer
{
public:
    PlaybackRenderer(PinLayout layout, std::vector<int> sequence, double threadWidth, float opacity,
                     std::vector<float> finalState = {}, std::size_t memoryBudgetBytes = std::size_t{64} << 20);

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
    void buildCheckpoints();

    PinLayout m_layout;
    std::vector<int> m_sequence;
    double m_width;
    float m_opacity;
    std::size_t m_budget;
    int m_interval = 1;
    std::vector<std::vector<float>> m_checkpoints; // m_checkpoints[k] = state after k * m_interval segments
    std::vector<float> m_current;
    int m_position = 0;
};

// Multi-colour counterpart: replays ColorStep lists with ColorRenderer's
// compositing (interleaved linear RGB), same checkpoint strategy.
class ColorPlaybackRenderer
{
public:
    ColorPlaybackRenderer(PinLayout layout, std::vector<ColorStep> steps, std::vector<QColor> palette,
                          QColor background, double threadWidth, float opacity, std::vector<float> finalState = {},
                          std::size_t memoryBudgetBytes = std::size_t{96} << 20);

    [[nodiscard]] int segmentCount() const noexcept { return static_cast<int>(m_steps.size()); }
    [[nodiscard]] int position() const noexcept { return m_position; }
    [[nodiscard]] const std::vector<ColorStep> &steps() const noexcept { return m_steps; }

    // Interleaved linear RGB after `steps` steps; valid until the next call.
    std::span<const float> seek(int steps);

private:
    void apply(int step);
    void buildCheckpoints();

    PinLayout m_layout;
    std::vector<ColorStep> m_steps;
    std::vector<LinearRgb> m_colors;
    LinearRgb m_background;
    double m_width;
    float m_opacity;
    std::size_t m_budget;
    int m_interval = 1;
    std::vector<std::vector<float>> m_checkpoints;
    std::vector<float> m_current;
    int m_position = 0;
};

} // namespace sa
