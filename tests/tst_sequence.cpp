#include "algorithms/GreedyOptimizer.h"
#include "algorithms/SequenceOptimizer.h"
#include "core/ImageProcessor.h"
#include "core/ThreadRenderer.h"

#include "SyntheticImages.h"

#include <QTest>

#include <set>
#include <stdexcept>
#include <stop_token>

using namespace sa;

namespace {

OptimizationProblem makeProblem(int size, int pins)
{
    const GrayImage gray = ImageProcessor::prepareBase(synthetic::portrait(256), size, size);
    return OptimizationProblem::fromImage(gray, PinLayout::circular(pins, size, size));
}

double exactError(const OptimizationProblem &problem, const std::vector<int> &sequence,
                  const OptimizationConfig &config)
{
    const std::vector<float> t =
        ThreadRenderer::renderTransmittance(problem.pins, sequence, config.threadWidth, config.threadOpacity);
    double weight = 0.0;
    for (float w : problem.weights)
        weight += w;
    return GreedyOptimizer::weightedSse(t, problem.target, problem.weights) / weight;
}

// A valid but poor sequence: pins chosen by a fixed stride pattern.
std::vector<int> naiveSequence(const PinLayout &pins, int segments, int minSeparation)
{
    std::vector<int> sequence{0};
    std::set<std::pair<int, int>> used;
    int current = 0;
    for (int s = 0; s < segments; ++s) {
        for (int step = minSeparation; step < pins.pinCount(); ++step) {
            const int next = (current + step * 7) % pins.pinCount();
            const auto key = std::minmax(current, next);
            if (pins.separation(current, next) >= minSeparation && !used.contains(key)) {
                used.insert(key);
                sequence.push_back(next);
                current = next;
                break;
            }
        }
    }
    return sequence;
}

void verifySequence(const OptimizationProblem &problem, const std::vector<int> &sequence,
                    const OptimizationConfig &config, int startPin)
{
    QCOMPARE(sequence.front(), startPin);
    std::set<std::pair<int, int>> used;
    for (std::size_t i = 0; i + 1 < sequence.size(); ++i) {
        const int a = sequence[i];
        const int b = sequence[i + 1];
        QVERIFY(problem.pins.canConnect(a, b));
        QVERIFY(problem.pins.separation(a, b) >= config.minPinSeparation);
        if (!config.allowRepeatedLines)
            QVERIFY2(used.insert(std::minmax(a, b)).second, "repeated line");
    }
}

} // namespace

class TestSequence : public QObject
{
    Q_OBJECT

private slots:
    void improvesNaiveSequence();
    void refinesGreedyResult();
    void deleteAndInsertMoves();
    void convergedIsLocallyOptimal();
    void deterministicAcrossThreads();
    void respectsMaxThreadLength();
    void cancellation();
    void invalidInput();
};

void TestSequence::improvesNaiveSequence()
{
    const OptimizationProblem problem = makeProblem(96, 72);
    OptimizationConfig config;
    config.minPinSeparation = 6;
    const std::vector<int> naive = naiveSequence(problem.pins, 300, config.minPinSeparation);
    SequenceOptimizer optimizer(naive, {5, 8, false});
    std::vector<double> errors;
    const StringArtResult result =
        optimizer.optimize(problem, config, {}, [&](const OptimizationProgress &p) { errors.push_back(p.error); });

    QCOMPARE(result.sequence.size(), naive.size());
    verifySequence(problem, result.sequence, config, 0);
    QVERIFY(optimizer.improvements() > 0);
    QVERIFY2(result.finalError < 0.9 * result.initialError,
             qPrintable(QStringLiteral("%1 -> %2").arg(result.initialError).arg(result.finalError)));
    // Reported error, buffer and length all match an independent recomputation.
    QVERIFY(std::abs(result.finalError - exactError(problem, result.sequence, config)) < 1e-9);
    QCOMPARE(result.transmittance, ThreadRenderer::renderTransmittance(problem.pins, result.sequence,
                                                                       config.threadWidth, config.threadOpacity));
    double length = 0.0;
    for (std::size_t i = 1; i < result.sequence.size(); ++i)
        length += problem.pins.distance(result.sequence[i - 1], result.sequence[i]) * problem.pins.mmPerUnit();
    QVERIFY(std::abs(length - result.threadLengthMm) < 1e-6 * length);
    for (std::size_t i = 1; i < errors.size(); ++i)
        QVERIFY(errors[i] <= errors[i - 1] + 1e-7);
}

void TestSequence::refinesGreedyResult()
{
    const OptimizationProblem problem = makeProblem(128, 120);
    OptimizationConfig config;
    config.minPinSeparation = 10;
    config.maxSegments = 800;
    config.startPin = 5;
    GreedyOptimizer greedy;
    const StringArtResult first = greedy.optimize(problem, config);
    SequenceOptimizer refine(first.sequence, {3, 15});
    const StringArtResult refined = refine.optimize(problem, config);
    verifySequence(problem, refined.sequence, config, 5);
    QVERIFY(refined.finalError <= first.finalError + 1e-12);
    QVERIFY(std::abs(refined.initialError - first.finalError) < 1e-9);
    qInfo("greedy %.6f -> refined %.6f (%d improvements, %d passes)", first.finalError, refined.finalError,
          refine.improvements(), refine.passes());
}

