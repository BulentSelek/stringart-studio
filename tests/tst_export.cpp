#include "core/ExportManager.h"
#include "core/ThreadRenderer.h"

#include <QFile>
#include <QImageReader>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace sa;

class TestExport : public QObject
{
    Q_OBJECT

private slots:
    void csvContents();
    void csvPixelsAndMillimetres();
    void csvEmptySequence();
    void csvWriteFile();
    void pngWriteAndReadBack();
    void pngNullImageFails();
    void highResolutionRender();
};

void TestExport::csvContents()
{
    const PinLayout layout = PinLayout::circular(4, 102, 102, 100.0);
    // radius 50 px -> 100 mm diameter, so 1 px = 1 mm.
    const std::vector<int> sequence{0, 2, 1};
    const QStringList lines = ExportManager::csvText(layout, sequence, LengthUnit::Millimeters)
                                  .split(u'\n', Qt::SkipEmptyParts);
    QCOMPARE(lines.size(), 3);
    QCOMPARE(lines[0], QStringLiteral(
                           "segment,from_pin,to_pin,from_x_mm,from_y_mm,to_x_mm,to_y_mm,length_mm,cumulative_length_mm"));
    QCOMPARE(lines[1], QStringLiteral("1,0,2,51.00,1.00,51.00,101.00,100.00,100.00"));
    const QStringList second = lines[2].split(u',');
    QCOMPARE(second.size(), 9);
    QCOMPARE(second[0], QStringLiteral("2"));
    QCOMPARE(second[1], QStringLiteral("2"));
    QCOMPARE(second[2], QStringLiteral("1"));
    const double length = second[7].toDouble();
    QVERIFY(std::abs(length - 50.0 * std::sqrt(2.0)) < 0.01);
    QVERIFY(std::abs(second[8].toDouble() - (100.0 + length)) < 0.01);
}

void TestExport::csvPixelsAndMillimetres()
{
    const PinLayout layout = PinLayout::circular(10, 202, 202, 400.0); // 2 mm per px
    const std::vector<int> sequence{0, 5};
    const QStringList px = ExportManager::csvText(layout, sequence, LengthUnit::Pixels).split(u'\n');
    const QStringList mm = ExportManager::csvText(layout, sequence, LengthUnit::Millimeters).split(u'\n');
    QVERIFY(px[0].contains(QStringLiteral("length_px")));
    QCOMPARE(px[1].split(u',')[7].toDouble(), 200.0);
    QCOMPARE(mm[1].split(u',')[7].toDouble(), 400.0);
}

void TestExport::csvEmptySequence()
{
    const PinLayout layout = PinLayout::circular(10, 64, 64);
    const QString text = ExportManager::csvText(layout, std::vector<int>{3}, LengthUnit::Millimeters);
    QCOMPARE(text.count(u'\n'), 1); // header only
}

void TestExport::csvWriteFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const PinLayout layout = PinLayout::circular(30, 64, 64);
    const std::vector<int> sequence{0, 10, 20, 5};
    const QString path = dir.filePath(QStringLiteral("seq.csv"));
    QString error;
    QVERIFY2(ExportManager::writeCsv(path, layout, sequence, LengthUnit::Millimeters, &error), qPrintable(error));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(file.readAll()), ExportManager::csvText(layout, sequence, LengthUnit::Millimeters));

    QVERIFY(!ExportManager::writeCsv(dir.filePath(QStringLiteral("missing/dir/x.csv")), layout, sequence,
                                     LengthUnit::Millimeters, &error));
    QVERIFY(!error.isEmpty());
}

void TestExport::pngWriteAndReadBack()
{
    QTemporaryDir dir;
    const PinLayout layout = PinLayout::circular(30, 64, 64);
    const std::vector<int> sequence{0, 15, 2, 17};
    const QImage image = ThreadRenderer::render(layout, sequence, RenderStyle{});
    const QString path = dir.filePath(QStringLiteral("art.png"));
    QString error;
    QVERIFY2(ExportManager::writePng(path, image, &error), qPrintable(error));
    QImageReader reader(path);
    QCOMPARE(reader.format(), QByteArray("png"));
    const QImage loaded = reader.read();
    QCOMPARE(loaded.size(), QSize(64, 64));
    QCOMPARE(loaded.convertToFormat(QImage::Format_RGB32), image);
}

void TestExport::pngNullImageFails()
{
    QTemporaryDir dir;
    QString error;
    QVERIFY(!ExportManager::writePng(dir.filePath(QStringLiteral("x.png")), QImage(), &error));
    QVERIFY(!error.isEmpty());
}

void TestExport::highResolutionRender()
{
    const PinLayout layout = PinLayout::circular(60, 128, 128);
    std::vector<int> sequence;
    for (int i = 0; i < 40; ++i)
        sequence.push_back((i * 23) % 60);
    RenderStyle style;
    style.opacity = 0.3f;
    const QImage low = ThreadRenderer::render(layout, sequence, style, 1.0);
    const QImage high = ThreadRenderer::render(layout, sequence, style, 4.0);
    QCOMPARE(high.size(), QSize(512, 512));
    // Box-downsampled high-resolution render approximates the working render.
    const QImage down = high.scaled(128, 128, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    double diff = 0.0;
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x)
            diff += std::abs(qGray(down.pixel(x, y)) - qGray(low.pixel(x, y)));
    QVERIFY2(diff / (128.0 * 128.0) < 6.0, qPrintable(QString::number(diff / (128.0 * 128.0))));

    // Custom colours: background where no thread passes.
    style.background = QColor(250, 240, 220);
    style.thread = QColor(120, 0, 0);
    const QImage coloured = ThreadRenderer::render(layout, sequence, style);
    QCOMPARE(coloured.pixel(0, 0), QColor(250, 240, 220).rgb());
}

QTEST_GUILESS_MAIN(TestExport)
#include "tst_export.moc"
