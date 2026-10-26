#include "algorithms/GreedyOptimizer.h"
#include "core/Compositing.h"
#include "core/ImageProcessor.h"
#include "core/LineRasterizer.h"
#include "core/ThreadRenderer.h"

#include "SyntheticImages.h"

#include <QTest>

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <stop_token>

using namespace sa;

namespace {

OptimizationProblem makeProblem(const QImage &image, int size, int pinCount)
{
    const GrayImage gray = ImageProcessor::prepareBase(image, size, size);
    return OptimizationProblem::fromImage(gray, PinLayout::circular(pinCount, size, size));
}

std::vector<float> encodeAll(const std::vector<float> &transmittance)
{
    std::vector<float> encoded(transmittance.size());
    std::transform(transmittance.begin(), transmittance.end(), encoded.begin(),
                   [](float t) { return SrgbEncoder::encode(t); });
    return encoded;
}

// Brute-force change of the objective: render the line into a copy of the
// full canvas and evaluate the complete weighted SSE before and after.
double bruteForceDelta(const OptimizationProblem &problem, const std::vector<float> &transmittance, int a, int b,
                       const OptimizationConfig &config)
{
    std::vector<LineSample> samples;
    rasterizePinLine(problem.pins, a, b, config.threadWidth, problem.width, problem.height, samples);
    std::vector<float> next = transmittance;
    for (const LineSample &s : samples)
        next[s.index] = applyThread(next[s.index], config.threadOpacity, s.coverage);
    return GreedyOptimizer::weightedSse(next, problem.target, problem.weights)
         - GreedyOptimizer::weightedSse(transmittance, problem.target, problem.weights);
}

// Reference greedy optimizer using only brute-force full-image evaluation.
std::vector<int> bruteForceGreedy(const OptimizationProblem &problem, const OptimizationConfig &config)
{
    std::vector<float> transmittance(problem.target.size(), 1.0f);
    std::vector<int> sequence{config.startPin};
    std::set<std::pair<int, int>> used;
    std::vector<LineSample> samples;
    int current = config.startPin;
    for (int segment = 0; segment < config.maxSegments; ++segment) {
        double bestDelta = std::numeric_limits<double>::infinity();
        int bestPin = -1;
        for (int j = 0; j < problem.pins.pinCount(); ++j) {
            if (problem.pins.separation(current, j) < config.minPinSeparation)
                continue;
            if (used.contains({std::min(current, j), std::max(current, j)}))
                continue;
            const double delta = bruteForceDelta(problem, transmittance, current, j, config);
            if (delta < bestDelta) {
                bestDelta = delta;
                bestPin = j;
            }
        }
        if (bestPin < 0 || !(bestDelta < 0.0))
            break;
        rasterizePinLine(problem.pins, current, bestPin, config.threadWidth, problem.width, problem.height, samples);
        for (const LineSample &s : samples)
            transmittance[s.index] = applyThread(transmittance[s.index], config.threadOpacity, s.coverage);
        used.insert({std::min(current, bestPin), std::max(current, bestPin)});
        sequence.push_back(bestPin);
        current = bestPin;
    }
    return sequence;
}

} // namespace

class TestOptimizer : public QObject
{
    Q_OBJECT

private slots:
    void incrementalScoreMatchesBruteForce();
    void matchesBruteForceReference();
    void errorDecreasesMonotonically();
    void sequenceIsContinuousAndValid();
    void repeatedLinesAllowed();
    void deterministicAcrossThreadsAndCache();
    void transmittanceMatchesRenderer();
    void cancellation();
    void maxThreadLength();
    void blankImageProducesNoThreads();
    void blackImageUsesAllSegments();
    void zeroOpacityStopsImmediately();
    void insufficientConnections();
    void invalidConfiguration();
    void reconstructsSyntheticDisk();
};

