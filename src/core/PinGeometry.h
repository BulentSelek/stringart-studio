#pragma once

#include <cstddef>
#include <utility>
#include <vector>

namespace sa {

struct Vec2
{
    double x = 0.0;
    double y = 0.0;

    friend bool operator==(const Vec2 &, const Vec2 &) = default;
};

enum class PinLayoutKind { Circular, Elliptical, Rectangular, Custom };

// Physical description of a frame, independent of the working resolution.
struct FrameSpec
{
    PinLayoutKind kind = PinLayoutKind::Circular;
    int pinCount = 240;                // ignored for Custom
    double widthMm = 500.0;            // circle: diameter
    double heightMm = 500.0;           // ignored for Circular and Custom
    std::vector<Vec2> customPinsMm;    // Custom only, in perimeter order

    friend bool operator==(const FrameSpec &, const FrameSpec &) = default;
};

// Pin positions in working-image coordinates: continuous pixel space where
// pixel (x, y) covers [x, x + 1) x [y, y + 1), y pointing down. Pins are
// ordered along the frame perimeter and indices increase clockwise; the
// thread may connect any two pins (chords), and `separation` counts
// perimeter steps between pins.
class PinLayout
{
public:
    static constexpr int kMinPins = 3;
    static constexpr int kMaxPins = 20000;

    // All factories throw std::invalid_argument on invalid parameters.

    // Pins evenly spaced on the largest circle that fits the canvas with a
    // one-pixel margin; pin 0 at the top. diameter maps units to mm.
    static PinLayout circular(int pinCount, int canvasWidth, int canvasHeight, double physicalDiameterMm = 500.0);

    // Pins evenly spaced by arc length on the inscribed ellipse; pin 0 at the top.
    static PinLayout elliptical(int pinCount, int canvasWidth, int canvasHeight, double widthMm, double heightMm);

    // Pins evenly spaced by perimeter length on the inset rectangle, starting
    // at the top-left corner.
    static PinLayout rectangular(int pinCount, int canvasWidth, int canvasHeight, double widthMm, double heightMm);

    // Pins given in millimetres (perimeter order), scaled uniformly to fit the
    // canvas with a one-pixel margin.
    static PinLayout custom(const std::vector<Vec2> &pinsMm, int canvasWidth, int canvasHeight);

    // Canvas size for a frame at a working resolution (pixels on the long side).
    static std::pair<int, int> canvasSize(const FrameSpec &frame, int workingSize);

    // Layout for a frame at a working resolution.
    static PinLayout fromFrame(const FrameSpec &frame, int workingSize);

    [[nodiscard]] PinLayoutKind kind() const noexcept { return m_kind; }
    [[nodiscard]] int pinCount() const noexcept { return static_cast<int>(m_pins.size()); }
    [[nodiscard]] const Vec2 &pin(int index) const { return m_pins.at(index); }
    [[nodiscard]] const std::vector<Vec2> &pins() const noexcept { return m_pins; }
    [[nodiscard]] int canvasWidth() const noexcept { return m_canvasWidth; }
    [[nodiscard]] int canvasHeight() const noexcept { return m_canvasHeight; }
    [[nodiscard]] Vec2 center() const noexcept { return m_center; }
    // Circle radius in working units; 0 for non-circular layouts.
    [[nodiscard]] double radius() const noexcept { return m_radius; }

    // Millimetres per working-image unit.
    [[nodiscard]] double mmPerUnit() const noexcept { return m_mmPerUnit; }

    // Number of perimeter steps between two pins (0 for the same pin).
    [[nodiscard]] int separation(int a, int b) const noexcept;

    // Whether a thread may run between two pins. False for the same pin and,
    // on rectangular frames, for pins on the same side (the thread would lie
    // along the frame edge). Separation limits are checked separately.
    [[nodiscard]] bool canConnect(int a, int b) const noexcept
    {
        if (a == b)
            return false;
        return m_sides.empty()
            || (m_sides[static_cast<std::size_t>(a)] & m_sides[static_cast<std::size_t>(b)]) == 0;
    }

    // Euclidean distance between two pins in working units.
    [[nodiscard]] double distance(int a, int b) const;

    // Per-pixel mask (1 inside, 0 outside) of the convex hull of the pins,
    // i.e. the region threads can cover. Pixel centres are tested.
    [[nodiscard]] std::vector<float> reachableMask() const;

private:
    static void validateCanvas(int pinCount, int canvasWidth, int canvasHeight);

    PinLayoutKind m_kind = PinLayoutKind::Circular;
    std::vector<Vec2> m_pins;
    int m_canvasWidth = 0;
    int m_canvasHeight = 0;
    Vec2 m_center;
    double m_radius = 0.0;
    double m_mmPerUnit = 1.0;
    std::vector<unsigned char> m_sides; // rectangular only: bit mask of frame sides per pin
};

} // namespace sa
