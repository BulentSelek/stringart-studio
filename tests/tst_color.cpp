#include "algorithms/GreedyOptimizer.h"
#include "algorithms/LayeredColorOptimizer.h"
#include "core/ColorRenderer.h"
#include "core/Compositing.h"
#include "core/ExportManager.h"
#include "core/PlaybackRenderer.h"
#include "core/ProjectFile.h"
#include "core/ImageProcessor.h"

#include "SyntheticImages.h"

#include <QPainter>
#include <QTest>

#include <limits>
#include <numeric>
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
    void layerMatchesBruteForce();
    void whiteBrightensGreyBoard();
    void invalidInput();
    void playbackMatchesRenderer();
    void colourExports();
    void colourProjectRoundTrip();
    void layeredWoundInBlocks();
    void layeredRefinementNeverWorse();
    void layerOrders();
    void colourSeparation();
    void layeredCancel();
    void layeredProjectSettings();
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
    LayeredColorSettings settings;
    settings.refinePasses = 0; // one layer, whole budget: plain greedy on the RGB objective
    const StringArtResult color =
        LayeredColorOptimizer(setup.target, {Qt::black}, Qt::white, settings).optimize(setup.problem, config);
    QCOMPARE(color.colorSteps.size(), gray.sequence.size() - 1);
    for (std::size_t i = 0; i < color.colorSteps.size(); ++i) {
        QCOMPARE(color.colorSteps[i].from, gray.sequence[i]);
        QCOMPARE(color.colorSteps[i].to, gray.sequence[i + 1]);
    }
    QVERIFY(std::abs(color.finalError - gray.finalError) < 1e-6);
}

void TestColor::layerMatchesBruteForce()
{
    const ColorSetup setup = makeSetup(twoColorImage(64), 32, 18);
    OptimizationConfig config;
    config.maxSegments = 14;
    config.minPinSeparation = 3;
    config.threadOpacity = 0.4f;
    const std::vector<QColor> palette{QColor(200, 30, 30), QColor(30, 40, 200), Qt::black};
    LayeredColorSettings settings;
    settings.order = LayerOrder::Palette;
    settings.refinePasses = 0;
    const StringArtResult result =
        LayeredColorOptimizer(setup.target, palette, Qt::white, settings).optimize(setup.problem, config);

    // Reference: each layer picks, step by step, the line whose full re-render
    // has the lowest error (layers above do not exist yet when it is built).
    // Layer lengths are taken from the result (they come from the budgets).
    std::vector<int> lengths(palette.size(), 0);
    for (const ColorStep &step : result.colorSteps)
        ++lengths[static_cast<std::size_t>(step.color)];
    std::vector<ColorStep> steps;
    const PinLayout &pins = setup.problem.pins;
    for (int c = 0; c < static_cast<int>(palette.size()); ++c) {
        int current = config.startPin;
        std::set<std::pair<int, int>> used;
        for (int s = 0; s < lengths[static_cast<std::size_t>(c)]; ++s) {
            double bestError = std::numeric_limits<double>::infinity();
            ColorStep best{-1, -1, -1};
            for (int j = 0; j < pins.pinCount(); ++j) {
                if (pins.separation(current, j) < 3 || used.contains(std::minmax(current, j)))
                    continue;
                std::vector<ColorStep> trial = steps;
                trial.push_back({c, current, j});
                const std::vector<float> rgb =
                    ColorRenderer::renderRgb(pins, trial, palette, Qt::white, config.threadWidth, config.threadOpacity);
                const double error = LayeredColorOptimizer::weightedSse(rgb, setup.target, setup.problem.weights);
                if (error < bestError - 1e-9) {
                    bestError = error;
                    best = {c, current, j};
                }
            }
            steps.push_back(best);
            used.insert(std::minmax(best.from, best.to));
            current = best.to;
        }
    }
    QCOMPARE(result.colorSteps, steps);
}

