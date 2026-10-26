#include "core/LineCache.h"

#include <QThreadPool>
#include <QtConcurrent/QtConcurrentMap>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

namespace sa {

LineCache::LineCache(const PinLayout &layout, double threadWidth, int imageWidth, int imageHeight,
                     int minSeparation, std::size_t budgetBytes, QThreadPool *pool)
    : m_layout(layout), m_width(threadWidth), m_imageWidth(imageWidth), m_imageHeight(imageHeight)
{
    const int n = layout.pinCount();

    // Upper bound on samples per line: one run of pixels per major-axis step,
    // each run spanning at most width / cos + 2 pixels.
    std::size_t estimatedSamples = 0;
    for (int a = 0; a < n; ++a) {
        for (int b = a + 1; b < n; ++b) {
            if (layout.separation(a, b) < minSeparation)
                continue;
            const Vec2 &p = layout.pin(a);
            const Vec2 &q = layout.pin(b);
            const double major = std::max(std::abs(q.x - p.x), std::abs(q.y - p.y));
            const double length = std::hypot(q.x - p.x, q.y - p.y);
            estimatedSamples += static_cast<std::size_t>((major + 1.0) * (threadWidth * length / major + 2.0));
        }
    }
    m_estimatedBytes = estimatedSamples * sizeof(LineSample);
    if (m_estimatedBytes > budgetBytes)
        return;

    // Rasterize each source pin's row of pairs in parallel, then flatten.
    std::vector<int> rows(static_cast<std::size_t>(n));
    std::iota(rows.begin(), rows.end(), 0);
    std::vector<std::vector<LineSample>> rowSamples(static_cast<std::size_t>(n));
    std::vector<std::vector<std::size_t>> rowCounts(static_cast<std::size_t>(n));
    auto rasterizeRow = [&](int a) {
        std::vector<LineSample> scratch;
        auto &samples = rowSamples[static_cast<std::size_t>(a)];
        auto &counts = rowCounts[static_cast<std::size_t>(a)];
        counts.assign(static_cast<std::size_t>(n - a - 1), 0);
        for (int b = a + 1; b < n; ++b) {
            if (layout.separation(a, b) < minSeparation)
                continue;
            rasterizePinLine(layout, a, b, threadWidth, imageWidth, imageHeight, scratch);
            samples.insert(samples.end(), scratch.begin(), scratch.end());
            counts[static_cast<std::size_t>(b - a - 1)] = scratch.size();
        }
    };
    if (pool)
        QtConcurrent::blockingMap(pool, rows, rasterizeRow);
    else
        std::for_each(rows.begin(), rows.end(), rasterizeRow);

    const std::size_t pairCount = static_cast<std::size_t>(n) * static_cast<std::size_t>(n - 1) / 2;
    m_offsets.assign(pairCount + 1, 0);
    std::size_t total = 0;
    std::size_t pair = 0;
    for (int a = 0; a < n; ++a) {
        for (std::size_t count : rowCounts[static_cast<std::size_t>(a)]) {
            m_offsets[pair++] = total;
            total += count;
        }
    }
    m_offsets[pairCount] = total;

    m_samples.reserve(total);
    for (auto &samples : rowSamples) {
        m_samples.insert(m_samples.end(), samples.begin(), samples.end());
        std::vector<LineSample>().swap(samples);
    }
    m_bytes = m_samples.capacity() * sizeof(LineSample) + m_offsets.capacity() * sizeof(std::size_t);
    m_precomputed = true;
}

std::size_t LineCache::pairIndex(int a, int b) const noexcept
{
    if (a > b)
        std::swap(a, b);
    const auto n = static_cast<std::size_t>(m_layout.pinCount());
    const auto i = static_cast<std::size_t>(a);
    return i * n - i * (i + 1) / 2 + static_cast<std::size_t>(b - a - 1);
}

std::span<const LineSample> LineCache::line(int a, int b, std::vector<LineSample> &scratch) const
{
    if (m_precomputed) {
        const std::size_t index = pairIndex(a, b);
        const std::size_t begin = m_offsets[index];
        const std::size_t end = m_offsets[index + 1];
        if (end > begin)
            return {m_samples.data() + begin, end - begin};
        // Pairs below the cache's separation threshold were skipped; fall back.
    }
    rasterizePinLine(m_layout, a, b, m_width, m_imageWidth, m_imageHeight, scratch);
    return scratch;
}

} // namespace sa
