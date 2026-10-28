#include "ImageStore.h"
#include "ProjectController.h"
#include "core/ProjectFile.h"

#include "SyntheticImages.h"

#include <QElapsedTimer>
#include <QFile>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QtQml/QQmlExtensionPlugin>

#include <cmath>

Q_IMPORT_QML_PLUGIN(StringArtStudioPlugin)

// Responsiveness limit for the GUI thread during a run. Sanitizer and debug
// builds run QML and rendering several times slower, so they get more room.
#if defined(__SANITIZE_ADDRESS__) || !defined(NDEBUG)
constexpr qint64 kMaxGuiGapMs = 2000;
#else
constexpr qint64 kMaxGuiGapMs = 250;
#endif

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
    void phase2Workflow();
    void frameOutlineFollowsShape();
    void dialogFoldersPersist();
    void windowStatePersists();
    void parametersPersist();
    void playbackWorkflow();
    void colourWorkflow();
    void workshopWorkflow();
    void paletteSuggestion();
    void threadDiameterInMillimetres();
    void effortLevels();
    void cropHandles();
    void undoRedo();
    void presets();
    void runHistory();

private:
    void createUi();
    void destroyUi();

    // Visual children are not necessarily QObject children; walk the item tree.
    QQuickItem *findItem(const QString &objectName) const
    {
        QList<QQuickItem *> pending{m_window->contentItem()};
        while (!pending.isEmpty()) {
            QQuickItem *item = pending.takeFirst();
            if (item->objectName() == objectName)
                return item;
            pending.append(item->childItems());
        }
        return nullptr;
    }

    QTemporaryDir m_dir;
    std::shared_ptr<ImageStore> m_store;
    std::unique_ptr<ProjectController> m_controller;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QQuickWindow *m_window = nullptr;
    QUrl m_portrait;
};

void TestUi::initTestCase()
{
    qputenv("QT_QUICK_CONTROLS_MATERIAL_VARIANT", "Dense");
    // Settings go to a temporary location, never the user's configuration.
    QStandardPaths::setTestModeEnabled(true);
    QVERIFY(m_dir.isValid());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, m_dir.filePath(QStringLiteral("config")));
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir.filePath(QStringLiteral("config")));
    QCoreApplication::setOrganizationName(QStringLiteral("StringArtStudioTest"));
    QCoreApplication::setApplicationName(QStringLiteral("tst_ui"));
    QQuickStyle::setStyle(QStringLiteral("Material"));
    QVERIFY(m_dir.isValid());
    const QString path = m_dir.filePath(QStringLiteral("portrait.png"));
    QVERIFY(sa::synthetic::portrait(800).save(path));
    m_portrait = QUrl::fromLocalFile(path);
}

void TestUi::init()
{
    QTest::failOnWarning(QRegularExpression(QStringLiteral(".*")));
    QSettings().clear(); // every test starts from default settings
    createUi();
}

void TestUi::createUi()
{
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
    destroyUi();
}

void TestUi::destroyUi()
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
    QVERIFY2(maxGap < kMaxGuiGapMs, qPrintable(QStringLiteral("GUI thread blocked for %1 ms").arg(maxGap)));
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

void TestUi::phase2Workflow()
{
    QVERIFY(m_controller->loadImage(m_portrait));
    m_controller->setProperty("frameShape", ProjectController::Rectangle);
    m_controller->setProperty("frameWidthMm", 600.0);
    m_controller->setProperty("frameHeightMm", 400.0);
    m_controller->setProperty("pinCount", 200);
    m_controller->setProperty("maxSegments", 1500);
    m_controller->setProperty("edgeEmphasis", 1.5);
    m_controller->rotateBy(90);
    m_controller->setProperty("crop", QRectF(0.1, 0.1, 0.8, 0.8));
    QCOMPARE(m_controller->canvasWidth(), 512);
    QCOMPARE(m_controller->canvasHeight(), 341);
    // The processed image follows the frame aspect once the debounce fires.
    QTRY_COMPARE(m_store->get(QStringLiteral("processed")).size(), QSize(512, 341));
    const QRectF crop = m_controller->effectiveCrop();
    QVERIFY(crop.width() > 0 && crop.left() >= 0.1 - 1e-9 && crop.right() <= 0.9 + 1e-9);

    m_controller->start();
    QTRY_VERIFY_WITH_TIMEOUT(!m_controller->running(), 60000);
    QVERIFY2(m_controller->hasResult(), qPrintable(m_controller->statusText()));
    QCOMPARE(m_store->get(QStringLiteral("residual")).size(), QSize(512, 341));
    const int segments = m_controller->segments();
    const double error = m_controller->error();

    const QString svg = m_dir.filePath(QStringLiteral("art.svg"));
    const QString json = m_dir.filePath(QStringLiteral("art.json"));
    const QString project = m_dir.filePath(QStringLiteral("art.sasproj"));
    m_controller->exportSvg(QUrl::fromLocalFile(svg));
    m_controller->exportJson(QUrl::fromLocalFile(json));
    QVERIFY(m_controller->saveProject(QUrl::fromLocalFile(project)));
    QFile svgFile(svg);
    QVERIFY(svgFile.open(QIODevice::ReadOnly));
    QCOMPARE(svgFile.readAll().count("<line "), segments);
    QVERIFY(QFile::exists(json));

    // Change everything, then reopen the project.
    m_controller->setProperty("frameShape", ProjectController::Circle);
    m_controller->setProperty("pinCount", 50);
    m_controller->resetCrop();
    QVERIFY(m_controller->loadImage(QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("portrait.png")))));
    QVERIFY(!m_controller->hasResult());

    QVERIFY2(m_controller->openProject(QUrl::fromLocalFile(project)), qPrintable(m_controller->statusText()));
    QVERIFY(m_controller->hasImage());
    QVERIFY(m_controller->hasResult());
    QCOMPARE(m_controller->property("frameShape").toInt(), int(ProjectController::Rectangle));
    QCOMPARE(m_controller->property("pinCount").toInt(), 200);
    QCOMPARE(m_controller->property("rotation").toDouble(), 90.0);
    QCOMPARE(m_controller->property("edgeEmphasis").toDouble(), 1.5);
    QCOMPARE(m_controller->segments(), segments);
    QCOMPARE(m_controller->error(), error);
    QCOMPARE(m_store->get(QStringLiteral("result")).size(), QSize(512, 341));
    QCOMPARE(m_store->get(QStringLiteral("residual")).size(), QSize(512, 341));

    // Re-running the restored settings reproduces the same result.
    m_controller->start();
    QTRY_VERIFY_WITH_TIMEOUT(!m_controller->running(), 60000);
    QCOMPARE(m_controller->segments(), segments);
    QVERIFY(std::abs(m_controller->error() - error) < 1e-12);

    QTest::qWait(300);
    const QByteArray shot = qgetenv("STRINGART_UI_SCREENSHOT2");
    if (!shot.isEmpty())
        QVERIFY(m_window->grabWindow().save(QString::fromLocal8Bit(shot)));
}

