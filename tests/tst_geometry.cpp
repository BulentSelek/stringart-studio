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

QTEST_GUILESS_MAIN(TestGeometry)
#include "tst_geometry.moc"