void TestColor::whiteBrightensGreyBoard()
{
    const ColorSetup setup = makeSetup(synthetic::portrait(128), 96, 60);
    OptimizationConfig config;
    config.maxSegments = 400;
    config.minPinSeparation = 6;
    config.threadWidth = 1.4;
    const std::vector<QColor> palette{Qt::black, QColor(150, 90, 60), Qt::white};
    const StringArtResult result =
        LayeredColorOptimizer(setup.target, palette, QColor(128, 128, 128)).optimize(setup.problem, config);
    verifyThreads(result, setup.problem.pins, config, 3);
    const std::vector<float> rendered = ColorRenderer::renderRgb(setup.problem.pins, result.colorSteps, palette,
                                                                 QColor(128, 128, 128), config.threadWidth,
                                                                 config.threadOpacity);
    for (std::size_t i = 0; i < rendered.size(); ++i)
        QVERIFY(std::abs(rendered[i] - result.rgb[i]) < 1e-4f);
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
        static_cast<void>(LayeredColorOptimizer(target, palette, Qt::white).optimize(setup.problem, config));
    };
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, run({}, setup.target));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, run(std::vector<QColor>(9, Qt::black), setup.target));
    auto wrong = setup.target;
    wrong[1].pop_back();
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, run({Qt::black}, wrong));
}

namespace {

std::vector<ColorStep> sampleSteps()
{
    // Two threads starting at pin 0 (the format allows any order of colours).
    return {{0, 0, 10}, {1, 0, 15}, {0, 10, 25}, {1, 15, 3}, {0, 25, 8}};
}

} // namespace

void TestColor::playbackMatchesRenderer()
{
    const PinLayout layout = PinLayout::circular(30, 64, 64);
    const std::vector<QColor> palette{QColor(200, 0, 0), QColor(0, 0, 200)};
    std::vector<ColorStep> steps;
    std::vector<int> current{0, 0};
    for (int i = 0; i < 200; ++i) {
        const int c = (i * 7) % 2;
        const int next = (current[static_cast<std::size_t>(c)] + 11 + i % 5) % 30;
        steps.push_back({c, current[static_cast<std::size_t>(c)], next});
        current[static_cast<std::size_t>(c)] = next;
    }
    ColorPlaybackRenderer playback(layout, steps, palette, QColor(240, 230, 200), 1.2, 0.3f);
    ColorPlaybackRenderer lazy(layout, steps, palette, QColor(240, 230, 200), 1.2, 0.3f,
                               ColorRenderer::renderRgb(layout, steps, palette, QColor(240, 230, 200), 1.2, 0.3f));
    for (int target : {200, 10, 200}) {
        const auto a = lazy.seek(target);
        const std::vector<float> reference = ColorRenderer::renderRgb(layout, steps, palette, QColor(240, 230, 200),
                                                                      1.2, 0.3f, 1.0, static_cast<std::size_t>(target));
        QVERIFY(std::equal(a.begin(), a.end(), reference.begin(), reference.end()));
    }
    for (int target : {200, 0, 1, 77, 76, 150, 3}) {
        const auto rgb = playback.seek(target);
        const std::vector<float> reference = ColorRenderer::renderRgb(layout, steps, palette, QColor(240, 230, 200),
                                                                      1.2, 0.3f, 1.0, static_cast<std::size_t>(target));
        QVERIFY2(std::equal(rgb.begin(), rgb.end(), reference.begin(), reference.end()),
                 qPrintable(QString::number(target)));
    }
}