void TestOptimizer::incrementalScoreMatchesBruteForce()
{
    const OptimizationProblem problem = makeProblem(synthetic::portrait(96), 48, 24);
    OptimizationConfig config;
    config.minPinSeparation = 2;
    config.threadOpacity = 0.4f;
    config.threadWidth = 1.3;
    config.maxSegments = 6;
    GreedyOptimizer optimizer;
    const StringArtResult partial = optimizer.optimize(problem, config);
    QCOMPARE(partial.segmentCount(), 6);

    const std::vector<float> encoded = encodeAll(partial.transmittance);
    const int current = partial.sequence.back();
    std::vector<LineSample> samples;
    for (int j = 0; j < problem.pins.pinCount(); ++j) {
        if (problem.pins.separation(current, j) < config.minPinSeparation)
            continue;
        rasterizePinLine(problem.pins, current, j, config.threadWidth, problem.width, problem.height, samples);
        const double incremental = GreedyOptimizer::scoreLine(samples, partial.transmittance, encoded, problem.target,
                                                              problem.weights, config.threadOpacity);
        const double brute = bruteForceDelta(problem, partial.transmittance, current, j, config);
        QVERIFY2(std::abs(incremental - brute) <= 1e-4 * std::max(1.0, std::abs(brute)),
                 qPrintable(QStringLiteral("pin %1: incremental %2 brute %3").arg(j).arg(incremental).arg(brute)));
    }
}

void TestOptimizer::matchesBruteForceReference()
{
    const OptimizationProblem problem = makeProblem(synthetic::portrait(64), 32, 16);
    OptimizationConfig config;
    config.minPinSeparation = 2;
    config.threadOpacity = 0.35f;
    config.maxSegments = 25;
    GreedyOptimizer optimizer;
    const StringArtResult result = optimizer.optimize(problem, config);
    const std::vector<int> reference = bruteForceGreedy(problem, config);
    QCOMPARE(result.sequence, reference);
}

void TestOptimizer::errorDecreasesMonotonically()
{
    const OptimizationProblem problem = makeProblem(synthetic::portrait(128), 96, 60);
    OptimizationConfig config;
    config.maxSegments = 300;
    config.minPinSeparation = 5;
    std::vector<double> errors;
    GreedyOptimizer optimizer;
    const StringArtResult result =
        optimizer.optimize(problem, config, {}, [&](const OptimizationProgress &p) { errors.push_back(p.error); });
    QVERIFY(errors.size() >= 2);
    for (std::size_t i = 1; i < errors.size(); ++i)
        QVERIFY2(errors[i] <= errors[i - 1] + 1e-7, qPrintable(QStringLiteral("step %1").arg(i)));
    QVERIFY(result.finalError < result.initialError);
    // The incrementally tracked error matches the exact recomputation.
    QVERIFY(std::abs(errors[errors.size() - 2] - result.finalError) < 1e-6);
    // Independent check from the returned transmittance.
    double sum = 0.0;
    double weight = 0.0;
    for (std::size_t i = 0; i < problem.target.size(); ++i) {
        const double d = SrgbEncoder::encode(result.transmittance[i]) - problem.target[i];
        sum += problem.weights[i] * d * d;
        weight += problem.weights[i];
    }
    QVERIFY(std::abs(sum / weight - result.finalError) < 1e-9);
}

void TestOptimizer::sequenceIsContinuousAndValid()
{
    const OptimizationProblem problem = makeProblem(synthetic::portrait(128), 96, 80);
    OptimizationConfig config;
    config.maxSegments = 500;
    config.minPinSeparation = 12;
    config.startPin = 7;
    GreedyOptimizer optimizer;
    const StringArtResult result = optimizer.optimize(problem, config);
    QCOMPARE(result.sequence.front(), 7);
    QVERIFY(result.segmentCount() > 0);
    std::set<std::pair<int, int>> used;
    double length = 0.0;
    for (std::size_t i = 0; i + 1 < result.sequence.size(); ++i) {
        const int a = result.sequence[i];
        const int b = result.sequence[i + 1];
        QVERIFY(a >= 0 && a < 80 && b >= 0 && b < 80);
        QVERIFY(problem.pins.separation(a, b) >= 12);
        QVERIFY2(used.insert({std::min(a, b), std::max(a, b)}).second, "repeated line");
        length += problem.pins.distance(a, b) * problem.pins.mmPerUnit();
    }
    QVERIFY(std::abs(length - result.threadLengthMm) < 1e-6 * length);
}

