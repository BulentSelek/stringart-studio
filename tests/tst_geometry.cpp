#include "core/Compositing.h"
#include "core/LineCache.h"
#include "core/LineRasterizer.h"
#include "core/PinGeometry.h"
#include "core/ThreadRenderer.h"

#include <QTest>

#include <cmath>
#include <numbers>
#include <stdexcept>

using namespace sa;

class TestGeometry : public QObject
{
    Q_OBJECT

private slots:
    void circularPins();
    void invalidPinCounts();
    void separation();
    void rasterCoverageIntegral_data();
    void rasterCoverageIntegral();
    void rasterBoundsAndSymmetry();
    void rasterHorizontalLine();
    void rasterDegenerate();
    void lineCacheMatchesOnDemand();
    void opacityAccumulation();
    void zeroOpacityRendersBlank();
    void srgbRoundTrip();
    void ellipticalPins();
    void rectangularPins();
    void customPins();
    void canvasSizeFollowsAspect();
    void physicalFrameSize();
    void reachableMask();
};

void TestGeometry::circularPins()
{
    const PinLayout layout = PinLayout::circular(200, 512, 512, 600.0);
    QCOMPARE(layout.pinCount(), 200);
    QCOMPARE(layout.radius(), 255.0);
    for (const Vec2 &p : layout.pins()) {
        const double r = std::hypot(p.x - 256.0, p.y - 256.0);
        QVERIFY(std::abs(r - 255.0) < 1e-9);
        QVERIFY(p.x >= 0.0 && p.x <= 512.0 && p.y >= 0.0 && p.y <= 512.0);
    }
    // Pin 0 at the top, pin n/4 on the right (clockwise in image coordinates).
    QVERIFY(std::abs(layout.pin(0).x - 256.0) < 1e-9);
    QVERIFY(std::abs(layout.pin(0).y - 1.0) < 1e-9);
    QVERIFY(std::abs(layout.pin(50).x - 511.0) < 1e-9);
    QVERIFY(std::abs(layout.pin(50).y - 256.0) < 1e-9);
    // Physical scale: pin circle diameter maps to 600 mm.
    QVERIFY(std::abs(layout.distance(0, 100) * layout.mmPerUnit() - 600.0) < 1e-9);
    // Adjacent pins are evenly spaced.
    const double chord = 2.0 * 255.0 * std::sin(std::numbers::pi / 200.0);
    for (int i = 0; i < 200; ++i)
        QVERIFY(std::abs(layout.distance(i, (i + 1) % 200) - chord) < 1e-9);
}

void TestGeometry::invalidPinCounts()
{
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, PinLayout::circular(2, 512, 512));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, PinLayout::circular(-5, 512, 512));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, PinLayout::circular(PinLayout::kMaxPins + 1, 512, 512));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, PinLayout::circular(100, 4, 512));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, PinLayout::circular(100, 512, 512, 0.0));
}

void TestGeometry::separation()
{
    const PinLayout layout = PinLayout::circular(10, 64, 64);
    QCOMPARE(layout.separation(0, 0), 0);
    QCOMPARE(layout.separation(0, 1), 1);
    QCOMPARE(layout.separation(0, 9), 1);
    QCOMPARE(layout.separation(2, 7), 5);
    QCOMPARE(layout.separation(8, 1), 3);
}

void TestGeometry::rasterCoverageIntegral_data()
{
    QTest::addColumn<double>("angle");
    QTest::addColumn<double>("width");
    for (double width : {0.5, 1.0, 2.0, 3.5}) {
        for (double angle : {0.0, 0.3, 0.785398, 1.1, 1.5707963, 2.5}) {
            QTest::addRow("w%.1f_a%.2f", width, angle) << angle << width;
        }
    }
}

void TestGeometry::rasterCoverageIntegral()
{
    QFETCH(double, angle);
    QFETCH(double, width);
    const Vec2 c{100.0, 100.0};
    const double half = 70.0;
    const Vec2 a{c.x - half * std::cos(angle), c.y - half * std::sin(angle)};
    const Vec2 b{c.x + half * std::cos(angle), c.y + half * std::sin(angle)};
    std::vector<LineSample> samples;
    rasterizeLine(a, b, width, 200, 200, samples);
    double sum = 0.0;
    for (const LineSample &s : samples) {
        QVERIFY(s.coverage > 0.0f && s.coverage <= 1.0f);
        sum += s.coverage;
    }
    // Summed coverage approximates the line's area (width * length).
    const double expected = width * 2.0 * half;
    QVERIFY2(std::abs(sum - expected) / expected < 0.03, qPrintable(QStringLiteral("sum %1 expected %2").arg(sum).arg(expected)));
}

