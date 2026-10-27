#include "algorithms/LayeredColorOptimizer.h"
#include "algorithms/PaletteSuggester.h"
#include "core/ImageProcessor.h"
#include "core/OptimizationProblem.h"

#include <QPainter>
#include <QTest>

#include <stdexcept>

using namespace sa;

namespace {

// Red left half, blue right half, black disk in the middle, on white.
QImage halves(int size)
{
    QImage image(size, size, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter p(&image);
    p.fillRect(QRect(0, 0, size / 2, size), QColor(210, 40, 40));
    p.fillRect(QRect(size / 2, 0, size - size / 2, size), QColor(40, 60, 210));
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::black);
    p.drawEllipse(QPointF(size / 2.0, size / 2.0), size / 8.0, size / 8.0);
    return image;
}

bool reddish(const QColor &c)
{
    return c.red() > c.blue() + 60 && c.red() > c.green() + 60;
}

bool bluish(const QColor &c)
{
    return c.blue() > c.red() + 60 && c.blue() > c.green() + 30;
}

bool dark(const QColor &c)
{
    return c.red() < 60 && c.green() < 60 && c.blue() < 60;
}

} // namespace

class TestPalette : public QObject
{
    Q_OBJECT

private slots:
    void candidatesFollowImageColours();
    void uniformImageGivesOppositeOfBackground();
    void darkBackgroundGivesWhite();
    void suggestFindsTheColours();
    void suggestIsDeterministic();
    void suggestBeatsAMismatchedPalette();
    void stopReturnsEarly();
    void invalidArguments();

private:
    static constexpr int kSize = 96;
    FrameSpec frame() const { return FrameSpec{PinLayoutKind::Circular, 100, 400.0, 400.0, {}}; }
    OptimizationConfig config() const
    {
        OptimizationConfig c;
        c.maxSegments = 800;
        c.minPinSeparation = 10;
        return c;
    }
    RgbImage target() const
    {
        const PinLayout layout = PinLayout::fromFrame(frame(), kSize);
        return ImageProcessor::prepareBaseRgb(halves(300), layout.canvasWidth(), layout.canvasHeight(), {}, 0.0);
    }
};

void TestPalette::candidatesFollowImageColours()
{
    const RgbImage image = target();
    const std::vector<float> weights(image.channels[0].size(), 1.0f);
    const std::vector<QColor> pool = PaletteSuggester::candidates(image.channels, weights, 3, Qt::white);
    QVERIFY(!pool.empty());
    QCOMPARE(pool.front(), QColor(Qt::black)); // opposite of the background
    QVERIFY(std::any_of(pool.begin(), pool.end(), reddish));
    QVERIFY(std::any_of(pool.begin(), pool.end(), bluish));
    QVERIFY(static_cast<int>(pool.size()) <= 2 * (3 + PaletteSuggestionSettings{}.extraClusters) + 1);
    for (std::size_t i = 0; i < pool.size(); ++i)
        QVERIFY(std::count(pool.begin(), pool.end(), pool[i]) == 1);
}

void TestPalette::uniformImageGivesOppositeOfBackground()
{
    std::array<std::vector<float>, 3> white;
    for (auto &plane : white)
        plane.assign(400, 1.0f);
    const std::vector<float> weights(400, 1.0f);
    const std::vector<QColor> pool = PaletteSuggester::candidates(white, weights, 4, Qt::white);
    QCOMPARE(pool.size(), std::size_t{1});
    QCOMPARE(pool.front(), QColor(Qt::black));
}

void TestPalette::darkBackgroundGivesWhite()
{
    std::array<std::vector<float>, 3> grey;
    for (auto &plane : grey)
        plane.assign(400, 0.8f);
    const std::vector<float> weights(400, 1.0f);
    const std::vector<QColor> pool = PaletteSuggester::candidates(grey, weights, 2, QColor(20, 20, 20));
    QCOMPARE(pool.front(), QColor(Qt::white));
    // Every candidate lies on a ray from the background: lighter than it here.
    for (const QColor &c : pool)
        QVERIFY(c.lightness() > 100);
}

void TestPalette::suggestFindsTheColours()
{
    const std::vector<QColor> palette =
        PaletteSuggester::suggest(target(), frame(), config(), kSize, 3, Qt::white);
    QCOMPARE(palette.size(), std::size_t{3});
    QVERIFY(std::any_of(palette.begin(), palette.end(), reddish));
    QVERIFY(std::any_of(palette.begin(), palette.end(), bluish));
    QVERIFY(std::any_of(palette.begin(), palette.end(), dark));
}

void TestPalette::suggestIsDeterministic()
{
    const RgbImage image = target();
    int calls = 0;
    int lastTotal = 0;
    const std::vector<QColor> a = PaletteSuggester::suggest(image, frame(), config(), kSize, 2, Qt::white, LayerOrder::Auto, {},
                                                            [&](int done, int total) {
                                                                ++calls;
                                                                QVERIFY(done <= total);
                                                                lastTotal = total;
                                                            });
    const std::vector<QColor> b = PaletteSuggester::suggest(image, frame(), config(), kSize, 2, Qt::white);
    QCOMPARE(a, b);
    QCOMPARE(calls, lastTotal); // one report per proxy run
}

void TestPalette::suggestBeatsAMismatchedPalette()
{
    // Full-size check of the proxy's choice against greens, which this image lacks.
    const RgbImage image = target();
    const PinLayout layout = PinLayout::fromFrame(frame(), kSize);
    OptimizationProblem problem;
    problem.width = layout.canvasWidth();
    problem.height = layout.canvasHeight();
    problem.weights = layout.reachableMask();
    problem.target.assign(problem.weights.size(), 1.0f);
    problem.pins = layout;
    auto error = [&](const std::vector<QColor> &palette) {
        LayeredColorOptimizer optimizer(image.channels, palette, Qt::white);
        return optimizer.optimize(problem, config()).finalError;
    };
    const std::vector<QColor> suggested = PaletteSuggester::suggest(image, frame(), config(), kSize, 3, Qt::white);
    const double mismatched = error({QColor(0, 120, 0), QColor(120, 200, 60), QColor(0, 80, 40)});
    const double withSuggestion = error(suggested);
    qInfo("suggested %.5f vs mismatched %.5f", withSuggestion, mismatched);
    QVERIFY(withSuggestion < mismatched);
}

void TestPalette::stopReturnsEarly()
{
    std::stop_source stop;
    stop.request_stop();
    const std::vector<QColor> palette =
        PaletteSuggester::suggest(target(), frame(), config(), kSize, 3, Qt::white, LayerOrder::Auto, stop.get_token());
    QVERIFY(palette.empty());
}

void TestPalette::invalidArguments()
{
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                             (void)PaletteSuggester::suggest(target(), frame(), config(), kSize, 0, Qt::white));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                             (void)PaletteSuggester::suggest(target(), frame(), config(), kSize, 9, Qt::white));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                             (void)PaletteSuggester::suggest(RgbImage{}, frame(), config(), kSize, 2, Qt::white));
    const std::array<std::vector<float>, 3> planes{std::vector<float>(10), std::vector<float>(10),
                                                   std::vector<float>(9)};
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                             (void)PaletteSuggester::candidates(planes, std::vector<float>(10), 2, Qt::white));
}

QTEST_MAIN(TestPalette)
#include "tst_palette.moc"
