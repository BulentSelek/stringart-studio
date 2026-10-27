#include "algorithms/ColorGreedyOptimizer.h"
#include "algorithms/GreedyOptimizer.h"
#include "core/ColorRenderer.h"
#include "core/ImageProcessor.h"

#include "SyntheticImages.h"

#include <QPainter>
#include <QTest>

#include <set>
#include <stdexcept>

using namespace sa;

namespace {

struct ColorSetup
{
    OptimizationProblem problem;
    std::array<std::vector<float>, 3> target;
};

ColorSetup makeSetup(const QImage &image, int size, int pins)
{
    ColorSetup setup;
    const GrayImage gray = ImageProcessor::prepareBase(image, size, size);
    setup.problem = OptimizationProblem::fromImage(gray, PinLayout::circular(pins, size, size));
    setup.target = ImageProcessor::prepareBaseRgb(image, size, size).channels;
    return setup;
}

QImage twoColorImage(int size)
{
    QImage image(size, size, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter p(&image);
    p.fillRect(QRectF(0, 0, size * 0.5, size), QColor(200, 30, 30));    // red left half
    p.fillRect(QRectF(size * 0.5, 0, size * 0.5, size), QColor(30, 40, 200)); // blue right half
    p.end();
    return image;
}

void verifyThreads(const StringArtResult &result, const PinLayout &pins, const OptimizationConfig &config,
                   int colors)
{
    std::vector<int> current(static_cast<std::size_t>(colors), config.startPin);
    std::vector<std::set<std::pair<int, int>>> used(static_cast<std::size_t>(colors));
    for (const ColorStep &step : result.colorSteps) {
        QVERIFY(step.color >= 0 && step.color < colors);
        // Each colour is one continuous thread.
        QCOMPARE(step.from, current[static_cast<std::size_t>(step.color)]);
        QVERIFY(pins.canConnect(step.from, step.to));
        QVERIFY(pins.separation(step.from, step.to) >= config.minPinSeparation);
        QVERIFY(used[static_cast<std::size_t>(step.color)].insert(std::minmax(step.from, step.to)).second);
        current[static_cast<std::size_t>(step.color)] = step.to;
    }
}

} // namespace

class TestColor : public QObject
{
    Q_OBJECT

private slots:
    void singleBlackMatchesGrayscale();
    void matchesBruteForce();
    void twoColourImage();
    void renderMatchesOptimizer();
    void invalidInput();
};

void TestColor::singleBlackMatchesGrayscale()
{
    const QImage image = synthetic::portrait(256);
    ColorSetup setup = makeSetup(image, 96, 80);
    // Use the gray image as all three channels so both problems are identical.
    for (auto &plane : setup.target)
        plane = setup.problem.target;
    OptimizationConfig config;
    config.maxSegments = 300;
    config.minPinSeparation = 8;
    const StringArtResult gray = GreedyOptimizer().optimize(setup.problem, config);
    const StringArtResult color =
        ColorGreedyOptimizer(setup.target, {Qt::black}, Qt::white).optimize(setup.problem, config);
    QCOMPARE(color.colorSteps.size(), gray.sequence.size() - 1);
    for (std::size_t i = 0; i < color.colorSteps.size(); ++i) {
        QCOMPARE(color.colorSteps[i].from, gray.sequence[i]);
        QCOMPARE(color.colorSteps[i].to, gray.sequence[i + 1]);
    }
    QVERIFY(std::abs(color.finalError - gray.finalError) < 1e-6);
}

void TestColor::matchesBruteForce()
{
    const ColorSetup setup = makeSetup(twoColorImage(64), 32, 18);
    OptimizationConfig config;
    config.maxSegments = 12;
    config.minPinSeparation = 3;
    config.threadOpacity = 0.4f;
    const std::vector<QColor> palette{QColor(200, 30, 30), QColor(30, 40, 200), Qt::black};
    const StringArtResult result =
        ColorGreedyOptimizer(setup.target, palette, Qt::white).optimize(setup.problem, config);

    // Reference: render every candidate step and evaluate the full objective.
    std::vector<ColorStep> steps;
    std::vector<int> current(palette.size(), 0);
    std::vector<std::set<std::pair<int, int>>> used(palette.size());
    const PinLayout &pins = setup.problem.pins;
    for (int s = 0; s < config.maxSegments; ++s) {
        double bestError = std::numeric_limits<double>::infinity();
        ColorStep best{-1, -1, -1};
        for (int c = 0; c < static_cast<int>(palette.size()); ++c) {
            for (int j = 0; j < pins.pinCount(); ++j) {
                const int from = current[static_cast<std::size_t>(c)];
                if (pins.separation(from, j) < 3 || used[static_cast<std::size_t>(c)].contains(std::minmax(from, j)))
                    continue;
                std::vector<ColorStep> trial = steps;
                trial.push_back({c, from, j});
                const std::vector<float> rgb =
                    ColorRenderer::renderRgb(pins, trial, palette, Qt::white, config.threadWidth, config.threadOpacity);
                const double error = ColorGreedyOptimizer::weightedSse(rgb, setup.target, setup.problem.weights);
                if (error < bestError - 1e-9) {
                    bestError = error;
                    best = {c, from, j};
                }
            }
        }
        steps.push_back(best);
        used[static_cast<std::size_t>(best.color)].insert(std::minmax(best.from, best.to));
        current[static_cast<std::size_t>(best.color)] = best.to;
    }
    QCOMPARE(result.colorSteps, steps);
}

void TestColor::twoColourImage()
{
    const ColorSetup setup = makeSetup(twoColorImage(256), 128, 120);
    OptimizationConfig config;
    config.maxSegments = 1500;
    config.minPinSeparation = 10;
    const std::vector<QColor> palette{QColor(200, 30, 30), QColor(30, 40, 200)};
    const StringArtResult result =
        ColorGreedyOptimizer(setup.target, palette, Qt::white).optimize(setup.problem, config);
    verifyThreads(result, setup.problem.pins, config, 2);
    QVERIFY(result.finalError < 0.6 * result.initialError);

    // Red threads mostly end on the left half, blue ones on the right.
    int redLeft = 0, redCount = 0, blueRight = 0, blueCount = 0;
    for (const ColorStep &step : result.colorSteps) {
        const double midX = (setup.problem.pins.pin(step.from).x + setup.problem.pins.pin(step.to).x) / 2.0;
        if (step.color == 0) {
            ++redCount;
            redLeft += midX < 64.0;
        } else {
            ++blueCount;
            blueRight += midX > 64.0;
        }
    }
    QVERIFY(redCount > 50 && blueCount > 50);
    QVERIFY2(redLeft > 0.8 * redCount && blueRight > 0.8 * blueCount,
             qPrintable(QStringLiteral("red %1/%2 blue %3/%4").arg(redLeft).arg(redCount).arg(blueRight).arg(blueCount)));
    // Centre of each half shows its colour.
    const QImage image = ColorRenderer::toImage(result.rgb, 128, 128);
    const QColor left = image.pixelColor(30, 64);
    const QColor right = image.pixelColor(98, 64);
    QVERIFY(left.red() > left.blue());
    QVERIFY(right.blue() > right.red());
}

void TestColor::renderMatchesOptimizer()
{
    const ColorSetup setup = makeSetup(synthetic::portrait(128), 96, 60);
    OptimizationConfig config;
    config.maxSegments = 400;
    config.minPinSeparation = 6;
    config.threadWidth = 1.4;
    const std::vector<QColor> palette{Qt::black, QColor(150, 90, 60), Qt::white};
    const StringArtResult result =
        ColorGreedyOptimizer(setup.target, palette, QColor(128, 128, 128)).optimize(setup.problem, config);
    verifyThreads(result, setup.problem.pins, config, 3);
    QCOMPARE(result.rgb, ColorRenderer::renderRgb(setup.problem.pins, result.colorSteps, palette,
                                                  QColor(128, 128, 128), config.threadWidth, config.threadOpacity));
    QVERIFY(result.finalError < result.initialError);
    // White thread is useful on a gray background (it can brighten).
    QVERIFY(std::any_of(result.colorSteps.begin(), result.colorSteps.end(),
                        [](const ColorStep &s) { return s.color == 2; }));
}

void TestColor::invalidInput()
{
    const ColorSetup setup = makeSetup(synthetic::portrait(64), 32, 20);
    OptimizationConfig config;
    config.minPinSeparation = 3;
    auto run = [&](std::vector<QColor> palette, std::array<std::vector<float>, 3> target) {
        static_cast<void>(ColorGreedyOptimizer(target, palette, Qt::white).optimize(setup.problem, config));
    };
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, run({}, setup.target));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, run(std::vector<QColor>(9, Qt::black), setup.target));
    auto wrong = setup.target;
    wrong[1].pop_back();
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, run({Qt::black}, wrong));
}

QTEST_GUILESS_MAIN(TestColor)
#include "tst_color.moc"
