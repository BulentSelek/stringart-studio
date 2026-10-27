#include "core/WorkshopDocuments.h"

#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace sa;

namespace {

QByteArray readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

int pdfPageCount(const QByteArray &pdf)
{
    return static_cast<int>(QString::fromLatin1(pdf).count(QRegularExpression(QStringLiteral("/Type\\s*/Page\\b"))));
}

// Page sizes in points from every /MediaBox of the document.
QList<QSizeF> pdfMediaBoxes(const QByteArray &pdf)
{
    QList<QSizeF> sizes;
    const QRegularExpression re(QStringLiteral("/MediaBox\\s*\\[\\s*0\\s+0\\s+([0-9.]+)\\s+([0-9.]+)\\s*\\]"));
    for (auto it = re.globalMatch(QString::fromLatin1(pdf)); it.hasNext();) {
        const auto m = it.next();
        sizes.append(QSizeF(m.captured(1).toDouble(), m.captured(2).toDouble()));
    }
    return sizes;
}

constexpr double kPointsPerMm = 72.0 / 25.4;

} // namespace

class TestWorkshop : public QObject
{
    Q_OBJECT

private slots:
    void monochromeStepsFollowSequence();
    void threadUsagePerColour();
    void labelStep();
    void tilesCoverContentWithOverlap();
    void tilesPreferFewerSheets();
    void singleSheetIsContentSize();
    void templatePdfPages();
    void guidePdfPages();
    void guidePdfMultiColour();
    void unwritablePathFails();
};

void TestWorkshop::monochromeStepsFollowSequence()
{
    const std::vector<int> sequence{3, 7, 1, 9};
    const std::vector<ColorStep> steps = monochromeSteps(sequence);
    QCOMPARE(steps.size(), std::size_t{3});
    QCOMPARE(steps[0], (ColorStep{0, 3, 7}));
    QCOMPARE(steps[2], (ColorStep{0, 1, 9}));
    QVERIFY(monochromeSteps(std::vector<int>{4}).empty());
}

void TestWorkshop::threadUsagePerColour()
{
    // Radius 50 px and 100 mm diameter: 1 px = 1 mm.
    const PinLayout layout = PinLayout::circular(4, 102, 102, 100.0);
    const std::vector<ColorStep> steps{{0, 0, 2}, {1, 1, 3}, {0, 2, 1}};
    const std::vector<QColor> palette{Qt::black, Qt::red, Qt::blue};
    const std::vector<ThreadUsage> usage = threadUsage(layout, steps, palette);
    QCOMPARE(usage.size(), std::size_t{3});
    QCOMPARE(usage[0].segments, 2);
    QVERIFY(std::abs(usage[0].lengthMm - (100.0 + 50.0 * std::sqrt(2.0))) < 1e-6);
    QCOMPARE(usage[1].segments, 1);
    QVERIFY(std::abs(usage[1].lengthMm - 100.0) < 1e-6);
    QCOMPARE(usage[2].segments, 0);
    QCOMPARE(usage[2].lengthMm, 0.0);
    QCOMPARE(usage[1].color, QColor(Qt::red));
}

void TestWorkshop::labelStep()
{
    // A label needs 1.3x its height of spacing.
    QCOMPARE(pinLabelStep(10.0, 2.4), 1);
    QCOMPARE(pinLabelStep(2.0, 2.4), 2);
    QCOMPARE(pinLabelStep(0.7, 2.4), 5);
    QCOMPARE(pinLabelStep(0.5, 2.4), 10);
    QCOMPARE(pinLabelStep(0.15, 2.4), 25);
    QCOMPARE(pinLabelStep(0.1, 2.4), 50);
    for (double spacing : {0.01, 0.07, 0.3, 1.1, 4.0}) {
        const int step = pinLabelStep(spacing, 2.4);
        QVERIFY(spacing * step >= 2.4 * 1.3);
    }
}

void TestWorkshop::tilesCoverContentWithOverlap()
{
    const QRectF content(-14.0, -14.0, 528.0, 528.0);
    TemplateOptions options;
    options.paper = TemplatePaper::A4;
    const TilePlan plan = planTiles(content, options);
    QVERIFY(plan.pageCount() > 1);
    QRectF covered;
    for (int r = 0; r < plan.rows; ++r) {
        for (int c = 0; c < plan.columns; ++c) {
            const QRectF tile = plan.tileRect(r, c);
            QVERIFY(std::abs(tile.width() - (plan.pageMm.width() - 2 * options.marginMm)) < 1e-9);
            covered = covered.isNull() ? tile : covered.united(tile);
            if (c + 1 < plan.columns)
                QVERIFY(std::abs(tile.right() - plan.tileRect(r, c + 1).left() - options.overlapMm) < 1e-9);
            if (r + 1 < plan.rows)
                QVERIFY(std::abs(tile.bottom() - plan.tileRect(r + 1, c).top() - options.overlapMm) < 1e-9);
        }
    }
    QVERIFY(covered.contains(content));
    // Centred: equal spare space on both sides.
    QVERIFY(std::abs((content.left() - covered.left()) - (covered.right() - content.right())) < 1e-9);
    QVERIFY(std::abs((content.top() - covered.top()) - (covered.bottom() - content.bottom())) < 1e-9);
}

void TestWorkshop::tilesPreferFewerSheets()
{
    // 260 x 150 mm fits one landscape A4 sheet (281 x 194 printable) but
    // needs two portrait ones.
    TemplateOptions options;
    const TilePlan plan = planTiles(QRectF(0, 0, 260, 150), options);
    QCOMPARE(plan.pageCount(), 1);
    QVERIFY(plan.pageMm.width() > plan.pageMm.height());
    // A3 needs fewer sheets than A4 for a large frame.
    const QRectF big(0, 0, 600, 600);
    options.paper = TemplatePaper::A3;
    const int a3 = planTiles(big, options).pageCount();
    options.paper = TemplatePaper::A4;
    QVERIFY(a3 < planTiles(big, options).pageCount());
}