void TestOptimizer::repeatedLinesAllowed()
{
    const OptimizationProblem problem = makeProblem(synthetic::solid(64, 0), 48, 8);
    OptimizationConfig config;
    config.maxSegments = 60;
    config.minPinSeparation = 3;
    config.allowRepeatedLines = true;
    GreedyOptimizer optimizer;
    const StringArtResult result = optimizer.optimize(problem, config);
    // 8 pins with separation >= 3 have only 12 distinct lines.
    QCOMPARE(result.segmentCount(), 60);
    config.allowRepeatedLines = false;
    const StringArtResult unique = optimizer.optimize(problem, config);
    QVERIFY(unique.segmentCount() <= 12);
    QCOMPARE(unique.stopReason, StopReason::NoValidCandidates);
}

void TestOptimizer::deterministicAcrossThreadsAndCache()
{
    const OptimizationProblem problem = makeProblem(synthetic::portrait(256), 128, 120);
    OptimizationConfig config;
    config.maxSegments = 400;
    config.minPinSeparation = 10;
    GreedyOptimizer optimizer;

    config.threadCount = 1;
    const StringArtResult single = optimizer.optimize(problem, config);
    config.threadCount = 8;
    const StringArtResult parallel = optimizer.optimize(problem, config);
    config.lineCacheBudgetBytes = 0;
    const StringArtResult onDemand = optimizer.optimize(problem, config);
    const StringArtResult again = optimizer.optimize(problem, config);

    QVERIFY(single.lineCachePrecomputed);
    QVERIFY(!onDemand.lineCachePrecomputed);
    QCOMPARE(parallel.sequence, single.sequence);
    QCOMPARE(onDemand.sequence, single.sequence);
    QCOMPARE(again.sequence, single.sequence);
    QCOMPARE(again.transmittance, single.transmittance);
}

void TestOptimizer::transmittanceMatchesRenderer()
{
    const OptimizationProblem problem = makeProblem(synthetic::portrait(256), 128, 100);
    OptimizationConfig config;
    config.maxSegments = 300;
    config.threadWidth = 1.7;
    config.threadOpacity = 0.25f;
    GreedyOptimizer optimizer;
    const StringArtResult result = optimizer.optimize(problem, config);
    const std::vector<float> rendered =
        ThreadRenderer::renderTransmittance(problem.pins, result.sequence, config.threadWidth, config.threadOpacity);
    QCOMPARE(rendered, result.transmittance);
}

void TestOptimizer::cancellation()
{
    const OptimizationProblem problem = makeProblem(synthetic::portrait(256), 128, 100);
    OptimizationConfig config;
    config.maxSegments = 5000;
    std::stop_source source;
    GreedyOptimizer optimizer;
    const StringArtResult result = optimizer.optimize(problem, config, source.get_token(),
                                                      [&](const OptimizationProgress &p) {
                                                          if (p.segments == 10)
                                                              source.request_stop();
                                                      });
    QCOMPARE(result.stopReason, StopReason::Cancelled);
    QCOMPARE(result.segmentCount(), 10);
    QCOMPARE(result.sequence.size(), std::size_t{11});
}

void TestOptimizer::maxThreadLength()
{
    const OptimizationProblem problem = makeProblem(synthetic::portrait(128), 96, 60);
    OptimizationConfig config;
    config.maxSegments = 1000;
    config.maxThreadLengthMm = 5000.0;
    GreedyOptimizer optimizer;
    const StringArtResult result = optimizer.optimize(problem, config);
    QCOMPARE(result.stopReason, StopReason::MaxThreadLength);
    QVERIFY(result.threadLengthMm <= 5000.0);
    QVERIFY(result.threadLengthMm > 5000.0 - 500.0); // one segment is at most the 500 mm diameter
}

void TestOptimizer::blankImageProducesNoThreads()
{
    const OptimizationProblem problem = makeProblem(synthetic::solid(64, 255), 64, 50);
    GreedyOptimizer optimizer;
    const StringArtResult result = optimizer.optimize(problem, OptimizationConfig{});
    QCOMPARE(result.segmentCount(), 0);
    QCOMPARE(result.stopReason, StopReason::NoImprovement);
    QVERIFY(result.finalError < 1e-9);
}