void TestGeometry::rasterBoundsAndSymmetry()
{
    const PinLayout layout = PinLayout::circular(37, 97, 61);
    std::vector<LineSample> forward;
    std::vector<LineSample> backward;
    for (int a = 0; a < layout.pinCount(); ++a) {
        for (int b = a + 1; b < layout.pinCount(); ++b) {
            rasterizePinLine(layout, a, b, 1.5, 97, 61, forward);
            rasterizePinLine(layout, b, a, 1.5, 97, 61, backward);
            QCOMPARE(forward.size(), backward.size());
            for (std::size_t i = 0; i < forward.size(); ++i) {
                QVERIFY(forward[i].index < 97u * 61u);
                QCOMPARE(forward[i].index, backward[i].index);
                QCOMPARE(forward[i].coverage, backward[i].coverage);
            }
        }
    }
}

void TestGeometry::rasterHorizontalLine()
{
    std::vector<LineSample> samples;
    // A width-1 line through pixel centres covers exactly one row fully.
    rasterizeLine({2.0, 5.5}, {12.0, 5.5}, 1.0, 16, 16, samples);
    QCOMPARE(samples.size(), std::size_t{10});
    for (const LineSample &s : samples) {
        QCOMPARE(s.index / 16u, 5u);
        QCOMPARE(s.coverage, 1.0f);
    }
    // Half-way between rows: two rows at half coverage.
    rasterizeLine({2.0, 5.0}, {12.0, 5.0}, 1.0, 16, 16, samples);
    QCOMPARE(samples.size(), std::size_t{20});
    for (const LineSample &s : samples)
        QCOMPARE(s.coverage, 0.5f);
}

void TestGeometry::rasterDegenerate()
{
    std::vector<LineSample> samples{{1, 1.0f}};
    rasterizeLine({5.0, 5.0}, {5.0, 5.0}, 1.0, 16, 16, samples);
    QVERIFY(samples.empty());
    rasterizeLine({-50.0, -50.0}, {-10.0, -20.0}, 1.0, 16, 16, samples);
    QVERIFY(samples.empty());
}

void TestGeometry::lineCacheMatchesOnDemand()
{
    const PinLayout layout = PinLayout::circular(40, 80, 80);
    const LineCache cached(layout, 1.0, 80, 80, 3, std::size_t{1} << 30, nullptr);
    const LineCache onDemand(layout, 1.0, 80, 80, 3, 0, nullptr);
    QVERIFY(cached.isPrecomputed());
    QVERIFY(!onDemand.isPrecomputed());
    QVERIFY(cached.memoryBytes() > 0);
    QVERIFY(cached.memoryBytes() <= cached.estimatedBytes() + 41 * 40 / 2 * sizeof(std::size_t));
    std::vector<LineSample> s1;
    std::vector<LineSample> s2;
    for (int a = 0; a < 40; ++a) {
        for (int b = 0; b < 40; ++b) {
            if (layout.separation(a, b) < 3)
                continue;
            const auto x = cached.line(a, b, s1);
            const auto y = onDemand.line(a, b, s2);
            QCOMPARE(x.size(), y.size());
            for (std::size_t i = 0; i < x.size(); ++i) {
                QCOMPARE(x[i].index, y[i].index);
                QCOMPARE(x[i].coverage, y[i].coverage);
            }
        }
    }
}

void TestGeometry::opacityAccumulation()
{
    const PinLayout layout = PinLayout::circular(4, 32, 32);
    // Pins 0 and 2 form a vertical diameter through x = 16.
    const std::vector<int> once{0, 2};
    const std::vector<int> thrice{0, 2, 0, 2};
    const float opacity = 0.3f;
    const auto t1 = ThreadRenderer::renderTransmittance(layout, once, 1.0, opacity);
    const auto t3 = ThreadRenderer::renderTransmittance(layout, thrice, 1.0, opacity);
    std::vector<LineSample> samples;
    rasterizePinLine(layout, 0, 2, 1.0, 32, 32, samples);
    QVERIFY(!samples.empty());
    for (const LineSample &s : samples) {
        const double single = 1.0 - opacity * s.coverage;
        QVERIFY(std::abs(t1[s.index] - single) < 1e-6);
        QVERIFY(std::abs(t3[s.index] - std::pow(single, 3.0)) < 1e-6);
        // Overlap never goes below zero and is darker than a single pass.
        QVERIFY(t3[s.index] >= 0.0f && t3[s.index] <= t1[s.index]);
    }
    // Untouched pixels stay at full transmittance.
    QCOMPARE(t3[0], 1.0f);
}

void TestGeometry::zeroOpacityRendersBlank()
{
    const PinLayout layout = PinLayout::circular(30, 64, 64);
    const std::vector<int> sequence{0, 15, 3, 20, 7};
    for (float t : ThreadRenderer::renderTransmittance(layout, sequence, 1.0, 0.0f))
        QCOMPARE(t, 1.0f);
    const QImage image = ThreadRenderer::render(layout, sequence, RenderStyle{1.0, 0.0f, Qt::white, Qt::black});
    QCOMPARE(image.pixel(32, 32), qRgb(255, 255, 255));
}