void TestUi::frameOutlineFollowsShape()
{
    QVERIFY(m_controller->loadImage(m_portrait));
    m_controller->setProperty("frameShape", ProjectController::Ellipse);
    m_controller->setProperty("frameWidthMm", 400.0);
    m_controller->setProperty("frameHeightMm", 600.0);
    QCOMPARE(m_controller->canvasWidth(), 341);
    QCOMPARE(m_controller->canvasHeight(), 512);
    QTRY_COMPARE(m_store->get(QStringLiteral("processed")).size(), QSize(341, 512));
    // The crop follows the new frame aspect (portrait-oriented on a square photo).
    const QRectF crop = m_controller->effectiveCrop();
    QVERIFY(std::abs(crop.width() / crop.height() - 341.0 / 512.0) < 0.01);

    QQuickItem *tabBar = findItem(QStringLiteral("viewTabs"));
    QVERIFY(tabBar);
    // Fit the whole photo inside the ellipse: the crop extends beyond the image.
    m_controller->fitWholeImage();
    const QRectF whole = m_controller->effectiveCrop();
    QVERIFY(whole.left() < 0.0 && whole.top() < 0.0 && whole.right() > 1.0 && whole.bottom() > 1.0);
    QTRY_VERIFY(m_store->get(QStringLiteral("processed")).pixel(5, 5) == qRgb(255, 255, 255));

    const QByteArray prefix = qgetenv("STRINGART_UI_SHAPES");
    for (int tab : {0, 1}) {
        tabBar->setProperty("currentIndex", tab);
        QTest::qWait(300);
        if (!prefix.isEmpty())
            QVERIFY(m_window->grabWindow().save(QString::fromLocal8Bit(prefix) + QString::number(tab) + u".png"));
    }

    // Selecting a parameter group switches the view: Image -> Original, Frame -> Processed.
    QQuickItem *categories = findItem(QStringLiteral("categoryTabs"));
    QVERIFY(categories);
    categories->setProperty("currentIndex", 1);
    QCOMPARE(tabBar->property("currentIndex").toInt(), 1);
    categories->setProperty("currentIndex", 0);
    QCOMPARE(tabBar->property("currentIndex").toInt(), 0);
    for (int category : {0, 1, 2, 3}) {
        categories->setProperty("currentIndex", category);
        QTest::qWait(200);
        if (!prefix.isEmpty())
            QVERIFY(m_window->grabWindow().save(QString::fromLocal8Bit(prefix) + u"cat" + QString::number(category)
                                                + u".png"));
    }
}

void TestUi::dialogFoldersPersist()
{
    QObject *folders = m_window->findChild<QObject *>(QStringLiteral("dialogFolders"));
    QVERIFY(folders);
    const QUrl folder = QUrl::fromLocalFile(m_dir.path());
    folders->setProperty("images", folder);

    // A new session (fresh engine and window) restores the folder.
    destroyUi();
    createUi();
    folders = m_window->findChild<QObject *>(QStringLiteral("dialogFolders"));
    QVERIFY(folders);
    QCOMPARE(folders->property("images").toUrl(), folder);
    QObject *dialog = m_window->findChild<QObject *>(QStringLiteral("openImageDialog"));
    QVERIFY(dialog);
    QCOMPARE(dialog->property("currentFolder").toUrl(), folder);
}