void TestSequence::deleteAndInsertMoves()
{
    const OptimizationProblem problem = makeProblem(96, 72);
    OptimizationConfig config;
    config.minPinSeparation = 6;
    config.maxSegments = 260; // room for 60 insertions
    const std::vector<int> naive = naiveSequence(problem.pins, 200, config.minPinSeparation);
    SequenceOptimizer fixedLength(naive, {4, 8, false});
    SequenceOptimizer resizing(naive, {4, 8, true});
    const StringArtResult fixed = fixedLength.optimize(problem, config);
    const StringArtResult resized = resizing.optimize(problem, config);
    verifySequence(problem, resized.sequence, config, 0);
    QVERIFY(resized.sequence.size() != naive.size());
    QVERIFY(static_cast<int>(resized.sequence.size()) - 1 <= config.maxSegments);
    QVERIFY(resized.finalError < fixed.finalError);
    QVERIFY(std::abs(resized.finalError - exactError(problem, resized.sequence, config)) < 1e-9);
    QCOMPARE(resized.transmittance, ThreadRenderer::renderTransmittance(problem.pins, resized.sequence,
                                                                        config.threadWidth, config.threadOpacity));
}

void TestSequence::convergedIsLocallyOptimal()
{
    const OptimizationProblem problem = makeProblem(64, 40);
    OptimizationConfig config;
    config.minPinSeparation = 4;
    config.threadOpacity = 0.3f;
    const std::vector<int> naive = naiveSequence(problem.pins, 80, config.minPinSeparation);
    SequenceOptimizer optimizer(naive, {50, 0, false}); // all pins, until converged
    const StringArtResult result = optimizer.optimize(problem, config);
    QCOMPARE(result.stopReason, StopReason::NoImprovement);

    // Brute force: no single relocation improves the fully recomputed error.
    const double base = exactError(problem, result.sequence, config);
    std::set<std::pair<int, int>> used;
    for (std::size_t i = 1; i < result.sequence.size(); ++i)
        used.insert(std::minmax(result.sequence[i - 1], result.sequence[i]));
    for (std::size_t i = 1; i < result.sequence.size(); i += 7) {
        const int a = result.sequence[i - 1];
        const int b = result.sequence[i];
        const int c = i + 1 < result.sequence.size() ? result.sequence[i + 1] : -1;
        for (int p = 0; p < problem.pins.pinCount(); ++p) {
            if (p == b || problem.pins.separation(a, p) < 4 || (c >= 0 && problem.pins.separation(p, c) < 4))
                continue;
            if (used.contains(std::minmax(a, p)) || (c >= 0 && (used.contains(std::minmax(p, c)) || a == c)))
                continue;
            std::vector<int> moved = result.sequence;
            moved[i] = p;
            QVERIFY2(exactError(problem, moved, config) >= base - 1e-9,
                     qPrintable(QStringLiteral("position %1 pin %2").arg(i).arg(p)));
        }
    }
}

void TestSequence::deterministicAcrossThreads()
{
    const OptimizationProblem problem = makeProblem(96, 90);
    OptimizationConfig config;
    config.minPinSeparation = 8;
    const std::vector<int> naive = naiveSequence(problem.pins, 250, config.minPinSeparation);
    config.threadCount = 1;
    const StringArtResult single = SequenceOptimizer(naive, {2, 10}).optimize(problem, config);
    config.threadCount = 8;
    const StringArtResult parallel = SequenceOptimizer(naive, {2, 10}).optimize(problem, config);
    QCOMPARE(parallel.sequence, single.sequence);
}

void TestSequence::respectsMaxThreadLength()
{
    const OptimizationProblem problem = makeProblem(96, 72);
    OptimizationConfig config;
    config.minPinSeparation = 6;
    const std::vector<int> naive = naiveSequence(problem.pins, 200, config.minPinSeparation);
    double length = 0.0;
    for (std::size_t i = 1; i < naive.size(); ++i)
        length += problem.pins.distance(naive[i - 1], naive[i]) * problem.pins.mmPerUnit();
    config.maxThreadLengthMm = length; // may not grow
    const StringArtResult result = SequenceOptimizer(naive, {3, 10}).optimize(problem, config);
    QVERIFY(result.threadLengthMm <= length + 1e-6);
}

void TestSequence::cancellation()
{
    const OptimizationProblem problem = makeProblem(96, 72);
    OptimizationConfig config;
    config.minPinSeparation = 6;
    const std::vector<int> naive = naiveSequence(problem.pins, 300, config.minPinSeparation);
    std::stop_source source;
    source.request_stop();
    const StringArtResult result = SequenceOptimizer(naive).optimize(problem, config, source.get_token());
    QCOMPARE(result.stopReason, StopReason::Cancelled);
    QCOMPARE(result.sequence, naive);
}

void TestSequence::invalidInput()
{
    const OptimizationProblem problem = makeProblem(64, 40);
    OptimizationConfig config;
    config.minPinSeparation = 4;
    auto run = [&](std::vector<int> sequence, OptimizationConfig c) {
        static_cast<void>(SequenceOptimizer(std::move(sequence)).optimize(problem, c));
    };
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, run({}, config));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, run({0, 1}, config));  // separation 1 < 4
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, run({0, 99}, config)); // pin out of range
    OptimizationConfig opaque = config;
    opaque.threadOpacity = 1.0f;
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, run({0, 20}, opaque));
    // An opaque thread thinner than 1/sqrt(2) px never covers a whole pixel,
    // so removing it (division by 1 - a c) stays defined.
    opaque.threadWidth = 0.6;
    const StringArtResult thin = SequenceOptimizer({0, 20, 5}).optimize(problem, opaque);
    QVERIFY(thin.finalError <= thin.initialError);
    // A single pin has nothing to refine.
    const StringArtResult trivial = SequenceOptimizer({3}).optimize(problem, config);
    QCOMPARE(trivial.sequence, std::vector<int>{3});
    QCOMPARE(trivial.stopReason, StopReason::NoImprovement);
}

QTEST_GUILESS_MAIN(TestSequence)
#include "tst_sequence.moc"