void TestGeometry::srgbRoundTrip()
{
    for (int i = 0; i <= 100; ++i) {
        const float v = i / 100.0f;
        QVERIFY(std::abs(linearToSrgb(srgbToLinear(v)) - v) < 1e-5f);
        QVERIFY(std::abs(SrgbEncoder::encode(v) - linearToSrgb(v)) < 2e-4f);
    }
    QCOMPARE(SrgbEncoder::encode(1.0f), linearToSrgb(1.0f));
    QCOMPARE(SrgbEncoder::encode(0.0f), 0.0f);
}

void TestGeometry::ellipticalPins()
{
    const PinLayout layout = PinLayout::elliptical(120, 402, 202, 800.0, 400.0);
    QCOMPARE(layout.pinCount(), 120);
    QCOMPARE(layout.kind(), PinLayoutKind::Elliptical);
    const double rx = 200.0;
    const double ry = 100.0;
    for (const Vec2 &p : layout.pins()) {
        const double u = (p.x - 201.0) / rx;
        const double v = (p.y - 101.0) / ry;
        QVERIFY(std::abs(u * u + v * v - 1.0) < 1e-6);
    }
    QVERIFY(std::abs(layout.pin(0).x - 201.0) < 1e-6);
    QVERIFY(std::abs(layout.pin(0).y - 1.0) < 1e-6);
    // Even spacing along the arc: neighbouring chords differ by < 1 %.
    double minChord = 1e9;
    double maxChord = 0.0;
    for (int i = 0; i < 120; ++i) {
        const double d = layout.distance(i, (i + 1) % 120);
        minChord = std::min(minChord, d);
        maxChord = std::max(maxChord, d);
    }
    QVERIFY2(maxChord / minChord < 1.01, qPrintable(QStringLiteral("%1 %2").arg(minChord).arg(maxChord)));
    QVERIFY(std::abs(layout.mmPerUnit() - 2.0) < 1e-9);
}

void TestGeometry::rectangularPins()
{
    // Inset rectangle 100 x 50 units, perimeter 300, 30 pins -> 10 units apart.
    const PinLayout layout = PinLayout::rectangular(30, 102, 52, 1000.0, 500.0);
    QCOMPARE(layout.pinCount(), 30);
    QCOMPARE(layout.pin(0), (Vec2{1.0, 1.0}));   // top-left corner
    QCOMPARE(layout.pin(10), (Vec2{101.0, 1.0})); // top-right corner
    QCOMPARE(layout.pin(15), (Vec2{101.0, 51.0}));
    QCOMPARE(layout.pin(25), (Vec2{1.0, 51.0}));
    QCOMPARE(layout.pin(29), (Vec2{1.0, 11.0}));
    for (const Vec2 &p : layout.pins()) {
        const bool onEdge = p.x == 1.0 || p.x == 101.0 || p.y == 1.0 || p.y == 51.0;
        QVERIFY(onEdge);
    }
    QVERIFY(std::abs(layout.mmPerUnit() - 10.0) < 1e-9);
    QCOMPARE(layout.separation(0, 29), 1);
    // Same-side pins cannot connect; corners belong to both adjacent sides.
    QVERIFY(!layout.canConnect(1, 9));   // top
    QVERIFY(!layout.canConnect(0, 5));   // corner and top
    QVERIFY(!layout.canConnect(0, 28));  // corner and left
    QVERIFY(!layout.canConnect(10, 13)); // corner and right
    QVERIFY(layout.canConnect(5, 20));   // top to bottom
    QVERIFY(layout.canConnect(10, 25));  // opposite corners
    QVERIFY(!layout.canConnect(4, 4));
    const PinLayout circle = PinLayout::circular(10, 64, 64);
    QVERIFY(circle.canConnect(0, 1));
    QVERIFY(!circle.canConnect(3, 3));
}

void TestGeometry::customPins()
{
    // A 200 x 100 mm triangle-ish shape fitted into 202 x 102 px.
    const std::vector<Vec2> mm{{0, 0}, {200, 0}, {200, 100}, {100, 60}, {0, 100}};
    const PinLayout layout = PinLayout::custom(mm, 202, 102);
    QCOMPARE(layout.pinCount(), 5);
    QCOMPARE(layout.kind(), PinLayoutKind::Custom);
    QVERIFY(std::abs(layout.mmPerUnit() - 1.0) < 1e-9);
    QCOMPARE(layout.pin(0), (Vec2{1.0, 1.0}));
    QCOMPARE(layout.pin(2), (Vec2{201.0, 101.0}));
    QCOMPARE(layout.pin(3), (Vec2{101.0, 61.0}));

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, PinLayout::custom({{0, 0}, {1, 1}}, 64, 64));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, PinLayout::custom({{0, 0}, {1, 1}, {2, 2}}, 64, 64));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, PinLayout::custom({{0, 0}, {1, 0}, {2, 0}}, 64, 64));
}

