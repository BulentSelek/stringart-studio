#include "algorithms/GreedyOptimizer.h"
#include "algorithms/LeastSquaresOptimizer.h"
#include "core/Compositing.h"
#include "core/ImageProcessor.h"
#include "core/LineRasterizer.h"

#include "SyntheticImages.h"

#include <QTest>

#include <cmath>
#include <stdexcept>
#include <stop_token>

using namespace sa;

namespace {

OptimizationProblem makeProblem(int size, int pins)
{
    const GrayImage gray = ImageProcessor::prepareBase(synthetic::portrait(256), size, size);
    return OptimizationProblem::fromImage(gray, PinLayout::circular(pins, size, size));
}

} // namespace

class TestLeastSquares : public QObject
{
    Q_OBJECT

private slots:
    void satisfiesKktConditions();
    void multiplicitiesAreInitialWeights();
    void cancellationAndInvalidInput();
};

void TestLeastSquares::satisfiesKktConditions()
{
    const OptimizationProblem problem = makeProblem(96, 80);
    OptimizationConfig config;
    config.minPinSeparation = 8;
    config.maxSegments = 400;
    const StringArtResult greedy = GreedyOptimizer().optimize(problem, config);

    LeastSquaresSettings settings;
    settings.maxIterations = 2000;
    settings.tolerance = 1e-12;
    settings.maxWeight = 3.0;
    const LeastSquaresResult ls = LeastSquaresOptimizer::solve(problem, config, greedy.sequence, settings);
    QVERIFY(!ls.lines.empty());
    QVERIFY(ls.finalObjective <= ls.initialObjective);
    QVERIFY(ls.finalObjective < 0.9 * ls.initialObjective);
    qInfo("density SSE %.3f -> %.3f in %d sweeps; virtual error %.5f vs thread error %.5f", ls.initialObjective,
          ls.finalObjective, ls.iterations, ls.virtualError, greedy.finalError);

    // Independent residual from the returned weights.
    std::vector<double> residual(problem.target.size());
    for (std::size_t p = 0; p < residual.size(); ++p)
        residual[p] = std::log(std::max<double>(srgbToLinear(problem.target[p]), settings.minTransmittance));
    std::vector<std::vector<std::pair<std::uint32_t, double>>> columns(ls.lines.size());
    std::vector<LineSample> samples;
    for (std::size_t l = 0; l < ls.lines.size(); ++l) {
        QVERIFY(ls.weights[l] >= 0.0 && ls.weights[l] <= settings.maxWeight);
        rasterizePinLine(problem.pins, ls.lines[l].first, ls.lines[l].second, config.threadWidth, problem.width,
                         problem.height, samples);
        for (const LineSample &s : samples) {
            const double d = -std::log(1.0f - config.threadOpacity * s.coverage);
            columns[l].push_back({s.index, d});
            residual[s.index] += ls.weights[l] * d;
        }
    }
    // Virtual image is exp(-density).
    for (std::size_t p = 0; p < residual.size(); p += 97) {
        const double density = residual[p] - std::log(std::max<double>(srgbToLinear(problem.target[p]), 1e-3));
        QVERIFY(std::abs(ls.transmittance[p] - std::exp(-density)) < 1e-5);
    }
    // KKT: zero gradient for interior weights, correct sign at the bounds.
    int interior = 0;
    for (std::size_t l = 0; l < columns.size(); ++l) {
        double g = 0.0;
        double h = 0.0;
        for (const auto &[index, d] : columns[l]) {
            g += problem.weights[index] * d * residual[index];
            h += problem.weights[index] * d * d;
        }
        const double tol = 1e-4 * std::max(1.0, h);
        if (ls.weights[l] <= 0.0) {
            QVERIFY2(g >= -tol, qPrintable(QStringLiteral("line %1 g %2").arg(l).arg(g)));
        } else if (ls.weights[l] >= settings.maxWeight) {
            QVERIFY2(g <= tol, qPrintable(QStringLiteral("line %1 g %2").arg(l).arg(g)));
        } else {
            ++interior;
            QVERIFY2(std::abs(g) <= tol, qPrintable(QStringLiteral("line %1 g %2").arg(l).arg(g)));
        }
    }
    QVERIFY(interior > 0);
}

void TestLeastSquares::multiplicitiesAreInitialWeights()
{
    const OptimizationProblem problem = makeProblem(64, 30);
    OptimizationConfig config;
    config.minPinSeparation = 3;
    const std::vector<int> sequence{0, 10, 0, 10, 20, 5};
    LeastSquaresSettings settings;
    settings.maxIterations = 0; // no optimization: weights stay at their start values
    const LeastSquaresResult ls = LeastSquaresOptimizer::solve(problem, config, sequence, settings);
    QCOMPARE(ls.lines.size(), std::size_t{3});
    QCOMPARE(ls.lines[0], std::make_pair(0, 10));
    QCOMPARE(ls.initialWeights, (std::vector<double>{3.0, 1.0, 1.0}));
    QCOMPARE(ls.weights, ls.initialWeights);
    QCOMPARE(ls.finalObjective, ls.initialObjective);
}

void TestLeastSquares::cancellationAndInvalidInput()
{
    const OptimizationProblem problem = makeProblem(64, 30);
    OptimizationConfig config;
    config.minPinSeparation = 3;
    std::stop_source source;
    source.request_stop();
    const LeastSquaresResult cancelled =
        LeastSquaresOptimizer::solve(problem, config, {0, 10, 20}, {}, source.get_token());
    QVERIFY(cancelled.cancelled);
    QCOMPARE(cancelled.iterations, 0);

    auto solve = [&](std::vector<int> sequence, OptimizationConfig c, LeastSquaresSettings s) {
        static_cast<void>(LeastSquaresOptimizer::solve(problem, c, sequence, s));
    };
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, solve({0, 0}, config, {}));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, solve({0, 99}, config, {}));
    OptimizationConfig opaque = config;
    opaque.threadOpacity = 1.0f;
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, solve({0, 10}, opaque, {}));
    opaque.threadWidth = 0.6; // never covers a whole pixel: finite densities
    QVERIFY(std::isfinite(LeastSquaresOptimizer::solve(problem, opaque, {0, 10, 20}).virtualError));
    QCOMPARE(maxLineCoverage(0.5), 0.5 * std::sqrt(2.0));
    QCOMPARE(maxLineCoverage(2.0), 1.0);
    LeastSquaresSettings bad;
    bad.maxWeight = 0.0;
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, solve({0, 10}, config, bad));
}

QTEST_GUILESS_MAIN(TestLeastSquares)
#include "tst_leastsquares.moc"