void TestOptimizer::blackImageUsesAllSegments()
{
    const OptimizationProblem problem = makeProblem(synthetic::solid(64, 0), 64, 50);
    OptimizationConfig config;
    config.maxSegments = 200;
    config.minPinSeparation = 5;
    GreedyOptimizer optimizer;
    const StringArtResult result = optimizer.optimize(problem, config);
    QCOMPARE(result.segmentCount(), 200);
    QCOMPARE(result.stopReason, StopReason::MaxSegments);
}

void TestOptimizer::zeroOpacityStopsImmediately()
{
    const OptimizationProblem problem = makeProblem(synthetic::portrait(64), 64, 50);
    OptimizationConfig config;
    config.threadOpacity = 0.0f;
    GreedyOptimizer optimizer;
    const StringArtResult result = optimizer.optimize(problem, config);
    QCOMPARE(result.segmentCount(), 0);
    QCOMPARE(result.stopReason, StopReason::NoImprovement);
    QCOMPARE(result.finalError, result.initialError);
}

void TestOptimizer::insufficientConnections()
{
    const OptimizationProblem problem = makeProblem(synthetic::solid(64, 0), 32, 5);
    OptimizationConfig config;
    config.minPinSeparation = 3; // max separation with 5 pins is 2
    GreedyOptimizer optimizer;
    const StringArtResult result = optimizer.optimize(problem, config);
    QCOMPARE(result.segmentCount(), 0);
    QCOMPARE(result.stopReason, StopReason::NoValidCandidates);
}

void TestOptimizer::invalidConfiguration()
{
    const OptimizationProblem problem = makeProblem(synthetic::portrait(64), 32, 20);
    GreedyOptimizer optimizer;
    auto run = [&](auto mutate) {
        OptimizationConfig config;
        mutate(config);
        return optimizer.optimize(problem, config);
    };
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, run([](auto &c) { c.threadOpacity = 1.5f; }));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, run([](auto &c) { c.threadOpacity = -0.1f; }));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, run([](auto &c) { c.threadWidth = 0.0; }));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, run([](auto &c) { c.startPin = 20; }));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, run([](auto &c) { c.minPinSeparation = 0; }));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, run([](auto &c) { c.maxSegments = -1; }));

    OptimizationProblem mismatched = problem;
    mismatched.weights.pop_back();
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                             static_cast<void>(optimizer.optimize(mismatched, OptimizationConfig{})));
}

void TestOptimizer::reconstructsSyntheticDisk()
{
    const OptimizationProblem problem = makeProblem(synthetic::disk(256), 128, 120);
    OptimizationConfig config;
    config.maxSegments = 1500;
    config.minPinSeparation = 10;
    GreedyOptimizer optimizer;
    const StringArtResult result = optimizer.optimize(problem, config);
    // Every chord through the disk also crosses white canvas, so a perfect
    // reconstruction is impossible; require a substantial error reduction.
    QVERIFY2(result.finalError < 0.6 * result.initialError,
             qPrintable(QStringLiteral("%1 -> %2").arg(result.initialError).arg(result.finalError)));

    // The disk (radius 0.2 of the image) must render darker than the ring
    // between radius 0.3 and 0.45.
    double inside = 0.0;
    double outside = 0.0;
    int nInside = 0;
    int nOutside = 0;
    for (int y = 0; y < 128; ++y) {
        for (int x = 0; x < 128; ++x) {
            const double r = std::hypot(x + 0.5 - 64.0, y + 0.5 - 64.0) / 128.0;
            const float t = result.transmittance[static_cast<std::size_t>(y) * 128 + x];
            if (r < 0.15) {
                inside += t;
                ++nInside;
            } else if (r > 0.3 && r < 0.45) {
                outside += t;
                ++nOutside;
            }
        }
    }
    QVERIFY2(inside / nInside < 0.5 * (outside / nOutside),
             qPrintable(QStringLiteral("inside %1 outside %2").arg(inside / nInside).arg(outside / nOutside)));
}

QTEST_GUILESS_MAIN(TestOptimizer)
#include "tst_optimizer.moc"