void TestUi::windowStatePersists()
{
    m_window->setWidth(1300);
    m_window->setHeight(780);
    findItem(QStringLiteral("categoryTabs"))->setProperty("currentIndex", 2);

    destroyUi();
    createUi();
    QCOMPARE(m_window->width(), 1300);
    QCOMPARE(m_window->height(), 780);
    QCOMPARE(findItem(QStringLiteral("categoryTabs"))->property("currentIndex").toInt(), 2);
}

void TestUi::parametersPersist()
{
    QVERIFY(m_controller->loadImage(m_portrait));
    m_controller->setProperty("frameShape", ProjectController::Rectangle);
    m_controller->setProperty("frameWidthMm", 700.0);
    m_controller->setProperty("frameHeightMm", 450.0);
    m_controller->setProperty("pinCount", 321);
    m_controller->setProperty("opacity", 0.33);
    m_controller->setProperty("contrast", 1.4);
    m_controller->setProperty("edgeEmphasis", 2.0);
    m_controller->setProperty("workingSize", 768);
    m_controller->setProperty("exportSize", 4096);
    m_controller->setProperty("threadColor", QColor(0x1a, 0x23, 0x40));
    m_controller->setProperty("rotation", 33.0);
    {
        QSettings settings;
        m_controller->saveSettings(settings);
    }

    ProjectController restored(m_store);
    {
        QSettings settings;
        restored.restoreSettings(settings);
    }
    QCOMPARE(restored.property("frameShape").toInt(), int(ProjectController::Rectangle));
    QCOMPARE(restored.property("frameWidthMm").toDouble(), 700.0);
    QCOMPARE(restored.property("frameHeightMm").toDouble(), 450.0);
    QCOMPARE(restored.property("pinCount").toInt(), 321);
    QVERIFY(std::abs(restored.property("opacity").toDouble() - 0.33) < 1e-6);
    QCOMPARE(restored.property("contrast").toDouble(), 1.4);
    QCOMPARE(restored.property("edgeEmphasis").toDouble(), 2.0);
    QCOMPARE(restored.property("workingSize").toInt(), 768);
    QCOMPARE(restored.property("exportSize").toInt(), 4096);
    QCOMPARE(restored.property("threadColor").value<QColor>(), QColor(0x1a, 0x23, 0x40));
    QCOMPARE(restored.property("rotation").toDouble(), 0.0); // image-specific, not stored
    QVERIFY(!restored.hasImage());

    // Corrupt settings are ignored.
    QSettings().setValue(QStringLiteral("Parameters/project"), QByteArray("{broken"));
    ProjectController fallback(m_store);
    QSettings settings;
    fallback.restoreSettings(settings);
    QCOMPARE(fallback.property("pinCount").toInt(), 240);
}

void TestUi::playbackWorkflow()
{
    QVERIFY(m_controller->loadImage(m_portrait));
    m_controller->setProperty("pinCount", 150);
    m_controller->setProperty("maxSegments", 600);
    m_controller->start();
    QTRY_VERIFY_WITH_TIMEOUT(!m_controller->running(), 60000);
    const int total = m_controller->resultSegments();
    QCOMPARE(total, m_controller->segments());
    QCOMPARE(m_controller->playbackPosition(), total);

    // Seeking shows exactly the first n segments of the computed sequence.
    m_controller->setPlaybackPosition(10);
    QCOMPARE(m_controller->playbackPosition(), 10);
    QVERIFY(m_controller->currentFromPin() >= 0);
    QCOMPARE(m_controller->currentSegmentLine().size(), 2);
    const QImage frame = m_store->get(QStringLiteral("result"));
    const QImage partial = m_store->get(QStringLiteral("result"));
    m_controller->setPlaybackPosition(total);
    QVERIFY(m_store->get(QStringLiteral("result")) != partial);
    m_controller->setPlaybackPosition(10);
    QCOMPARE(m_store->get(QStringLiteral("result")), frame);
    m_controller->setPlaybackPosition(-3);
    QCOMPARE(m_controller->playbackPosition(), 0);
    QCOMPARE(m_controller->currentFromPin(), -1);
    m_controller->stepPlayback(5);
    QCOMPARE(m_controller->playbackPosition(), 5);

    // Playback advances on its own and stops at the end.
    m_controller->setProperty("playbackSpeed", 1000.0);
    m_controller->play();
    QVERIFY(m_controller->playing());
    QTRY_VERIFY_WITH_TIMEOUT(m_controller->playbackPosition() > 50, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!m_controller->playing(), 10000);
    QCOMPARE(m_controller->playbackPosition(), total);

    // Frame export renders the selected prefix of the sequence.
    m_controller->setPlaybackPosition(total / 2);
    m_controller->setProperty("exportSize", 512);
    const QString path = m_dir.filePath(QStringLiteral("frame.png"));
    m_controller->exportFrame(QUrl::fromLocalFile(path));
    QTRY_VERIFY_WITH_TIMEOUT(!m_controller->exporting(), 30000);
    QCOMPARE(QImage(path).size(), QSize(512, 512));

    // Before/after comparison is available on the result view.
    findItem(QStringLiteral("viewTabs"))->setProperty("currentIndex", 2);
    // The String art view shows the thread, its error map or a comparison.
    QQuickItem *mode = findItem(QStringLiteral("resultModeBox"));
    QVERIFY(mode);
    QVERIFY(mode->isVisible() && mode->isEnabled());
    QQuickItem *panel = findItem(QStringLiteral("viewTabs"))->parentItem();
    while (panel && !panel->property("imageSource").isValid())
        panel = panel->parentItem();
    QVERIFY(panel);
    QVERIFY(panel->property("imageSource").toString().contains(QStringLiteral("/result/")));
    mode->setProperty("currentIndex", 1);
    QVERIFY(panel->property("imageSource").toString().contains(QStringLiteral("/residual/")));
    findItem(QStringLiteral("viewTabs"))->setProperty("currentIndex", 1);
    QVERIFY(!mode->isVisible());
    QVERIFY(!panel->property("showErrorMap").toBool());
    QVERIFY(panel->property("imageSource").toString().contains(QStringLiteral("/processed/")));
    findItem(QStringLiteral("viewTabs"))->setProperty("currentIndex", 2);
    mode->setProperty("currentIndex", 2);
    QVERIFY(panel->property("showCompare").toBool());
    QVERIFY(panel->property("imageSource").toString().contains(QStringLiteral("/result/")));

    QTest::qWait(300);
    const QByteArray shot = qgetenv("STRINGART_UI_PLAYBACK");
    if (!shot.isEmpty())
        QVERIFY(m_window->grabWindow().save(QString::fromLocal8Bit(shot)));
}

