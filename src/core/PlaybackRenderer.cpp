#include "core/PlaybackRenderer.h"

#include "core/Compositing.h"
#include "core/LineRasterizer.h"

#include <algorithm>
#include <utility>

namespace sa {

PlaybackRenderer::PlaybackRenderer(PinLayout layout, std::vector<int> sequence, double threadWidth, float opacity,
                                   std::size_t memoryBudgetBytes)
    : m_layout(std::move(layout)), m_sequence(std::move(sequence)), m_width(threadWidth), m_opacity(opacity)
{
    const std::size_t pixels =
        static_cast<std::size_t>(m_layout.canvasWidth()) * static_cast<std::size_t>(m_layout.canvasHeight());
    const int segments = segmentCount();
    const std::size_t checkpointBytes = std::max<std::size_t>(1, pixels * sizeof(float));
    const auto maxCheckpoints = std::max<std::size_t>(1, memoryBudgetBytes / checkpointBytes);
    m_interval = std::max(16, static_cast<int>((static_cast<std::size_t>(segments) + maxCheckpoints - 1)
                                               / maxCheckpoints));

    m_current.assign(pixels, 1.0f);
    m_checkpoints.push_back(m_current);
    for (int s = 0; s < segments; ++s) {
        apply(s);
        if ((s + 1) % m_interval == 0)
            m_checkpoints.push_back(m_current);
    }
    m_position = segments;
}

int PlaybackRenderer::segmentCount() const noexcept
{
    return m_sequence.size() < 2 ? 0 : static_cast<int>(m_sequence.size()) - 1;
}

void PlaybackRenderer::apply(int segment)
{
    thread_local std::vector<LineSample> samples;
    rasterizePinLine(m_layout, m_sequence[static_cast<std::size_t>(segment)],
                     m_sequence[static_cast<std::size_t>(segment) + 1], m_width, m_layout.canvasWidth(),
                     m_layout.canvasHeight(), samples);
    for (const LineSample &s : samples)
        m_current[s.index] = applyThread(m_current[s.index], m_opacity, s.coverage);
}

std::span<const float> PlaybackRenderer::seek(int segments)
{
    segments = std::clamp(segments, 0, segmentCount());
    // Step forward when the target is within one interval ahead; otherwise
    // restart from the nearest checkpoint at or before the target.
    if (segments < m_position || segments - m_position > m_interval) {
        const int k = segments / m_interval;
        m_current = m_checkpoints[static_cast<std::size_t>(k)];
        m_position = k * m_interval;
    }
    while (m_position < segments)
        apply(m_position++);
    return m_current;
}

} // namespace sa