void TestColor::colourExports()
{
    const PinLayout layout = PinLayout::circular(30, 102, 102, 100.0);
    const std::vector<QColor> palette{QColor(200, 0, 0), QColor(0, 0, 200)};
    const std::vector<ColorStep> steps = sampleSteps();
    const QStringList csv =
        ExportManager::colorCsvText(layout, steps, palette, LengthUnit::Millimeters).split(u'\n', Qt::SkipEmptyParts);
    QCOMPARE(csv.size(), 6);
    QVERIFY(csv[0].startsWith(QStringLiteral("step,color,color_hex,from_pin,to_pin")));
    QVERIFY(csv[2].startsWith(QStringLiteral("2,1,#0000c8,0,15,")));
    // Cumulative length is per colour.
    const double first = csv[1].split(u',')[9].toDouble();
    const double third = csv[3].split(u',')[9].toDouble();
    QVERIFY(std::abs(csv[3].split(u',')[10].toDouble() - (first + third)) < 0.02);

    const QString svg = ExportManager::colorSvgText(layout, steps, palette, Qt::white, 1.0, 0.3f, true);
    QCOMPARE(svg.count(QStringLiteral("<line ")), 5);
    QCOMPARE(svg.count(QStringLiteral("stroke=\"#c80000\"")), 3);
    QCOMPARE(svg.count(QStringLiteral("stroke=\"#0000c8\"")), 2);
    QCOMPARE(svg.count(QStringLiteral("<circle ")), 30);
    QVERIFY(svg.trimmed().endsWith(QStringLiteral("</svg>")));
}

void TestColor::colourProjectRoundTrip()
{
    ProjectData project;
    project.frame.pinCount = 30;
    project.config.minPinSeparation = 1;
    project.colorMode = true;
    project.palette = {QColor(200, 0, 0), QColor(0, 0, 200)};
    project.colorBackground = QColor(240, 230, 200);
    project.colorSteps = sampleSteps();
    const ProjectData loaded = ProjectFile::fromJson(ProjectFile::toJson(project, false, false));
    QVERIFY(loaded.colorMode);
    QCOMPARE(loaded.palette, project.palette);
    QCOMPARE(loaded.colorBackground, project.colorBackground);
    QCOMPARE(loaded.colorSteps, project.colorSteps);
    QVERIFY(loaded.sequence.empty());

    // Broken continuity is rejected.
    project.colorSteps[2].from = 11;
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                             static_cast<void>(ProjectFile::fromJson(ProjectFile::toJson(project, false, false))));
    // Single-colour projects carry no colour section.
    ProjectData mono;
    mono.frame.pinCount = 30;
    mono.config.minPinSeparation = 1;
    mono.sequence = {0, 10, 20};
    QVERIFY(!ProjectFile::toJson(mono, false, false).contains("\"color\""));
    QVERIFY(!ProjectFile::fromJson(ProjectFile::toJson(mono, false, false)).colorMode);
}

namespace {

// Red left half, blue right half, black disk in the middle.
QImage halvesWithDisk(int size)
{
    QImage image = twoColorImage(size);
    QPainter p(&image);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::black);
    p.drawEllipse(QPointF(size / 2.0, size / 2.0), size / 7.0, size / 7.0);
    return image;
}

int colourChanges(const std::vector<ColorStep> &steps)
{
    int changes = 0;
    for (std::size_t i = 1; i < steps.size(); ++i)
        changes += steps[i].color != steps[i - 1].color ? 1 : 0;
    return changes;
}

} // namespace

