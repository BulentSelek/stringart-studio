#include "ImageStore.h"
#include "ProjectController.h"

#include "SyntheticImages.h"

#include <QElapsedTimer>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QtQml/QQmlExtensionPlugin>

Q_IMPORT_QML_PLUGIN(StringArtStudioPlugin)

// End-to-end check of the Phase 1 acceptance flow through the real QML UI:
// load a portrait, choose a pin count, optimize without blocking the GUI
// thread, inspect, export PNG and CSV.
class TestUi : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();
    void loadsWithoutWarnings();
    void fullWorkflow();
    void pauseResumeCancel();

private:
    QTemporaryDir m_dir;
    std::shared_ptr<ImageStore> m_store;
    std::unique_ptr<ProjectController> m_controller;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QQuickWindow *m_window = nullptr;
    QUrl m_portrait;
};

void TestUi::initTestCase()
{
    QQuickStyle::setStyle(QStringLiteral("Material"));
    QVERIFY(m_dir.isValid());
    const QString path = m_dir.filePath(QStringLiteral("portrait.png"));
    QVERIFY(sa::synthetic::portrait(800).save(path));
    m_portrait = QUrl::fromLocalFile(path);
}

void TestUi::init()
{
    QTest::failOnWarning(QRegularExpression(QStringLiteral(".*")));
    m_store = std::make_shared<ImageStore>();
    m_controller = std::make_unique<ProjectController>(m_store);
    m_engine = std::make_unique<QQmlApplicationEngine>();
    m_engine->addImageProvider(QStringLiteral("studio"), new StudioImageProvider(m_store));
    m_engine->setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(m_controller.get())}});
    m_engine->loadFromModule("StringArtStudio", "Main");
    QCOMPARE(m_engine->rootObjects().size(), 1);
    m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().first());
    QVERIFY(m_window);
    QVERIFY(QTest::qWaitForWindowExposed(m_window));
}

void TestUi::cleanup()
{
    m_engine.reset();
    m_controller.reset();
    m_window = nullptr;
}

void TestUi::loadsWithoutWarnings()
{
    QVERIFY(!m_controller->hasImage());
    QVERIFY(!m_controller->statusText().isEmpty());
}

void TestUi::fullWorkflow()
{
    QVERIFY(m_controller->loadImage(m_portrait));
    QVERIFY(m_controller->hasImage());
    m_controller->setProperty("pinCount", 200);
    m_controller->setProperty("maxSegments", 2500);
    QCOMPARE(m_controller->pinPositions().size(), 200);

    // Measure the largest gap between GUI-thread timer ticks during the run.
    QElapsedTimer clock;
    qint64 last = 0;
    qint64 maxGap = 0;
    QTimer ticker;
    ticker.setInterval(5);
    connect(&ticker, &QTimer::timeout, this, [&] {
        const qint64 now = clock.elapsed();
        maxGap = std::max(maxGap, now - last);
        last = now;
    });
    QSignalSpy resultFrames(m_controller.get(), &ProjectController::resultRevisionChanged);

    clock.start();
    ticker.start();
    m_controller->start();
    QVERIFY(m_controller->running());
    QTRY_VERIFY_WITH_TIMEOUT(!m_controller->running(), 120000);
    ticker.stop();

    QVERIFY2(m_controller->hasResult(), qPrintable(m_controller->statusText()));
    QVERIFY(m_controller->segments() > 500);
    QVERIFY(m_controller->error() < 0.5 * m_controller->initialError());
    QVERIFY(m_controller->threadLengthM() > 0.0);
    QVERIFY2(maxGap < 250, qPrintable(QStringLiteral("GUI thread blocked for %1 ms").arg(maxGap)));
    QVERIFY(resultFrames.count() >= 1);
    qInfo("run: %d segments, %.2f s, max GUI gap %lld ms, %lld preview frames", m_controller->segments(),
          m_controller->elapsedSeconds(), maxGap, static_cast<long long>(resultFrames.count()));

    const QString png = m_dir.filePath(QStringLiteral("art.png"));
    m_controller->setProperty("exportSize", 1024);
    m_controller->exportPng(QUrl::fromLocalFile(png));
    QVERIFY(m_controller->exporting());
    QTRY_VERIFY_WITH_TIMEOUT(!m_controller->exporting(), 30000);
    const QImage exported(png);
    QCOMPARE(exported.size(), QSize(1024, 1024));

    const QString csv = m_dir.filePath(QStringLiteral("sequence.csv"));
    m_controller->exportCsv(QUrl::fromLocalFile(csv));
    QFile file(csv);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QList<QByteArray> lines = file.readAll().trimmed().split('\n');
    QCOMPARE(lines.size(), m_controller->segments() + 1);
    QVERIFY(lines.first().startsWith("segment,from_pin,to_pin"));

    // Let the result view repaint, then optionally save a screenshot.
    QTest::qWait(300);
    const QByteArray shot = qgetenv("STRINGART_UI_SCREENSHOT");
    if (!shot.isEmpty())
        QVERIFY(m_window->grabWindow().save(QString::fromLocal8Bit(shot)));
}

void TestUi::pauseResumeCancel()
{
    QVERIFY(m_controller->loadImage(m_portrait));
    m_controller->setProperty("maxSegments", 50000);
    m_controller->setProperty("allowRepeats", true);
    m_controller->setProperty("workingSize", 1024);
    m_controller->start();
    QTRY_VERIFY_WITH_TIMEOUT(m_controller->segments() > 20, 30000);

    m_controller->pause();
    QVERIFY(m_controller->paused());
    QTest::qWait(300); // let the worker reach the pause point and publish
    const int pausedAt = m_controller->segments();
    QTest::qWait(400);
    QCOMPARE(m_controller->segments(), pausedAt);

    m_controller->resume();
    QTRY_VERIFY_WITH_TIMEOUT(m_controller->segments() > pausedAt, 30000);

    m_controller->pause();
    m_controller->cancel(); // cancelling while paused must not deadlock
    QTRY_VERIFY_WITH_TIMEOUT(!m_controller->running(), 30000);
    QVERIFY(m_controller->statusText().contains(QStringLiteral("cancelled")));
    QVERIFY(m_controller->hasResult()); // partial result remains available
}

QTEST_MAIN(TestUi)
#include "tst_ui.moc"