void TestUi::colourWorkflow()
{
    // A colourful test picture: red left half, blue right half, black disk.
    QImage picture(400, 400, QImage::Format_RGB32);
    picture.fill(Qt::white);
    {
        QPainter p(&picture);
        p.fillRect(QRect(0, 0, 200, 400), QColor(210, 40, 40));
        p.fillRect(QRect(200, 0, 200, 400), QColor(40, 60, 210));
        p.setBrush(Qt::black);
        p.drawEllipse(QRect(150, 150, 100, 100));
    }
    const QString picturePath = m_dir.filePath(QStringLiteral("colour.png"));
    QVERIFY(picture.save(picturePath));
    QVERIFY(m_controller->loadImage(QUrl::fromLocalFile(picturePath)));

    m_controller->setProperty("colorMode", true);
    m_controller->setPalette({QColor(210, 40, 40), QColor(40, 60, 210), QColor(Qt::black)}, Qt::white);
    QCOMPARE(m_controller->palette().size(), 3);
    m_controller->setProperty("pinCount", 150);
    m_controller->setProperty("maxSegments", 1500);
    // The processed view shows colour once the debounce fires.
    QTRY_VERIFY(m_store->get(QStringLiteral("processed")).pixelColor(60, 256).red() > 150);

    m_controller->start();
    QTRY_VERIFY_WITH_TIMEOUT(!m_controller->running(), 120000);
    QVERIFY2(m_controller->resultIsColor(), qPrintable(m_controller->statusText()));
    QVERIFY(m_controller->resultSegments() > 100);
    QVERIFY(m_controller->error() < m_controller->initialError());
    const QImage result = m_store->get(QStringLiteral("result"));
    QVERIFY(result.pixelColor(110, 256).red() > result.pixelColor(110, 256).blue());
    QVERIFY(result.pixelColor(400, 256).blue() > result.pixelColor(400, 256).red());
    // Layered: the spool changes only between colours.
    m_controller->setPlaybackPosition(0);
    const QVariantList steps = m_controller->stepsAhead(m_controller->resultSegments());
    QCOMPARE(steps.size(), m_controller->resultSegments());
    int spoolChanges = 0;
    for (qsizetype i = 1; i < steps.size(); ++i) {
        spoolChanges += steps[i].toMap().value(QStringLiteral("color"))
                                != steps[i - 1].toMap().value(QStringLiteral("color"))
                            ? 1
                            : 0;
    }
    QVERIFY2(spoolChanges <= 2, qPrintable(QString::number(spoolChanges)));
    m_controller->setPlaybackPosition(m_controller->resultSegments());

    // Playback of colour steps.
    m_controller->setPlaybackPosition(5);
    QVERIFY(m_controller->palette().contains(QVariant::fromValue(m_controller->currentThreadColor())));
    m_controller->setPlaybackPosition(m_controller->resultSegments());

    // Colour exports and project round trip.
    const QString csv = m_dir.filePath(QStringLiteral("colour.csv"));
    const QString svg = m_dir.filePath(QStringLiteral("colour.svg"));
    const QString project = m_dir.filePath(QStringLiteral("colour.sasproj"));
    m_controller->exportCsv(QUrl::fromLocalFile(csv));
    m_controller->exportSvg(QUrl::fromLocalFile(svg));
    QFile csvFile(csv);
    QVERIFY(csvFile.open(QIODevice::ReadOnly));
    const QList<QByteArray> lines = csvFile.readAll().trimmed().split('\n');
    QVERIFY(lines.first().startsWith("step,color,color_hex"));
    QCOMPARE(lines.size(), m_controller->resultSegments() + 1);
    QFile svgFile(svg);
    QVERIFY(svgFile.open(QIODevice::ReadOnly));
    QCOMPARE(svgFile.readAll().count("<line "), m_controller->resultSegments());
    const QString png = m_dir.filePath(QStringLiteral("colour.png.out.png"));
    m_controller->setProperty("exportSize", 1024);
    m_controller->exportPng(QUrl::fromLocalFile(png));
    QTRY_VERIFY_WITH_TIMEOUT(!m_controller->exporting(), 30000);
    QVERIFY(QImage(png).pixelColor(220, 512).red() > QImage(png).pixelColor(220, 512).blue());

    const int segments = m_controller->resultSegments();
    QVERIFY(m_controller->saveProject(QUrl::fromLocalFile(project)));
    m_controller->setProperty("colorMode", false);
    QVERIFY(m_controller->loadImage(m_portrait));
    QVERIFY(m_controller->openProject(QUrl::fromLocalFile(project)));
    QVERIFY(m_controller->property("colorMode").toBool());
    QVERIFY(m_controller->resultIsColor());
    QCOMPARE(m_controller->resultSegments(), segments);

    // One thread per used colour; together they make up the total length.
    double usageSum = 0.0;
    int usageSegments = 0;
    for (const QVariant &row : m_controller->threadUsage()) {
        usageSum += row.toMap().value(QStringLiteral("lengthM")).toDouble();
        usageSegments += row.toMap().value(QStringLiteral("segments")).toInt();
    }
    QCOMPARE(usageSegments, segments);
    QVERIFY(std::abs(usageSum - m_controller->threadLengthM()) < 0.01 * m_controller->threadLengthM());
    bool sawChange = false;
    for (int p = 1; p <= segments && !sawChange; ++p) {
        m_controller->setPlaybackPosition(p);
        sawChange = m_controller->threadChangesHere();
    }
    QVERIFY(sawChange);
    m_controller->setPlaybackPosition(segments);

    findItem(QStringLiteral("categoryTabs"))->setProperty("currentIndex", 2);
    findItem(QStringLiteral("viewTabs"))->setProperty("currentIndex", 2);
    QTest::qWait(300);
    const QByteArray shot = qgetenv("STRINGART_UI_COLOR");
    if (!shot.isEmpty())
        QVERIFY(m_window->grabWindow().save(QString::fromLocal8Bit(shot)));
}

