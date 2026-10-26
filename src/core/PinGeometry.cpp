#include "core/PinGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

namespace sa {

namespace {

constexpr double kMargin = 1.0; // pins stay one pixel inside the canvas

double cross(const Vec2 &o, const Vec2 &a, const Vec2 &b)
{
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

// Andrew's monotone chain; returns the hull in counter-clockwise order.
std::vector<Vec2> convexHull(std::vector<Vec2> points)
{
    std::sort(points.begin(), points.end(), [](const Vec2 &a, const Vec2 &b) {
        return a.x < b.x || (a.x == b.x && a.y < b.y);
    });
    points.erase(std::unique(points.begin(), points.end()), points.end());
    if (points.size() < 3)
        return points;
    std::vector<Vec2> hull(2 * points.size());
    std::size_t k = 0;
    for (const Vec2 &p : points) {
        while (k >= 2 && cross(hull[k - 2], hull[k - 1], p) <= 0.0)
            --k;
        hull[k++] = p;
    }
    for (std::size_t i = points.size() - 1, lower = k + 1; i-- > 0;) {
        while (k >= lower && cross(hull[k - 2], hull[k - 1], points[i]) <= 0.0)
            --k;
        hull[k++] = points[i];
    }
    hull.resize(k - 1);
    return hull;
}

} // namespace

void PinLayout::validateCanvas(int pinCount, int canvasWidth, int canvasHeight)
{
    if (pinCount < kMinPins || pinCount > kMaxPins)
        throw std::invalid_argument("pin count must be between " + std::to_string(kMinPins) + " and "
                                    + std::to_string(kMaxPins));
    if (canvasWidth < 8 || canvasHeight < 8)
        throw std::invalid_argument("canvas must be at least 8x8 pixels");
}

PinLayout PinLayout::circular(int pinCount, int canvasWidth, int canvasHeight, double physicalDiameterMm)
{
    validateCanvas(pinCount, canvasWidth, canvasHeight);
    if (!(physicalDiameterMm > 0.0))
        throw std::invalid_argument("physical diameter must be positive");

    PinLayout layout;
    layout.m_kind = PinLayoutKind::Circular;
    layout.m_canvasWidth = canvasWidth;
    layout.m_canvasHeight = canvasHeight;
    layout.m_center = {canvasWidth * 0.5, canvasHeight * 0.5};
    layout.m_radius = std::min(canvasWidth, canvasHeight) * 0.5 - kMargin;
    layout.m_mmPerUnit = physicalDiameterMm / (2.0 * layout.m_radius);

    layout.m_pins.reserve(pinCount);
    for (int i = 0; i < pinCount; ++i) {
        const double angle = -std::numbers::pi / 2.0 + 2.0 * std::numbers::pi * i / pinCount;
        layout.m_pins.push_back({layout.m_center.x + layout.m_radius * std::cos(angle),
                                 layout.m_center.y + layout.m_radius * std::sin(angle)});
    }
    return layout;
}

PinLayout PinLayout::elliptical(int pinCount, int canvasWidth, int canvasHeight, double widthMm, double heightMm)
{
    validateCanvas(pinCount, canvasWidth, canvasHeight);
    if (!(widthMm > 0.0) || !(heightMm > 0.0))
        throw std::invalid_argument("frame size must be positive");

    PinLayout layout;
    layout.m_kind = PinLayoutKind::Elliptical;
    layout.m_canvasWidth = canvasWidth;
    layout.m_canvasHeight = canvasHeight;
    layout.m_center = {canvasWidth * 0.5, canvasHeight * 0.5};
    const double rx = canvasWidth * 0.5 - kMargin;
    const double ry = canvasHeight * 0.5 - kMargin;
    layout.m_mmPerUnit = widthMm / (2.0 * rx);

    // Tabulate arc length over the parameter angle, then place pins at equal
    // arc-length steps (linear interpolation between samples).
    const int samples = std::max(4096, 16 * pinCount);
    std::vector<double> cumulative(static_cast<std::size_t>(samples) + 1, 0.0);
    auto point = [&](double t) {
        const double angle = -std::numbers::pi / 2.0 + 2.0 * std::numbers::pi * t;
        return Vec2{layout.m_center.x + rx * std::cos(angle), layout.m_center.y + ry * std::sin(angle)};
    };
    Vec2 previous = point(0.0);
    for (int i = 1; i <= samples; ++i) {
        const Vec2 current = point(static_cast<double>(i) / samples);
        cumulative[static_cast<std::size_t>(i)] =
            cumulative[static_cast<std::size_t>(i) - 1] + std::hypot(current.x - previous.x, current.y - previous.y);
        previous = current;
    }
    const double perimeter = cumulative.back();
    layout.m_pins.reserve(pinCount);
    for (int k = 0; k < pinCount; ++k) {
        const double target = perimeter * k / pinCount;
        const auto it = std::lower_bound(cumulative.begin(), cumulative.end(), target);
        const auto i = static_cast<std::size_t>(std::max<std::ptrdiff_t>(1, it - cumulative.begin()));
        const double span = cumulative[i] - cumulative[i - 1];
        const double f = span > 0.0 ? (target - cumulative[i - 1]) / span : 0.0;
        layout.m_pins.push_back(point((static_cast<double>(i - 1) + f) / samples));
    }
    return layout;
}

PinLayout PinLayout::rectangular(int pinCount, int canvasWidth, int canvasHeight, double widthMm, double heightMm)
{
    validateCanvas(pinCount, canvasWidth, canvasHeight);
    if (!(widthMm > 0.0) || !(heightMm > 0.0))
        throw std::invalid_argument("frame size must be positive");

    PinLayout layout;
    layout.m_kind = PinLayoutKind::Rectangular;
    layout.m_canvasWidth = canvasWidth;
    layout.m_canvasHeight = canvasHeight;
    layout.m_center = {canvasWidth * 0.5, canvasHeight * 0.5};
    const double x0 = kMargin;
    const double y0 = kMargin;
    const double w = canvasWidth - 2.0 * kMargin;
    const double h = canvasHeight - 2.0 * kMargin;
    layout.m_mmPerUnit = widthMm / w;

    const double perimeter = 2.0 * (w + h);
    layout.m_pins.reserve(pinCount);
    layout.m_sides.reserve(pinCount);
    for (int k = 0; k < pinCount; ++k) {
        double s = perimeter * k / pinCount;
        Vec2 p;
        if (s < w) {
            p = {x0 + s, y0}; // top edge, left to right
        } else if ((s -= w) < h) {
            p = {x0 + w, y0 + s}; // right edge, downwards
        } else if ((s -= h) < w) {
            p = {x0 + w - s, y0 + h}; // bottom edge, right to left
        } else {
            s -= w;
            p = {x0, y0 + h - s}; // left edge, upwards
        }
        layout.m_pins.push_back(p);
        // Corner pins belong to both adjacent sides.
        constexpr double eps = 1e-9;
        unsigned char sides = 0;
        if (std::abs(p.y - y0) < eps)
            sides |= 1; // top
        if (std::abs(p.x - (x0 + w)) < eps)
            sides |= 2; // right
        if (std::abs(p.y - (y0 + h)) < eps)
            sides |= 4; // bottom
        if (std::abs(p.x - x0) < eps)
            sides |= 8; // left
        layout.m_sides.push_back(sides);
    }
    return layout;
}

PinLayout PinLayout::custom(const std::vector<Vec2> &pinsMm, int canvasWidth, int canvasHeight)
{
    validateCanvas(static_cast<int>(pinsMm.size()), canvasWidth, canvasHeight);
    double minX = pinsMm.front().x, maxX = minX, minY = pinsMm.front().y, maxY = minY;
    for (const Vec2 &p : pinsMm) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y))
            throw std::invalid_argument("custom pin coordinates must be finite");
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }
    const double spanX = maxX - minX;
    const double spanY = maxY - minY;
    if (!(spanX > 0.0) || !(spanY > 0.0))
        throw std::invalid_argument("custom pins must span a two-dimensional area");
    if (convexHull(pinsMm).size() < 3)
        throw std::invalid_argument("custom pins must not be collinear");

    PinLayout layout;
    layout.m_kind = PinLayoutKind::Custom;
    layout.m_canvasWidth = canvasWidth;
    layout.m_canvasHeight = canvasHeight;
    layout.m_center = {canvasWidth * 0.5, canvasHeight * 0.5};
    const double unitsPerMm =
        std::min((canvasWidth - 2.0 * kMargin) / spanX, (canvasHeight - 2.0 * kMargin) / spanY);
    layout.m_mmPerUnit = 1.0 / unitsPerMm;
    // Centre the pin set on the canvas.
    const double offsetX = (canvasWidth - spanX * unitsPerMm) * 0.5;
    const double offsetY = (canvasHeight - spanY * unitsPerMm) * 0.5;
    layout.m_pins.reserve(pinsMm.size());
    for (const Vec2 &p : pinsMm)
        layout.m_pins.push_back({offsetX + (p.x - minX) * unitsPerMm, offsetY + (p.y - minY) * unitsPerMm});
    return layout;
}

