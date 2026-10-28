#pragma once

#include "core/LineRasterizer.h"
#include "core/PinGeometry.h"

#include <cstddef>
#include <span>
#include <vector>

class QThreadPool;

namespace sa {

// Rasterized coverage of every valid pin pair (separation >= minSeparation
// and PinLayout::canConnect).
//
// If the estimated memory fits `budgetBytes`, all lines are rasterized up
// front (in parallel on `pool`). Otherwise lines are rasterized on demand into
// the caller's scratch buffer; both paths return identical samples because
// they share rasterizePinLine. Memory: 8 bytes per covered pixel sample.
class LineCache
{
public:
    LineCache(const PinLayout &layout, double threadWidth, int imageWidth, int imageHeight, int minSeparation,
              std::size_t budgetBytes, QThreadPool *pool);

    [[nodiscard]] bool isPrecomputed() const noexcept { return m_precomputed; }
    [[nodiscard]] std::size_t memoryBytes() const noexcept { return m_bytes; }
    // Not computed beyond the budget: when isPrecomputed() is false, only
    // known to exceed budgetBytes.
    [[nodiscard]] std::size_t estimatedBytes() const noexcept { return m_estimatedBytes; }

    // Samples of the line between pins a and b (order irrelevant). The span
    // stays valid until the next call with the same scratch buffer.
    [[nodiscard]] std::span<const LineSample> line(int a, int b, std::vector<LineSample> &scratch) const;

private:
    // Lines from pin a to every pin b > a: samples of b in
    // [offsets[b - a - 1], offsets[b - a]).
    struct Row
    {
        std::vector<std::size_t> offsets;
        std::vector<LineSample> samples;
    };

    const PinLayout &m_layout;
    double m_width;
    int m_imageWidth;
    int m_imageHeight;
    bool m_precomputed = false;
    std::size_t m_bytes = 0;
    std::size_t m_estimatedBytes = 0;
    std::vector<Row> m_rows;
};

} // namespace sa