void TestUi::workshopWorkflow()
{
    // The pin template needs only a frame.
    const QString templatePath = m_dir.filePath(QStringLiteral("template.pdf"));
    m_controller->exportPinTemplate(QUrl::fromLocalFile(templatePath), 0);
    QTRY_VERIFY_WITH_TIMEOUT(!m_controller->exporting(), 30000);
    QFile templateFile(templatePath);
    QVERIFY2(templateFile.open(QIODevice::ReadOnly), qPrintable(m_controller->statusText()));
    QVERIFY(templateFile.read(4) == "%PDF");

    QVERIFY(m_controller->loadImage(m_portrait));
    m_controller->setProperty("pinCount", 150);
    m_controller->setProperty("maxSegments", 400);
    m_controller->start();
    QTRY_VERIFY_WITH_TIMEOUT(!m_controller->running(), 60000);
    const int total = m_controller->resultSegments();
    QVERIFY(total > 10);

    const QVariantList usage = m_controller->threadUsage();
    QCOMPARE(usage.size(), 1);
    QCOMPARE(usage.first().toMap().value(QStringLiteral("segments")).toInt(), total);
    QVERIFY(std::abs(usage.first().toMap().value(QStringLiteral("lengthM")).toDouble() - m_controller->threadLengthM())
            < 1e-6 * m_controller->threadLengthM() + 1e-9);

    const QString guidePath = m_dir.filePath(QStringLiteral("guide.pdf"));
    m_controller->exportWindingGuide(QUrl::fromLocalFile(guidePath));
    QTRY_VERIFY_WITH_TIMEOUT(!m_controller->exporting(), 30000);
    QFile guideFile(guidePath);
    QVERIFY2(guideFile.open(QIODevice::ReadOnly), qPrintable(m_controller->statusText()));
    QVERIFY(guideFile.read(4) == "%PDF");

    // Winding mode starts at step 1 and follows the keys.
    QQuickItem *button = findItem(QStringLiteral("windingModeButton"));
    QVERIFY(button);
    QMetaObject::invokeMethod(button, "clicked");
    QVERIFY(m_controller->winding());
    QCOMPARE(m_controller->playbackPosition(), 1);
    QQuickItem *target = nullptr;
    QTRY_VERIFY((target = findItem(QStringLiteral("windingTargetPin"))) && target->isVisible());
    QCOMPARE(target->property("text").toString(), QStringLiteral("→ %1").arg(m_controller->currentToPin()));
    QCOMPARE(m_controller->stepsAhead(3).size(), 3);
    QCOMPARE(m_controller->stepsAhead(3).first().toMap().value(QStringLiteral("from")).toInt(),
             m_controller->currentToPin());

    QTest::keyClick(m_window, Qt::Key_Space);
    QCOMPARE(m_controller->playbackPosition(), 2);
    QTest::keyClick(m_window, Qt::Key_PageDown);
    QTest::keyClick(m_window, Qt::Key_Right);
    QCOMPARE(m_controller->playbackPosition(), 4);
    QTest::keyClick(m_window, Qt::Key_Left);
    QCOMPARE(m_controller->playbackPosition(), 3);
    QCOMPARE(target->property("text").toString(), QStringLiteral("→ %1").arg(m_controller->currentToPin()));
    // Back stops at the first step.
    for (int i = 0; i < 5; ++i)
        QTest::keyClick(m_window, Qt::Key_Backspace);
    QCOMPARE(m_controller->playbackPosition(), 1);
    QTest::keyClick(m_window, Qt::Key_Space);
    QTest::keyClick(m_window, Qt::Key_Space);

    const QByteArray shot = qgetenv("STRINGART_UI_WINDING");
    if (!shot.isEmpty()) {
        QTest::qWait(300);
        QVERIFY(m_window->grabWindow().save(QString::fromLocal8Bit(shot)));
    }

    // Escape leaves winding mode and shows the whole result again.
    QTest::keyClick(m_window, Qt::Key_Escape);
    QTRY_VERIFY(!m_controller->winding());
    QCOMPARE(m_controller->playbackPosition(), total);

    // The position is remembered for this result, also across restarts.
    destroyUi();
    createUi();
    QVERIFY(m_controller->loadImage(m_portrait));
    m_controller->setProperty("pinCount", 150);
    m_controller->setProperty("maxSegments", 400);
    m_controller->start();
    QTRY_VERIFY_WITH_TIMEOUT(!m_controller->running(), 60000);
    m_controller->beginWinding();
    QCOMPARE(m_controller->playbackPosition(), 3);
    m_controller->endWinding();
}