std::pair<int, int> PinLayout::canvasSize(const FrameSpec &frame, int workingSize)
{
    if (workingSize < 8)
        throw std::invalid_argument("working size must be at least 8 pixels");
    double aspect = 1.0; // width / height
    switch (frame.kind) {
    case PinLayoutKind::Circular:
        return {workingSize, workingSize};
    case PinLayoutKind::Elliptical:
    case PinLayoutKind::Rectangular:
        if (!(frame.widthMm > 0.0) || !(frame.heightMm > 0.0))
            throw std::invalid_argument("frame size must be positive");
        aspect = frame.widthMm / frame.heightMm;
        break;
    case PinLayoutKind::Custom: {
        if (frame.customPinsMm.empty())
            throw std::invalid_argument("custom layout has no pins");
        const auto [minX, maxX] = std::minmax_element(frame.customPinsMm.begin(), frame.customPinsMm.end(),
                                                      [](const Vec2 &a, const Vec2 &b) { return a.x < b.x; });
        const auto [minY, maxY] = std::minmax_element(frame.customPinsMm.begin(), frame.customPinsMm.end(),
                                                      [](const Vec2 &a, const Vec2 &b) { return a.y < b.y; });
        const double w = maxX->x - minX->x;
        const double h = maxY->y - minY->y;
        if (!(w > 0.0) || !(h > 0.0))
            throw std::invalid_argument("custom pins must span a two-dimensional area");
        aspect = w / h;
        break;
    }
    }
    if (aspect >= 1.0)
        return {workingSize, std::max(8, static_cast<int>(std::lround(workingSize / aspect)))};
    return {std::max(8, static_cast<int>(std::lround(workingSize * aspect))), workingSize};
}