void TestColor::layeredWoundInBlocks()
{
    const ColorSetup setup = makeSetup(halvesWithDisk(256), 128, 120);
    OptimizationConfig config;
    config.maxSegments = 1800;
    config.minPinSeparation = 10;
    const std::vector<QColor> palette{QColor(200, 30, 30), QColor(30, 40, 200), Qt::black};
    const StringArtResult result =
        LayeredColorOptimizer(setup.target, palette, Qt::white).optimize(setup.problem, config);
    verifyThreads(result, setup.problem.pins, config, 3);
    QVERIFY(static_cast<int>(result.colorSteps.size()) <= config.maxSegments);
    QVERIFY(colourChanges(result.colorSteps) <= 2);
    // Each colour forms one block (wound once), in light-to-dark or dark-to-light order.
    std::vector<int> blocks;
    for (const ColorStep &step : result.colorSteps) {
        if (blocks.empty() || blocks.back() != step.color)
            blocks.push_back(step.color);
    }
    std::vector<int> sorted = blocks;
    std::sort(sorted.begin(), sorted.end());
    QVERIFY(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());
    const auto light = LayeredColorOptimizer::layerOrder(palette, LayerOrder::LightToDark);
    const auto dark = LayeredColorOptimizer::layerOrder(palette, LayerOrder::DarkToLight);
    auto follows = [&blocks](const std::vector<int> &order) {
        std::vector<int> kept;
        std::copy_if(order.begin(), order.end(), std::back_inserter(kept),
                     [&blocks](int c) { return std::find(blocks.begin(), blocks.end(), c) != blocks.end(); });
        return kept == blocks;
    };
    QVERIFY(follows(light) || follows(dark));
    // The returned picture is what the renderer makes of the steps.
    const std::vector<float> rendered = ColorRenderer::renderRgb(setup.problem.pins, result.colorSteps, palette,
                                                                 Qt::white, config.threadWidth, config.threadOpacity);
    QCOMPARE(rendered.size(), result.rgb.size());
    float maxDifference = 0.0f;
    for (std::size_t i = 0; i < rendered.size(); ++i)
        maxDifference = std::max(maxDifference, std::abs(rendered[i] - result.rgb[i]));
    QVERIFY2(maxDifference < 1e-4f, qPrintable(QString::number(maxDifference)));
    QVERIFY(std::abs(result.finalError - LayeredColorOptimizer::weightedSse(rendered, setup.target,
                                                                          setup.problem.weights)
                                             / (3.0 * std::accumulate(setup.problem.weights.begin(),
                                                                      setup.problem.weights.end(), 0.0)))
            < 1e-6);
    QVERIFY(result.finalError < 0.6 * result.initialError);
    // Colours land where they belong, and the disk is dark.
    const QImage image = ColorRenderer::toImage(result.rgb, 128, 128);
    QVERIFY(image.pixelColor(25, 64).red() > image.pixelColor(25, 64).blue() + 30);
    QVERIFY(image.pixelColor(103, 64).blue() > image.pixelColor(103, 64).red() + 30);
    QVERIFY2(image.pixelColor(64, 64).lightness() < 110, qPrintable(image.pixelColor(64, 64).name()));
}

void TestColor::layeredRefinementNeverWorse()
{
    const ColorSetup setup = makeSetup(halvesWithDisk(200), 96, 90);
    OptimizationConfig config;
    config.maxSegments = 900;
    config.minPinSeparation = 8;
    const std::vector<QColor> palette{Qt::white, QColor(200, 30, 30), QColor(30, 40, 200), Qt::black};
    double previous = std::numeric_limits<double>::infinity();
    for (int passes : {0, 1, 2}) {
        LayeredColorSettings settings;
        settings.order = LayerOrder::LightToDark;
        settings.refinePasses = passes;
        const StringArtResult result =
            LayeredColorOptimizer(setup.target, palette, Qt::white, settings).optimize(setup.problem, config);
        verifyThreads(result, setup.problem.pins, config, 4);
        qInfo("passes %d: error %.5f, %zu steps", passes, result.finalError, result.colorSteps.size());
        QVERIFY(result.finalError <= previous);
        previous = result.finalError;
    }
    // Deterministic, also with a different thread count.
    LayeredColorSettings settings;
    settings.refinePasses = 1;
    config.threadCount = 1;
    const StringArtResult a =
        LayeredColorOptimizer(setup.target, palette, Qt::white, settings).optimize(setup.problem, config);
    config.threadCount = 4;
    const StringArtResult b =
        LayeredColorOptimizer(setup.target, palette, Qt::white, settings).optimize(setup.problem, config);
    QCOMPARE(a.colorSteps, b.colorSteps);
}

void TestColor::layerOrders()
{
    const std::vector<QColor> palette{QColor(200, 30, 30), Qt::white, Qt::black, QColor(240, 200, 0)};
    QCOMPARE(LayeredColorOptimizer::layerOrder(palette, LayerOrder::Palette), (std::vector<int>{0, 1, 2, 3}));
    QCOMPARE(LayeredColorOptimizer::layerOrder(palette, LayerOrder::LightToDark), (std::vector<int>{1, 3, 0, 2}));
    QCOMPARE(LayeredColorOptimizer::layerOrder(palette, LayerOrder::DarkToLight), (std::vector<int>{2, 0, 3, 1}));
    QCOMPARE(LayeredColorOptimizer::layerOrder(palette, LayerOrder::Auto),
             LayeredColorOptimizer::layerOrder(palette, LayerOrder::LightToDark));
}