void TestUi::paletteSuggestion()
{
    QImage picture(300, 300, QImage::Format_RGB32);
    picture.fill(Qt::white);
    {
        QPainter p(&picture);
        p.fillRect(QRect(0, 0, 150, 300), QColor(210, 40, 40));
        p.fillRect(QRect(150, 0, 150, 300), QColor(40, 60, 210));
    }
    const QString picturePath = m_dir.filePath(QStringLiteral("suggest.png"));
    QVERIFY(picture.save(picturePath));
    QVERIFY(m_controller->loadImage(QUrl::fromLocalFile(picturePath)));
    m_controller->setProperty("colorMode", true);
    m_controller->setProperty("pinCount", 150);
    m_controller->setProperty("maxSegments", 1500);
    m_controller->setPalette({QColor(0, 120, 0), QColor(Qt::yellow)}, Qt::white);
    findItem(QStringLiteral("categoryTabs"))->setProperty("currentIndex", 2);

    // The button asks for as many colours as the palette has (2).
    QQuickItem *button = nullptr;
    QTRY_VERIFY((button = findItem(QStringLiteral("suggestPaletteButton"))) && button->isVisible());
    QTRY_VERIFY(button->isEnabled());
    QMetaObject::invokeMethod(button, "clicked");
    QVERIFY(m_controller->suggestingPalette());
    QTRY_VERIFY_WITH_TIMEOUT(!m_controller->suggestingPalette(), 60000);
    const QVariantList palette = m_controller->palette();
    QCOMPARE(palette.size(), 2);
    // The greens are replaced by colours of the image (black and red score
    // best here under the objective, slightly ahead of red and blue).
    bool imageColour = false;
    for (const QVariant &v : palette) {
        const QColor c = v.value<QColor>();
        QVERIFY2(c.green() <= std::max(c.red(), c.blue()), qPrintable(c.name()));
        imageColour |= c.red() > c.blue() + 60 || c.blue() > c.red() + 60;
    }
    QVERIFY2(imageColour, qPrintable(m_controller->statusText()));

    // Cancelling keeps the palette.
    m_controller->suggestPalette(4);
    m_controller->cancelPaletteSuggestion();
    QTRY_VERIFY_WITH_TIMEOUT(!m_controller->suggestingPalette(), 30000);
    QCOMPARE(m_controller->palette(), palette);

    // Loading another image drops a running suggestion silently.
    m_controller->suggestPalette(3);
    QVERIFY(m_controller->suggestingPalette());
    QVERIFY(m_controller->loadImage(m_portrait));
    const QString loaded = m_controller->statusText();
    QTRY_VERIFY_WITH_TIMEOUT(!m_controller->suggestingPalette(), 30000);
    QCOMPARE(m_controller->palette(), palette);
    QCOMPARE(m_controller->statusText(), loaded);

    // Destroying the UI while a suggestion runs is safe.
    m_controller->suggestPalette(3);
    QVERIFY(m_controller->suggestingPalette());
}

