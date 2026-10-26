#include "core/LineRasterizer.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace sa {

namespace {

// Walks the major axis of the segment. `major`/`minor` select the coordinates
// so that one implementation handles both x-major and y-major lines.
template<bool XMajor>
void rasterizeMajor(Vec2 a, Vec2 b, double width, int imageWidth, int imageHeight, std::vector<LineSample> &out)
{
    auto major = [](Vec2 p) { return XMajor ? p.x : p.y; };
    auto minor = [](Vec2 p) { return XMajor ? p.y : p.x; };

    if (major(a) > major(b))
        std::swap(a, b);

    const double dMajor = major(b) - major(a);
    const double dMinor = minor(b) - minor(a);
    const double length = std::hypot(dMajor, dMinor);
    const double cosine = dMajor / length; // >= 1/sqrt(2) for the major axis
    const double slope = dMinor / dMajor;

    // Vertical (minor-axis) half-extent of the thread band within a column.
    const double halfExtent = width * 0.5 / cosine;

    const int majorLimit = XMajor ? imageWidth : imageHeight;
    const int minorLimit = XMajor ? imageHeight : imageWidth;

    // Pixel columns whose centres lie within the segment's major extent.
    const int first = std::max(0, static_cast<int>(std::ceil(major(a) - 0.5)));
    const int last = std::min(majorLimit - 1, static_cast<int>(std::floor(major(b) - 0.5)));

    for (int m = first; m <= last; ++m) {
        const double centerMajor = m + 0.5;
        const double lineMinor = minor(a) + (centerMajor - major(a)) * slope;
        const double bandLo = lineMinor - halfExtent;
        const double bandHi = lineMinor + halfExtent;
        const int lo = std::max(0, static_cast<int>(std::floor(bandLo)));
        const int hi = std::min(minorLimit - 1, static_cast<int>(std::floor(bandHi)));
        for (int n = lo; n <= hi; ++n) {
            const double coverage = std::min<double>(n + 1, bandHi) - std::max<double>(n, bandLo);
            if (coverage <= 1e-7)
                continue;
            const int x = XMajor ? m : n;
            const int y = XMajor ? n : m;
            out.push_back({static_cast<std::uint32_t>(y) * static_cast<std::uint32_t>(imageWidth)
                               + static_cast<std::uint32_t>(x),
                           static_cast<float>(std::min(coverage, 1.0))});
        }
    }
}

} // namespace

void rasterizeLine(Vec2 a, Vec2 b, double width, int imageWidth, int imageHeight, std::vector<LineSample> &out)
{
    out.clear();
    if (width <= 0.0 || imageWidth <= 0 || imageHeight <= 0)
        return;
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    if (std::abs(dx) < 1e-9 && std::abs(dy) < 1e-9)
        return;
    if (std::abs(dx) >= std::abs(dy))
        rasterizeMajor<true>(a, b, width, imageWidth, imageHeight, out);
    else
        rasterizeMajor<false>(a, b, width, imageWidth, imageHeight, out);
}

void rasterizePinLine(const PinLayout &layout, int a, int b, double width, int imageWidth, int imageHeight,
                      std::vector<LineSample> &out, double scale)
{
    if (a > b)
        std::swap(a, b);
    const Vec2 &p = layout.pin(a);
    const Vec2 &q = layout.pin(b);
    rasterizeLine({p.x * scale, p.y * scale}, {q.x * scale, q.y * scale}, width, imageWidth, imageHeight, out);
}

} // namespace sa