void TestColor::colourSeparation()
{
    const int size = 64;
    const std::size_t pixels = static_cast<std::size_t>(size) * size;
    const std::vector<float> weights(pixels, 1.0f);
    // All red: the red map is full, black is empty.
    std::array<std::vector<float>, 3> red{std::vector<float>(pixels, 200 / 255.0f),
                                          std::vector<float>(pixels, 30 / 255.0f),
                                          std::vector<float>(pixels, 30 / 255.0f)};
    const auto maps = LayeredColorOptimizer::separate(red, weights, size, size,
                                                      std::vector<QColor>{QColor(200, 30, 30), Qt::black}, Qt::white,
                                                      1.0);
    QCOMPARE(maps.size(), std::size_t{2});
    QVERIFY(std::accumulate(maps[0].begin(), maps[0].end(), 0.0) > 0.99 * pixels);
    QVERIFY(std::accumulate(maps[1].begin(), maps[1].end(), 0.0) < 0.01 * pixels);
    // Grey of linear value 0.75 on white with black thread: dithering in linear
    // light makes a quarter of the pixels black.
    const float grey = linearToSrgb(0.75f);
    std::array<std::vector<float>, 3> greyPlanes{std::vector<float>(pixels, grey), std::vector<float>(pixels, grey),
                                                 std::vector<float>(pixels, grey)};
    const auto greyMaps = LayeredColorOptimizer::separate(greyPlanes, weights, size, size,
                                                          std::vector<QColor>{Qt::black}, Qt::white, 0.0);
    const double share = std::accumulate(greyMaps[0].begin(), greyMaps[0].end(), 0.0) / pixels;
    QVERIFY2(std::abs(share - 0.25) < 0.02, qPrintable(QString::number(share)));
    // Zero-weight pixels are board.
    std::vector<float> none(pixels, 0.0f);
    const auto empty = LayeredColorOptimizer::separate(greyPlanes, none, size, size, std::vector<QColor>{Qt::black},
                                                       Qt::white, 1.0);
    QCOMPARE(std::accumulate(empty[0].begin(), empty[0].end(), 0.0), 0.0);
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                             (void)LayeredColorOptimizer::separate(greyPlanes, weights, size + 1, size,
                                                                   std::vector<QColor>{Qt::black}, Qt::white, 1.0));
}

void TestColor::layeredCancel()
{
    const ColorSetup setup = makeSetup(halvesWithDisk(128), 64, 60);
    OptimizationConfig config;
    config.maxSegments = 400;
    config.minPinSeparation = 5;
    std::stop_source stop;
    stop.request_stop();
    const StringArtResult result =
        LayeredColorOptimizer(setup.target, {Qt::black, Qt::red}, Qt::white).optimize(setup.problem, config,
                                                                                     stop.get_token());
    QCOMPARE(result.stopReason, StopReason::Cancelled);
    QVERIFY(result.colorSteps.empty());
    QCOMPARE(result.finalError, result.initialError);
}

void TestColor::layeredProjectSettings()
{
    ProjectData project;
    project.frame.pinCount = 30;
    project.config.minPinSeparation = 1;
    project.colorMode = true;
    project.palette = {Qt::black, Qt::white};
    for (int order = 0; order < 4; ++order) {
        project.layerOrder = order;
        QCOMPARE(ProjectFile::fromJson(ProjectFile::toJson(project, false, false)).layerOrder, order);
    }
    QByteArray bad = ProjectFile::toJson(project, false, false);
    QVERIFY(bad.contains("\"palette\""));
    bad.replace("\"layerOrder\": \"palette\"", "\"layerOrder\": \"sideways\"");
    bad.replace("\"layerOrder\":\"palette\"", "\"layerOrder\":\"sideways\"");
    QVERIFY(bad.contains("sideways"));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, (void)ProjectFile::fromJson(bad));
}

QTEST_GUILESS_MAIN(TestColor)
#include "tst_color.moc"