void TestUi::cropHandles()
{
    QVERIFY(m_controller->loadImage(m_portrait));
    findItem(QStringLiteral("categoryTabs"))->setProperty("currentIndex", 0); // Frame page: Original view
    QQuickItem *topLeft = nullptr;
    QTRY_VERIFY((topLeft = findItem(QStringLiteral("cropHandle00"))) && topLeft->isVisible());
    // Circle: four corners, no edge handles.
    for (const char *name : {"cropHandle10", "cropHandle11", "cropHandle01"})
        QVERIFY(findItem(QString::fromLatin1(name))->isVisible());
    QVERIFY(!findItem(QStringLiteral("cropHandle50"))->isVisible());

    auto drag = [&](const QString &name, QPoint delta) {
        QQuickItem *handle = findItem(name);
        const QPoint from = handle->mapToScene(QPointF(handle->width() / 2, handle->height() / 2)).toPoint();
        QTest::mousePress(m_window, Qt::LeftButton, {}, from);
        for (int i = 1; i <= 5; ++i)
            QTest::mouseMove(m_window, from + delta * i / 5);
        QTest::mouseRelease(m_window, Qt::LeftButton, {}, from + delta);
        QTest::qWait(150);
    };
    auto near = [](double a, double b) { return std::abs(a - b) < 0.02; };

    // Top-left corner outward: the bottom-right corner stays, the aspect is kept.
    QRectF before = m_controller->effectiveCrop();
    drag(QStringLiteral("cropHandle00"), QPoint(-40, -40));
    QRectF after = m_controller->effectiveCrop();
    QVERIFY2(after.width() > before.width() + 0.02, qPrintable(QString::number(after.width())));
    QVERIFY(near(after.right(), before.right()) && near(after.bottom(), before.bottom()));
    QVERIFY(near(after.width() / after.height(), before.width() / before.height()));

    // Ellipse: edge handles appear; the left edge moves only the left side and
    // changes the frame aspect (height in mm follows).
    m_controller->setProperty("frameShape", ProjectController::Ellipse);
    QTRY_VERIFY(findItem(QStringLiteral("cropHandle05"))->isVisible());
    QTest::qWait(150);
    before = m_controller->effectiveCrop();
    const double heightMm = m_controller->property("frameHeightMm").toDouble();
    drag(QStringLiteral("cropHandle05"), QPoint(-60, 0));
    after = m_controller->effectiveCrop();
    QVERIFY(after.left() < before.left() - 0.02);
    QVERIFY(near(after.right(), before.right()) && near(after.top(), before.top())
            && near(after.bottom(), before.bottom()));
    QVERIFY(m_controller->property("frameHeightMm").toDouble() < heightMm); // wider frame, same width in mm
    // Top edge up: only the top moves.
    before = after;
    drag(QStringLiteral("cropHandle50"), QPoint(0, -50));
    after = m_controller->effectiveCrop();
    QVERIFY(after.top() < before.top() - 0.02);
    QVERIFY(near(after.left(), before.left()) && near(after.bottom(), before.bottom()));
    // Ellipse corners resize freely.
    before = after;
    drag(QStringLiteral("cropHandle10"), QPoint(30, -10));
    after = m_controller->effectiveCrop();
    QVERIFY(near(after.left(), before.left()) && near(after.bottom(), before.bottom()));
    QVERIFY(after.right() > before.right() && after.top() < before.top());
}

void TestUi::effortLevels()
{
    findItem(QStringLiteral("categoryTabs"))->setProperty("currentIndex", 3);
    QQuickItem *effort = nullptr;
    QTRY_VERIFY((effort = findItem(QStringLiteral("effortField"))) && effort->isVisible());
    QCOMPARE(m_controller->property("workingSize").toInt(), 512); // Medium by default
    QCOMPARE(effort->property("currentIndex").toInt(), 1);
    QCOMPARE(effort->property("displayText").toString(), QStringLiteral("Medium"));
    QMetaObject::invokeMethod(effort, "activated", Q_ARG(int, 2));
    QCOMPARE(m_controller->property("workingSize").toInt(), 1024);
    QCOMPARE(effort->property("displayText").toString(), QStringLiteral("High"));
    QMetaObject::invokeMethod(effort, "activated", Q_ARG(int, 0));
    QCOMPARE(m_controller->property("workingSize").toInt(), 384);
    // A resolution from a project that is not one of the levels.
    m_controller->setProperty("workingSize", 768);
    QCOMPARE(effort->property("displayText").toString(), QStringLiteral("Custom"));
}

void TestUi::threadDiameterInMillimetres()
{
    // The thread is set in millimetres and converted to working pixels.
    QVERIFY(std::abs(m_controller->threadWidthPx() - 0.3 / m_controller->mmPerPixel()) < 1e-9);
    const double before = m_controller->threadWidthPx();
    m_controller->setProperty("frameWidthMm", 250.0); // half the frame: twice the pixels per mm
    QVERIFY(std::abs(m_controller->threadWidthPx() - 2.0 * before) < 0.02 * before);
    m_controller->setProperty("frameWidthMm", 500.0);

    // A run uses the converted width; the project keeps the diameter.
    QVERIFY(m_controller->loadImage(m_portrait));
    m_controller->setProperty("threadDiameterMm", 0.5);
    m_controller->setProperty("maxSegments", 200);
    m_controller->start();
    QTRY_VERIFY_WITH_TIMEOUT(!m_controller->running(), 60000);
    const QString path = m_dir.filePath(QStringLiteral("diameter.sasproj"));
    QVERIFY(m_controller->saveProject(QUrl::fromLocalFile(path)));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const sa::ProjectData project = sa::ProjectFile::fromJson(file.readAll());
    QCOMPARE(project.threadDiameterMm, 0.5);
    QVERIFY(std::abs(project.config.threadWidth - 0.5 / m_controller->mmPerPixel()) < 1e-9);
}

