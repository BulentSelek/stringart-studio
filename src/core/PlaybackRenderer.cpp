#include "core/PlaybackRenderer.h"

#include "core/Compositing.h"
#include "core/LineRasterizer.h"

#include <algorithm>
#include <utility>

namespace sa {

PlaybackRenderer::PlaybackRenderer(PinLayout layout, std::vector<int> sequence, double threadWidth, float opacity,
                                   std::vector<float> finalState, std::size_t memoryBudgetBytes)
    : m_layout(std::move(layout)), m_sequence(std::move(sequence)), m_width(threadWidth), m_opacity(opacity),
      m_budget(memoryBudgetBytes)
{
    const std::size_t pixels =
        static_cast<std::size_t>(m_layout.canvasWidth()) * static_cast<std::size_t>(m_layout.canvasHeight());
    if (finalState.size() == pixels) {
        m_current = std::move(finalState);
        m_position = segmentCount();
    } else {
        buildCheckpoints();
    }
}

void PlaybackRenderer::buildCheckpoints()
{
    const std::size_t pixels =
        static_cast<std::size_t>(m_layout.canvasWidth()) * static_cast<std::size_t>(m_layout.canvasHeight());
    const int segments = segmentCount();
    const auto maxCheckpoints = std::max<std::size_t>(1, m_budget / std::max<std::size_t>(1, pixels * sizeof(float)));
    m_interval = std::max(16, static_cast<int>((static_cast<std::size_t>(segments) + maxCheckpoints - 1)
                                               / maxCheckpoints));
    m_checkpoints.clear();
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
    if (m_checkpoints.empty() && segments != m_position)
        buildCheckpoints();
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

ColorPlaybackRenderer::ColorPlaybackRenderer(PinLayout layout, std::vector<ColorStep> steps,
                                             std::vector<QColor> palette, QColor background, double threadWidth,
                                             float opacity, std::vector<float> finalState,
                                             std::size_t memoryBudgetBytes)
    : m_layout(std::move(layout)), m_steps(std::move(steps)), m_background(toLinearRgb(background)),
      m_width(threadWidth), m_opacity(opacity), m_budget(memoryBudgetBytes)
{
    for (const QColor &c : palette)
        m_colors.push_back(toLinearRgb(c));
    const std::size_t pixels =
        static_cast<std::size_t>(m_layout.canvasWidth()) * static_cast<std::size_t>(m_layout.canvasHeight());
    if (finalState.size() == pixels * 3) {
        m_current = std::move(finalState);
        m_position = segmentCount();
    } else {
        buildCheckpoints();
    }
}

void ColorPlaybackRenderer::buildCheckpoints()
{
    const std::size_t pixels =
        static_cast<std::size_t>(m_layout.canvasWidth()) * static_cast<std::size_t>(m_layout.canvasHeight());
    const auto maxCheckpoints = std::max<std::size_t>(1, m_budget / std::max<std::size_t>(1, pixels * 12));
    const int count = segmentCount();
    m_interval = std::max(16, static_cast<int>((static_cast<std::size_t>(count) + maxCheckpoints - 1)
                                               / maxCheckpoints));
    m_checkpoints.clear();
    m_current.resize(pixels * 3);
    for (std::size_t p = 0; p < pixels; ++p)
        std::copy(m_background.begin(), m_background.end(), m_current.begin() + static_cast<std::ptrdiff_t>(3 * p));
    m_checkpoints.push_back(m_current);
    for (int s = 0; s < count; ++s) {
        apply(s);
        if ((s + 1) % m_interval == 0)
            m_checkpoints.push_back(m_current);
    }
    m_position = count;
}

void ColorPlaybackRenderer::apply(int step)
{
    thread_local std::vector<LineSample> samples;
    const ColorStep &s = m_steps[static_cast<std::size_t>(step)];
    rasterizePinLine(m_layout, s.from, s.to, m_width, m_layout.canvasWidth(), m_layout.canvasHeight(), samples);
    const LinearRgb &k = m_colors.at(static_cast<std::size_t>(s.color));
    for (const LineSample &sample : samples)
        applyThreadOver(&m_current[3 * static_cast<std::size_t>(sample.index)], k, m_opacity * sample.coverage);
}

std::span<const float> ColorPlaybackRenderer::seek(int steps)
{
    steps = std::clamp(steps, 0, segmentCount());
    if (m_checkpoints.empty() && steps != m_position)
        buildCheckpoints();
    if (steps < m_position || steps - m_position > m_interval) {
        const int k = steps / m_interval;
        m_current = m_checkpoints[static_cast<std::size_t>(k)];
        m_position = k * m_interval;
    }
    while (m_position < steps)
        apply(m_position++);
    return m_current;
}

} // namespace sa