PinLayout PinLayout::fromFrame(const FrameSpec &frame, int workingSize)
{
    const auto [width, height] = canvasSize(frame, workingSize);
    switch (frame.kind) {
    case PinLayoutKind::Circular:
        return circular(frame.pinCount, width, height, frame.widthMm);
    case PinLayoutKind::Elliptical:
        return elliptical(frame.pinCount, width, height, frame.widthMm, frame.heightMm);
    case PinLayoutKind::Rectangular:
        return rectangular(frame.pinCount, width, height, frame.widthMm, frame.heightMm);
    case PinLayoutKind::Custom:
        return custom(frame.customPinsMm, width, height);
    }
    throw std::invalid_argument("unknown layout kind");
}

int PinLayout::separation(int a, int b) const noexcept
{
    const int n = pinCount();
    const int d = std::abs(a - b) % n;
    return std::min(d, n - d);
}

double PinLayout::distance(int a, int b) const
{
    const Vec2 &p = m_pins.at(a);
    const Vec2 &q = m_pins.at(b);
    return std::hypot(q.x - p.x, q.y - p.y);
}

std::vector<float> PinLayout::reachableMask() const
{
    std::vector<float> mask(static_cast<std::size_t>(m_canvasWidth) * static_cast<std::size_t>(m_canvasHeight), 0.0f);
    const std::vector<Vec2> hull = convexHull(m_pins);
    if (hull.size() < 3)
        return mask;
    // Scanline fill: for a convex polygon each row is one interval.
    for (int y = 0; y < m_canvasHeight; ++y) {
        const double cy = y + 0.5;
        double left = std::numeric_limits<double>::infinity();
        double right = -std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < hull.size(); ++i) {
            const Vec2 &a = hull[i];
            const Vec2 &b = hull[(i + 1) % hull.size()];
            if ((a.y <= cy && b.y >= cy) || (b.y <= cy && a.y >= cy)) {
                const double x = a.y == b.y ? std::min(a.x, b.x) : a.x + (cy - a.y) * (b.x - a.x) / (b.y - a.y);
                const double x2 = a.y == b.y ? std::max(a.x, b.x) : x;
                left = std::min(left, x);
                right = std::max(right, x2);
            }
        }
        if (left > right)
            continue;
        const int x0 = std::max(0, static_cast<int>(std::ceil(left - 0.5)));
        const int x1 = std::min(m_canvasWidth - 1, static_cast<int>(std::floor(right - 0.5)));
        std::fill(mask.begin() + static_cast<std::ptrdiff_t>(y) * m_canvasWidth + x0,
                  mask.begin() + static_cast<std::ptrdiff_t>(y) * m_canvasWidth + x1 + 1, 1.0f);
    }
    return mask;
}

} // namespace sa
