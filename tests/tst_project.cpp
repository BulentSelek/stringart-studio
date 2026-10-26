#include "core/ProjectFile.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <stdexcept>

using namespace sa;

class TestProject : public QObject
{
    Q_OBJECT

private slots:
    void roundTrip();
    void roundTripCustomFrame();
    void exportIncludesPins();
    void rejectsInvalidInput_data();
    void rejectsInvalidInput();
    void parsePinCsv();
    void parsePinCsvErrors();
};

namespace {

ProjectData sampleProject()
{
    ProjectData p;
    p.frame.kind = PinLayoutKind::Elliptical;
    p.frame.pinCount = 180;
    p.frame.widthMm = 600;
    p.frame.heightMm = 450;
    p.workingSize = 384;
    p.imagePath = QStringLiteral("/photos/portrait.jpg");
    p.imageData = QByteArray("\x89PNG\r\n\x1a\n binary \0 data", 22);
    p.crop = QRectF(0.1, 0.2, 0.5, 0.6);
    p.rotationDegrees = -12.5;
    p.tone = {0.1, 1.3, 0.8, true};
    p.config.maxSegments = 3210;
    p.config.maxThreadLengthMm = 123456.0;
    p.config.minPinSeparation = 15;
    p.config.threadOpacity = 0.17f;
    p.config.threadWidth = 1.4;
    p.config.allowRepeatedLines = true;
    p.config.startPin = 7;
    p.config.minImprovement = 0.001;
    p.edgeEmphasis = 2.5;
    p.threadColor = QColor(0x1a, 0x23, 0x40);
    p.backgroundColor = QColor(0xf4, 0xec, 0xd8);
    p.sequence = {7, 100, 20, 150, 3};
    p.stopReason = QStringLiteral("cancelled");
    p.initialError = 0.25;
    p.finalError = 0.0375;
    p.threadLengthMm = 2345.5;
    p.elapsedSeconds = 1.25;
    return p;
}

void compareProjects(const ProjectData &a, const ProjectData &b, bool withImage)
{
    QCOMPARE(a.frame, b.frame);
    QCOMPARE(a.workingSize, b.workingSize);
    QCOMPARE(a.imagePath, b.imagePath);
    QCOMPARE(a.imageData, withImage ? b.imageData : QByteArray());
    QCOMPARE(a.crop, b.crop);
    QCOMPARE(a.rotationDegrees, b.rotationDegrees);
    QCOMPARE(a.tone, b.tone);
    QCOMPARE(a.config.maxSegments, b.config.maxSegments);
    QCOMPARE(a.config.maxThreadLengthMm, b.config.maxThreadLengthMm);
    QCOMPARE(a.config.minPinSeparation, b.config.minPinSeparation);
    QCOMPARE(a.config.threadOpacity, b.config.threadOpacity);
    QCOMPARE(a.config.threadWidth, b.config.threadWidth);
    QCOMPARE(a.config.allowRepeatedLines, b.config.allowRepeatedLines);
    QCOMPARE(a.config.startPin, b.config.startPin);
    QCOMPARE(a.config.minImprovement, b.config.minImprovement);
    QCOMPARE(a.edgeEmphasis, b.edgeEmphasis);
    QCOMPARE(a.threadColor, b.threadColor);
    QCOMPARE(a.backgroundColor, b.backgroundColor);
    QCOMPARE(a.sequence, b.sequence);
    QCOMPARE(a.stopReason, b.stopReason);
    QCOMPARE(a.initialError, b.initialError);
    QCOMPARE(a.finalError, b.finalError);
    QCOMPARE(a.threadLengthMm, b.threadLengthMm);
    QCOMPARE(a.elapsedSeconds, b.elapsedSeconds);
}

} // namespace

void TestProject::roundTrip()
{
    const ProjectData original = sampleProject();
    const ProjectData loaded = ProjectFile::fromJson(ProjectFile::toJson(original, true, false));
    compareProjects(loaded, original, true);
    const ProjectData withoutImage = ProjectFile::fromJson(ProjectFile::toJson(original, false, true));
    compareProjects(withoutImage, original, false);
}

void TestProject::roundTripCustomFrame()
{
    ProjectData original = sampleProject();
    original.frame.kind = PinLayoutKind::Custom;
    original.frame.customPinsMm = {{0, 0}, {120.5, 0}, {120.5, 80.25}, {60, 100}, {0, 80.25}};
    original.config.startPin = 0;
    original.config.minPinSeparation = 1;
    original.sequence = {0, 2, 4, 1, 3};
    original.crop = QRectF();
    compareProjects(ProjectFile::fromJson(ProjectFile::toJson(original, true, true)), original, true);
}

void TestProject::exportIncludesPins()
{
    const ProjectData project = sampleProject();
    const QJsonObject root = QJsonDocument::fromJson(ProjectFile::toJson(project, false, true)).object();
    const QJsonArray pins = root.value(QStringLiteral("pins")).toArray();
    QCOMPARE(pins.size(), 180);
    const PinLayout layout = PinLayout::fromFrame(project.frame, project.workingSize);
    const QJsonObject pin10 = pins[10].toObject();
    QCOMPARE(pin10.value(QStringLiteral("index")).toInt(), 10);
    QVERIFY(std::abs(pin10.value(QStringLiteral("xMm")).toDouble() - layout.pin(10).x * layout.mmPerUnit()) < 1e-9);
    QCOMPARE(root.value(QStringLiteral("result")).toObject().value(QStringLiteral("segments")).toInt(), 4);
    QVERIFY(!root.value(QStringLiteral("image")).toObject().contains(QStringLiteral("data")));
}