void TestUi::undoRedo()
{
    QVERIFY(!m_controller->canUndo());
    m_controller->setProperty("pinCount", 150);
    QVERIFY(m_controller->canUndo()); // pending edit counts
    QTest::qWait(600);                 // past the debounce: its own step
    // Quick successive edits form one step.
    m_controller->setProperty("opacity", 0.3);
    m_controller->setProperty("opacity", 0.35);
    m_controller->setProperty("opacity", 0.4);
    m_controller->undo(); // flushes the pending edit, then undoes it
    QCOMPARE(m_controller->property("opacity").toFloat(), 0.65f); // stored as float
    QCOMPARE(m_controller->property("pinCount").toInt(), 150);
    QVERIFY(m_controller->canRedo());
    m_controller->undo();
    QCOMPARE(m_controller->property("pinCount").toInt(), 240);
    QVERIFY(!m_controller->canUndo());
    // The applied state is not recorded as a new edit.
    QTest::qWait(600);
    QVERIFY(m_controller->canRedo());
    m_controller->redo();
    m_controller->redo();
    QCOMPARE(m_controller->property("pinCount").toInt(), 150);
    QCOMPARE(m_controller->property("opacity").toFloat(), 0.4f); // stored as float
    // A new edit drops the redo branch.
    m_controller->undo();
    m_controller->setProperty("threadDiameterMm", 0.5);
    QTRY_VERIFY(!m_controller->canRedo() && m_controller->canUndo());
    QCOMPARE(m_controller->property("opacity").toFloat(), 0.65f); // stored as float

    // Keyboard and toolbar.
    QTest::keySequence(m_window, QKeySequence::Undo);
    QCOMPARE(m_controller->property("threadDiameterMm").toDouble(), 0.3);
    QQuickItem *undoButton = findItem(QStringLiteral("undoButton"));
    QVERIFY(undoButton);
    QTRY_VERIFY(undoButton->isEnabled());
    QMetaObject::invokeMethod(undoButton, "clicked");
    QCOMPARE(m_controller->property("pinCount").toInt(), 240);
}

void TestUi::presets()
{
    m_controller->setProperty("pinCount", 180);
    m_controller->setProperty("opacity", 0.3);
    QVERIFY(m_controller->savePreset(QStringLiteral("  Fine  ")));
    QVERIFY(!m_controller->savePreset(QStringLiteral("   ")));
    QCOMPARE(m_controller->presetNames(), QStringList{QStringLiteral("Fine")});
    m_controller->setProperty("pinCount", 300);
    m_controller->setProperty("opacity", 0.1);
    QVERIFY(m_controller->loadPreset(QStringLiteral("Fine")));
    QCOMPARE(m_controller->property("pinCount").toInt(), 180);
    QCOMPARE(m_controller->property("opacity").toFloat(), 0.3f); // stored as float
    QVERIFY(!m_controller->loadPreset(QStringLiteral("missing")));
    // Loading a preset is undoable.
    m_controller->undo();
    QCOMPARE(m_controller->property("pinCount").toInt(), 300);

    // Presets are stored in the settings, not in the session.
    destroyUi();
    createUi();
    QCOMPARE(m_controller->presetNames(), QStringList{QStringLiteral("Fine")});
    m_controller->deletePreset(QStringLiteral("Fine"));
    QVERIFY(m_controller->presetNames().isEmpty());
}

void TestUi::runHistory()
{
    QVERIFY(m_controller->loadImage(m_portrait));
    m_controller->setProperty("maxSegments", 300);
    m_controller->setProperty("pinCount", 120);
    m_controller->start();
    QTRY_VERIFY_WITH_TIMEOUT(!m_controller->running(), 60000);
    m_controller->setProperty("pinCount", 200);
    m_controller->start();
    QTRY_VERIFY_WITH_TIMEOUT(!m_controller->running(), 60000);

    QVariantList runs = m_controller->runHistory();
    QCOMPARE(runs.size(), 2);
    const QVariantMap newest = runs[0].toMap();
    const QVariantMap oldest = runs[1].toMap();
    QVERIFY(newest.value(QStringLiteral("id")).toInt() > oldest.value(QStringLiteral("id")).toInt());
    QVERIFY(newest.value(QStringLiteral("summary")).toString().startsWith(QStringLiteral("200 pins")));
    QCOMPARE(newest.value(QStringLiteral("error")).toDouble(), m_controller->error());
    QVERIFY(!m_store->get(QStringLiteral("run%1").arg(oldest.value(QStringLiteral("id")).toInt())).isNull());

    findItem(QStringLiteral("viewTabs"))->setProperty("currentIndex", 3);
    QTRY_VERIFY(findItem(QStringLiteral("runsView"))->isVisible());
    QTest::qWait(300);
    const QByteArray shot = qgetenv("STRINGART_UI_RUNS");
    if (!shot.isEmpty())
        QVERIFY(m_window->grabWindow().save(QString::fromLocal8Bit(shot)));

    m_controller->restoreRunSettings(oldest.value(QStringLiteral("id")).toInt());
    QCOMPARE(m_controller->property("pinCount").toInt(), 120);
    m_controller->removeRun(oldest.value(QStringLiteral("id")).toInt());
    QCOMPARE(m_controller->runHistory().size(), 1);
    QVERIFY(m_store->get(QStringLiteral("run%1").arg(oldest.value(QStringLiteral("id")).toInt())).isNull());

    // A new image starts a new comparison.
    QVERIFY(m_controller->loadImage(m_portrait));
    QVERIFY(m_controller->runHistory().isEmpty());
}

QTEST_MAIN(TestUi)
#include "tst_ui.moc"
