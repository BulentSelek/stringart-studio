#include "core/PinGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <stdexcept>
#include <string>

namespace sa {

PinLayout PinLayout::circular(int pinCount, int canvasWidth, int canvasHeight, double physicalDiameterMm)
{
    if (pinCount < kMinPins || pinCount > kMaxPins)
        throw std::invalid_argument("pin count must be between " + std::to_string(kMinPins) + " and "
                                    + std::to_string(kMaxPins));
    if (canvasWidth < 8 || canvasHeight < 8)
        throw std::invalid_argument("canvas must be at least 8x8 pixels");
    if (!(physicalDiameterMm > 0.0))
        throw std::invalid_argument("physical diameter must be positive");

    PinLayout layout;
    layout.m_kind = PinLayoutKind::Circular;
    layout.m_canvasWidth = canvasWidth;
    layout.m_canvasHeight = canvasHeight;
    layout.m_center = {canvasWidth * 0.5, canvasHeight * 0.5};
    layout.m_radius = std::min(canvasWidth, canvasHeight) * 0.5 - 1.0;
    layout.m_mmPerUnit = physicalDiameterMm / (2.0 * layout.m_radius);

    layout.m_pins.reserve(pinCount);
    for (int i = 0; i < pinCount; ++i) {
        const double angle = -std::numbers::pi / 2.0 + 2.0 * std::numbers::pi * i / pinCount;
        layout.m_pins.push_back({layout.m_center.x + layout.m_radius * std::cos(angle),
                                 layout.m_center.y + layout.m_radius * std::sin(angle)});
    }
    return layout;
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

} // namespace sa
