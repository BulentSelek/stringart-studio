#include "core/PlaybackRenderer.h"
#include "core/ThreadRenderer.h"

#include <QTest>

using namespace sa;

class TestPlayback : public QObject
{
    Q_OBJECT

private slots:
    void seekMatchesRenderer();
    void smallBudgetUsesSparseCheckpoints();
    void emptySequence();
    void lazyCheckpointsFromFinalState();
};

namespace {

std::vector<int> pseudoRandomSequence(int pins, int segments)
{
    std::vector<int> sequence{0};
    unsigned state = 12345;
    for (int i = 0; i < segments; ++i) {
        state = state * 1103515245u + 12345u;
        int next = static_cast<int>((state >> 8) % static_cast<unsigned>(pins));
        if (next == sequence.back())
            next = (next + 1) % pins;
        sequence.push_back(next);
    }
    return sequence;
}

} // namespace

void TestPlayback::seekMatchesRenderer()
{
    const PinLayout layout = PinLayout::circular(80, 96, 96);
    const std::vector<int> sequence = pseudoRandomSequence(80, 300);
    PlaybackRenderer playback(layout, sequence, 1.3, 0.25f);
    QCOMPARE(playback.segmentCount(), 300);
    QCOMPARE(playback.position(), 300);
    // Forward steps, backward jumps, jumps across checkpoints and clamping.
    for (int target : {300, 0, 1, 2, 17, 16, 15, 150, 151, 299, 40, 400, -5}) {
        const auto t = playback.seek(target);
        const int expected = std::clamp(target, 0, 300);
        QCOMPARE(playback.position(), expected);
        const std::vector<float> reference =
            ThreadRenderer::renderTransmittance(layout, sequence, 1.3, 0.25f, 1.0, static_cast<std::size_t>(expected));
        QVERIFY2(std::equal(t.begin(), t.end(), reference.begin(), reference.end()),
                 qPrintable(QStringLiteral("segment %1").arg(target)));
    }
}

void TestPlayback::smallBudgetUsesSparseCheckpoints()
{
    const PinLayout layout = PinLayout::circular(60, 64, 64);
    const std::vector<int> sequence = pseudoRandomSequence(60, 500);
    PlaybackRenderer playback(layout, sequence, 1.0, 0.2f, {}, 64 * 64 * sizeof(float) * 4); // 4 checkpoints
    QVERIFY(playback.checkpointInterval() >= 125);
    const auto t = playback.seek(333);
    const std::vector<float> reference = ThreadRenderer::renderTransmittance(layout, sequence, 1.0, 0.2f, 1.0, 333);
    QVERIFY(std::equal(t.begin(), t.end(), reference.begin(), reference.end()));
}

void TestPlayback::emptySequence()
{
    PlaybackRenderer playback(PinLayout::circular(10, 32, 32), {3}, 1.0, 0.2f);
    QCOMPARE(playback.segmentCount(), 0);
    const auto t = playback.seek(5);
    QCOMPARE(t.size(), std::size_t{32 * 32});
    QVERIFY(std::all_of(t.begin(), t.end(), [](float v) { return v == 1.0f; }));
}

void TestPlayback::lazyCheckpointsFromFinalState()
{
    const PinLayout layout = PinLayout::circular(70, 80, 80);
    const std::vector<int> sequence = pseudoRandomSequence(70, 250);
    std::vector<float> final = ThreadRenderer::renderTransmittance(layout, sequence, 1.0, 0.25f);
    PlaybackRenderer playback(layout, sequence, 1.0, 0.25f, final);
    QCOMPARE(playback.position(), 250);
    const auto end = playback.seek(250);
    QVERIFY(std::equal(end.begin(), end.end(), final.begin(), final.end()));
    for (int target : {100, 0, 249, 250}) {
        const auto t = playback.seek(target);
        const std::vector<float> reference =
            ThreadRenderer::renderTransmittance(layout, sequence, 1.0, 0.25f, 1.0, static_cast<std::size_t>(target));
        QVERIFY(std::equal(t.begin(), t.end(), reference.begin(), reference.end()));
    }
}

QTEST_GUILESS_MAIN(TestPlayback)
#include "tst_playback.moc"