void TestWorkshop::singleSheetIsContentSize()
{
    TemplateOptions options;
    options.paper = TemplatePaper::SingleSheet;
    const QRectF content(5, 7, 400, 300);
    const TilePlan plan = planTiles(content, options);
    QCOMPARE(plan.pageCount(), 1);
    QCOMPARE(plan.pageMm, QSizeF(416, 316));
    QCOMPARE(plan.tileRect(0, 0), content);
}

void TestWorkshop::templatePdfPages()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const PinLayout layout = PinLayout::fromFrame(FrameSpec{PinLayoutKind::Circular, 240, 500.0, 500.0, {}}, 512);

    TemplateOptions options;
    options.title = QStringLiteral("Test frame");
    const QString tiledPath = dir.filePath(QStringLiteral("tiled.pdf"));
    QString error;
    QVERIFY2(WorkshopDocuments::writePinTemplatePdf(tiledPath, layout, options, &error), qPrintable(error));
    const QByteArray tiled = readAll(tiledPath);
    QVERIFY(tiled.startsWith("%PDF"));
    const TilePlan plan = planTiles(WorkshopDocuments::templateContentMm(layout), options);
    QCOMPARE(pdfPageCount(tiled), plan.pageCount());
    for (const QSizeF &size : pdfMediaBoxes(tiled)) {
        QVERIFY(std::abs(size.width() - plan.pageMm.width() * kPointsPerMm) < 1.0);
        QVERIFY(std::abs(size.height() - plan.pageMm.height() * kPointsPerMm) < 1.0);
    }

    options.paper = TemplatePaper::SingleSheet;
    const QString singlePath = dir.filePath(QStringLiteral("single.pdf"));
    QVERIFY2(WorkshopDocuments::writePinTemplatePdf(singlePath, layout, options, &error), qPrintable(error));
    const QByteArray single = readAll(singlePath);
    QCOMPARE(pdfPageCount(single), 1);
    const QRectF content = WorkshopDocuments::templateContentMm(layout);
    const QList<QSizeF> boxes = pdfMediaBoxes(single);
    QVERIFY(!boxes.isEmpty());
    // 1:1 scale: the sheet is the frame plus labels plus margins.
    QVERIFY(std::abs(boxes.first().width() - (content.width() + 2 * options.marginMm) * kPointsPerMm) < 1.0);
    QVERIFY(std::abs(boxes.first().height() - (content.height() + 2 * options.marginMm) * kPointsPerMm) < 1.0);
    QVERIFY(content.width() > 500.0);
}

void TestWorkshop::guidePdfPages()
{
    QTemporaryDir dir;
    const PinLayout layout = PinLayout::circular(200, 402, 402, 400.0);
    std::vector<int> shortSequence{0, 100};
    std::vector<int> longSequence;
    for (int i = 0; i < 3001; ++i)
        longSequence.push_back((i * 37) % 200);
    WorkshopDocuments::GuideInfo info;
    info.title = QStringLiteral("Portrait");
    info.frameDescription = QStringLiteral("circular frame, 400 mm, 200 pins");
    info.palette = {Qt::black};

    QString error;
    const QString a = dir.filePath(QStringLiteral("short.pdf"));
    const QString b = dir.filePath(QStringLiteral("long.pdf"));
    QVERIFY2(WorkshopDocuments::writeWindingGuidePdf(a, layout, monochromeSteps(shortSequence), info, &error),
             qPrintable(error));
    QVERIFY2(WorkshopDocuments::writeWindingGuidePdf(b, layout, monochromeSteps(longSequence), info, &error),
             qPrintable(error));
    QCOMPARE(pdfPageCount(readAll(a)), 1);
    const int pages = pdfPageCount(readAll(b));
    // 3000 steps at roughly 50 rows x 5 columns per page.
    QVERIFY2(pages >= 11 && pages <= 16, qPrintable(QString::number(pages)));
}

void TestWorkshop::guidePdfMultiColour()
{
    QTemporaryDir dir;
    const PinLayout layout = PinLayout::circular(120, 302, 302, 300.0);
    std::vector<ColorStep> steps;
    for (int i = 0; i < 500; ++i)
        steps.push_back({i % 3 == 0 ? 1 : 0, (i * 7) % 120, (i * 7 + 50) % 120});
    WorkshopDocuments::GuideInfo info;
    info.palette = {Qt::black, QColor(176, 58, 46), QColor(31, 78, 140)}; // third colour unused
    const QString path = dir.filePath(QStringLiteral("colour.pdf"));
    QString error;
    QVERIFY2(WorkshopDocuments::writeWindingGuidePdf(path, layout, steps, info, &error), qPrintable(error));
    const QByteArray pdf = readAll(path);
    QVERIFY(pdf.startsWith("%PDF"));
    QVERIFY(pdfPageCount(pdf) >= 2);
}

void TestWorkshop::unwritablePathFails()
{
    const PinLayout layout = PinLayout::circular(60, 202, 202, 200.0);
    QString error;
    QVERIFY(!WorkshopDocuments::writePinTemplatePdf(QStringLiteral("/nonexistent-dir/x/template.pdf"), layout, {},
                                                    &error));
    QVERIFY(!error.isEmpty());
}

QTEST_MAIN(TestWorkshop)
#include "tst_workshop.moc"
