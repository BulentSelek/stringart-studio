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
    // each run spanning at most width / cos + 2 pixels. Stops as soon as the
    // estimate exceeds the budget.
    const std::size_t budgetSamples = budgetBytes / sizeof(LineSample);
    std::size_t estimatedSamples = 0;
    for (int a = 0; a < n && estimatedSamples <= budgetSamples; ++a) {
        for (int b = a + 1; b < n; ++b) {
            if (layout.separation(a, b) < minSeparation || !layout.canConnect(a, b))
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

    // Rasterize each source pin's row of pairs in parallel. Rows stay separate
    // buffers: copying them into one would need twice the memory at the end.
    std::vector<int> rows(static_cast<std::size_t>(n));
    std::iota(rows.begin(), rows.end(), 0);
    m_rows.resize(static_cast<std::size_t>(n));
    auto rasterizeRow = [&](int a) {
        std::vector<LineSample> scratch;
        Row &row = m_rows[static_cast<std::size_t>(a)];
        row.offsets.assign(static_cast<std::size_t>(n - a), 0);
        for (int b = a + 1; b < n; ++b) {
            if (layout.separation(a, b) >= minSeparation && layout.canConnect(a, b)) {
                rasterizePinLine(layout, a, b, threadWidth, imageWidth, imageHeight, scratch);
                row.samples.insert(row.samples.end(), scratch.begin(), scratch.end());
            }
            row.offsets[static_cast<std::size_t>(b - a)] = row.samples.size();
        }
        row.samples.shrink_to_fit();
    };
    if (pool)
        QtConcurrent::blockingMap(pool, rows, rasterizeRow);
    else
        std::for_each(rows.begin(), rows.end(), rasterizeRow);

    for (const Row &row : m_rows)
        m_bytes += row.samples.capacity() * sizeof(LineSample) + row.offsets.capacity() * sizeof(std::size_t);
    m_precomputed = true;
}

std::span<const LineSample> LineCache::line(int a, int b, std::vector<LineSample> &scratch) const
{
    if (m_precomputed) {
        if (a > b)
            std::swap(a, b);
        const Row &row = m_rows[static_cast<std::size_t>(a)];
        const std::size_t begin = row.offsets[static_cast<std::size_t>(b - a - 1)];
        const std::size_t end = row.offsets[static_cast<std::size_t>(b - a)];
        if (end > begin)
            return {row.samples.data() + begin, end - begin};
        // Pairs below the cache's separation threshold were skipped; fall back.
    }
    rasterizePinLine(m_layout, a, b, m_width, m_imageWidth, m_imageHeight, scratch);
    return scratch;
}

} // namespace sa
