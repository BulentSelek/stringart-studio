#pragma once

#include <vector>

namespace sa {

struct Vec2
{
    double x = 0.0;
    double y = 0.0;
};

enum class PinLayoutKind { Circular };

// Pin positions in working-image coordinates: continuous pixel space where
// pixel (x, y) covers [x, x + 1) x [y, y + 1). Pins are ordered along the
// frame perimeter; pin 0 is at the top and indices increase clockwise.
class PinLayout
{
public:
    static constexpr int kMinPins = 3;
    static constexpr int kMaxPins = 20000;

    // Pins evenly spaced on the largest circle that fits the canvas with a
    // one-pixel margin. physicalDiameterMm maps working units to millimetres.
    // Throws std::invalid_argument on invalid parameters.
    static PinLayout circular(int pinCount, int canvasWidth, int canvasHeight, double physicalDiameterMm = 500.0);

    [[nodiscard]] PinLayoutKind kind() const noexcept { return m_kind; }
    [[nodiscard]] int pinCount() const noexcept { return static_cast<int>(m_pins.size()); }
    [[nodiscard]] const Vec2 &pin(int index) const { return m_pins.at(index); }
    [[nodiscard]] const std::vector<Vec2> &pins() const noexcept { return m_pins; }
    [[nodiscard]] int canvasWidth() const noexcept { return m_canvasWidth; }
    [[nodiscard]] int canvasHeight() const noexcept { return m_canvasHeight; }
    [[nodiscard]] Vec2 center() const noexcept { return m_center; }
    [[nodiscard]] double radius() const noexcept { return m_radius; }

    // Millimetres per working-image unit.
    [[nodiscard]] double mmPerUnit() const noexcept { return m_mmPerUnit; }

    // Number of perimeter steps between two pins (0 for the same pin).
    [[nodiscard]] int separation(int a, int b) const noexcept;

    // Euclidean distance between two pins in working units.
    [[nodiscard]] double distance(int a, int b) const;

private:
    PinLayoutKind m_kind = PinLayoutKind::Circular;
    std::vector<Vec2> m_pins;
    int m_canvasWidth = 0;
    int m_canvasHeight = 0;
    Vec2 m_center;
    double m_radius = 0.0;
    double m_mmPerUnit = 1.0;
};

} // namespace sa