void TestProject::rejectsInvalidInput_data()
{
    QTest::addColumn<QByteArray>("json");
    QTest::addColumn<QString>("message");

    const QByteArray valid = ProjectFile::toJson(sampleProject(), false, false);
    auto modified = [&](auto mutate) {
        QJsonObject root = QJsonDocument::fromJson(valid).object();
        mutate(root);
        return QJsonDocument(root).toJson();
    };
    QTest::newRow("not json") << QByteArray("{oops") << QStringLiteral("invalid JSON");
    QTest::newRow("array") << QByteArray("[]") << QStringLiteral("JSON object");
    QTest::newRow("format") << modified([](QJsonObject &r) { r[QStringLiteral("format")] = QStringLiteral("x"); })
                            << QStringLiteral("not a String Art Studio project");
    QTest::newRow("version") << modified([](QJsonObject &r) { r[QStringLiteral("version")] = 99; })
                             << QStringLiteral("unsupported project version");
    QTest::newRow("shape") << modified([](QJsonObject &r) {
        QJsonObject f = r[QStringLiteral("frame")].toObject();
        f[QStringLiteral("shape")] = QStringLiteral("hexagon");
        r[QStringLiteral("frame")] = f;
    }) << QStringLiteral("unknown frame shape");
    QTest::newRow("pin count") << modified([](QJsonObject &r) {
        QJsonObject f = r[QStringLiteral("frame")].toObject();
        f[QStringLiteral("pinCount")] = 2;
        r[QStringLiteral("frame")] = f;
    }) << QStringLiteral("invalid project settings");
    QTest::newRow("opacity") << modified([](QJsonObject &r) {
        QJsonObject o = r[QStringLiteral("optimization")].toObject();
        o[QStringLiteral("threadOpacity")] = 3.0;
        r[QStringLiteral("optimization")] = o;
    }) << QStringLiteral("threadOpacity");
    QTest::newRow("type") << modified([](QJsonObject &r) {
        QJsonObject o = r[QStringLiteral("optimization")].toObject();
        o[QStringLiteral("maxSegments")] = QStringLiteral("many");
        r[QStringLiteral("optimization")] = o;
    }) << QStringLiteral("must be a number");
    QTest::newRow("sequence") << modified([](QJsonObject &r) {
        QJsonObject res = r[QStringLiteral("result")].toObject();
        res[QStringLiteral("sequence")] = QJsonArray{0, 5, 180};
        r[QStringLiteral("result")] = res;
    }) << QStringLiteral("invalid pin index");
    QTest::newRow("base64") << modified([](QJsonObject &r) {
        QJsonObject i = r[QStringLiteral("image")].toObject();
        i[QStringLiteral("data")] = QStringLiteral("***");
        r[QStringLiteral("image")] = i;
    }) << QStringLiteral("base64");
}

void TestProject::rejectsInvalidInput()
{
    QFETCH(QByteArray, json);
    QFETCH(QString, message);
    try {
        static_cast<void>(ProjectFile::fromJson(json));
        QFAIL("expected an exception");
    } catch (const std::runtime_error &e) {
        QVERIFY2(QString::fromUtf8(e.what()).contains(message), e.what());
    }
}

void TestProject::parsePinCsv()
{
    const std::vector<Vec2> plain = ProjectFile::parsePinCsv(QStringLiteral("0,0\n10,0\n10,5\n"));
    QCOMPARE(plain, (std::vector<Vec2>{{0, 0}, {10, 0}, {10, 5}}));

    const std::vector<Vec2> header = ProjectFile::parsePinCsv(
        QStringLiteral("# exported pins\r\nindex;y_mm;x_mm\r\n0;1.5;2\r\n\r\n1;3;4\r\n2;5;6.25\r\n"));
    QCOMPARE(header, (std::vector<Vec2>{{2, 1.5}, {4, 3}, {6.25, 5}}));

    const std::vector<Vec2> spaces = ProjectFile::parsePinCsv(QStringLiteral("1 2\n3\t4\n5   6"));
    QCOMPARE(spaces, (std::vector<Vec2>{{1, 2}, {3, 4}, {5, 6}}));
}

void TestProject::parsePinCsvErrors()
{
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, static_cast<void>(ProjectFile::parsePinCsv(QStringLiteral("0,0\n1,1\n"))));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, static_cast<void>(ProjectFile::parsePinCsv(QStringLiteral("0,0\n1,a\n2,2\n3,3"))));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, static_cast<void>(ProjectFile::parsePinCsv(QStringLiteral("x,y\n1\n2,2\n3,3"))));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, static_cast<void>(ProjectFile::parsePinCsv(QString())));
}

QTEST_GUILESS_MAIN(TestProject)
#include "tst_project.moc"