void TestGeometry::canvasSizeFollowsAspect()
{
    FrameSpec frame;
    QCOMPARE(PinLayout::canvasSize(frame, 512), (std::pair{512, 512}));
    frame.kind = PinLayoutKind::Rectangular;
    frame.widthMm = 600;
    frame.heightMm = 400;
    QCOMPARE(PinLayout::canvasSize(frame, 512), (std::pair{512, 341}));
    frame.kind = PinLayoutKind::Elliptical;
    frame.widthMm = 300;
    frame.heightMm = 600;
    QCOMPARE(PinLayout::canvasSize(frame, 512), (std::pair{256, 512}));
    frame.kind = PinLayoutKind::Custom;
    frame.customPinsMm = {{0, 0}, {100, 0}, {50, 50}};
    QCOMPARE(PinLayout::canvasSize(frame, 512), (std::pair{512, 256}));
    const PinLayout layout = PinLayout::fromFrame(frame, 512);
    QCOMPARE(layout.canvasWidth(), 512);
    QCOMPARE(layout.canvasHeight(), 256);

    frame.kind = PinLayoutKind::Rectangular;
    frame.heightMm = 0.0;
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, PinLayout::canvasSize(frame, 512));
}

void TestGeometry::physicalFrameSize()
{
    // The pins span exactly the frame size in mm, whatever the canvas rounding.
    struct Case
    {
        PinLayoutKind kind;
        double widthMm;
        double heightMm;
        int workingSize;
    };
    const Case cases[] = {{PinLayoutKind::Rectangular, 400, 600, 512}, {PinLayoutKind::Rectangular, 600, 400, 384},
                          {PinLayoutKind::Rectangular, 50, 5000, 512},  {PinLayoutKind::Elliptical, 400, 600, 512},
                          {PinLayoutKind::Elliptical, 333, 500, 384},   {PinLayoutKind::Elliptical, 5000, 50, 512}};
    for (const Case &c : cases) {
        FrameSpec frame;
        frame.kind = c.kind;
        frame.widthMm = c.widthMm;
        frame.heightMm = c.heightMm;
        frame.pinCount = 400;
        const PinLayout layout = PinLayout::fromFrame(frame, c.workingSize);
        double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
        for (const Vec2 &p : layout.pins()) {
            minX = std::min(minX, p.x);
            maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
            QVERIFY(p.x >= 0.0 && p.x <= layout.canvasWidth() && p.y >= 0.0 && p.y <= layout.canvasHeight());
        }
        const QString label = QStringLiteral("%1 x %2 mm at %3 px").arg(c.widthMm).arg(c.heightMm).arg(c.workingSize);
        // Pins sit on the outline, so an ellipse's extreme pins can fall just short of it.
        const double tolerance = c.kind == PinLayoutKind::Elliptical ? 0.002 : 1e-9;
        QVERIFY2(std::abs((maxX - minX) * layout.mmPerUnit() / c.widthMm - 1.0) < tolerance, qPrintable(label));
        QVERIFY2(std::abs((maxY - minY) * layout.mmPerUnit() / c.heightMm - 1.0) < tolerance, qPrintable(label));
        // Centred on the canvas.
        QVERIFY2(std::abs(minX + maxX - layout.canvasWidth()) < 0.05 * layout.canvasWidth(), qPrintable(label));
        QVERIFY2(std::abs(minY + maxY - layout.canvasHeight()) < 0.05 * layout.canvasHeight(), qPrintable(label));
    }
}

void TestGeometry::reachableMask()
{
    const PinLayout rect = PinLayout::rectangular(32, 52, 32, 100, 60);
    const std::vector<float> mask = rect.reachableMask();
    // Pixel centres inside [1, 51] x [1, 31] are reachable.
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 52; ++x) {
            const bool inside = x >= 1 && x <= 50 && y >= 1 && y <= 30;
            QCOMPARE(mask[static_cast<std::size_t>(y) * 52 + x], inside ? 1.0f : 0.0f);
        }
    }
    // Circle: mask area approximates pi r^2 (inscribed polygon slightly smaller).
    const PinLayout circle = PinLayout::circular(360, 200, 200);
    double area = 0.0;
    for (float v : circle.reachableMask())
        area += v;
    const double expected = std::numbers::pi * 99.0 * 99.0;
    QVERIFY(std::abs(area - expected) / expected < 0.01);
}

QTEST_GUILESS_MAIN(TestGeometry)
#include "tst_geometry.moc"
