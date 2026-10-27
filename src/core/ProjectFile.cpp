#include "core/ProjectFile.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace sa {

namespace {
const QString kLayerOrders[] = {QStringLiteral("auto"), QStringLiteral("light-to-dark"),
                                 QStringLiteral("dark-to-light"), QStringLiteral("palette")};
} // namespace

namespace {

constexpr auto kFormat = "stringart-studio-project";

[[noreturn]] void fail(const QString &message)
{
    throw std::runtime_error(message.toStdString());
}

PinLayoutKind shapeFromName(const QString &name)
{
    for (PinLayoutKind kind : {PinLayoutKind::Circular, PinLayoutKind::Elliptical, PinLayoutKind::Rectangular,
                               PinLayoutKind::Custom}) {
        if (ProjectFile::shapeName(kind) == name)
            return kind;
    }
    fail(QStringLiteral("unknown frame shape \"%1\"").arg(name));
}

QJsonObject requireObject(const QJsonObject &parent, const QString &key)
{
    const QJsonValue value = parent.value(key);
    if (!value.isObject())
        fail(QStringLiteral("missing object \"%1\"").arg(key));
    return value.toObject();
}

double number(const QJsonObject &object, const QString &key, double fallback)
{
    const QJsonValue value = object.value(key);
    if (value.isUndefined())
        return fallback;
    if (!value.isDouble())
        fail(QStringLiteral("\"%1\" must be a number").arg(key));
    return value.toDouble();
}

int integer(const QJsonObject &object, const QString &key, int fallback)
{
    const double v = number(object, key, fallback);
    if (v != std::floor(v) || v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max())
        fail(QStringLiteral("\"%1\" must be an integer").arg(key));
    return static_cast<int>(v);
}

bool boolean(const QJsonObject &object, const QString &key, bool fallback)
{
    const QJsonValue value = object.value(key);
    if (value.isUndefined())
        return fallback;
    if (!value.isBool())
        fail(QStringLiteral("\"%1\" must be true or false").arg(key));
    return value.toBool();
}

QColor color(const QJsonObject &object, const QString &key, const QColor &fallback)
{
    const QJsonValue value = object.value(key);
    if (value.isUndefined())
        return fallback;
    const QColor c = QColor::fromString(value.toString());
    if (!c.isValid())
        fail(QStringLiteral("\"%1\" is not a colour").arg(key));
    return c;
}

} // namespace

QString ProjectFile::shapeName(PinLayoutKind kind)
{
    switch (kind) {
    case PinLayoutKind::Circular:
        return QStringLiteral("circular");
    case PinLayoutKind::Elliptical:
        return QStringLiteral("elliptical");
    case PinLayoutKind::Rectangular:
        return QStringLiteral("rectangular");
    case PinLayoutKind::Custom:
        return QStringLiteral("custom");
    }
    return {};
}

QByteArray ProjectFile::toJson(const ProjectData &project, bool embedImage, bool includePins)
{
    QJsonObject frame{
        {QStringLiteral("shape"), shapeName(project.frame.kind)},
        {QStringLiteral("pinCount"), project.frame.pinCount},
        {QStringLiteral("widthMm"), project.frame.widthMm},
        {QStringLiteral("heightMm"), project.frame.heightMm},
    };
    if (project.frame.kind == PinLayoutKind::Custom) {
        QJsonArray pins;
        for (const Vec2 &p : project.frame.customPinsMm)
            pins.append(QJsonArray{p.x, p.y});
        frame.insert(QStringLiteral("customPinsMm"), pins);
    }

    QJsonObject image{
        {QStringLiteral("path"), project.imagePath},
        {QStringLiteral("rotationDegrees"), project.rotationDegrees},
        {QStringLiteral("brightness"), project.tone.brightness},
        {QStringLiteral("contrast"), project.tone.contrast},
        {QStringLiteral("gamma"), project.tone.gamma},
        {QStringLiteral("invert"), project.tone.invert},
    };
    if (!project.crop.isEmpty()) {
        image.insert(QStringLiteral("crop"), QJsonArray{project.crop.x(), project.crop.y(), project.crop.width(),
                                                         project.crop.height()});
    }
    if (embedImage && !project.imageData.isEmpty())
        image.insert(QStringLiteral("data"), QString::fromLatin1(project.imageData.toBase64()));

    const OptimizationConfig &c = project.config;
    const QJsonObject optimization{
        {QStringLiteral("algorithm"), QStringLiteral("greedy")},
        {QStringLiteral("maxSegments"), c.maxSegments},
        {QStringLiteral("maxThreadLengthMm"), c.maxThreadLengthMm},
        {QStringLiteral("minPinSeparation"), c.minPinSeparation},
        {QStringLiteral("threadOpacity"), c.threadOpacity},
        {QStringLiteral("threadWidth"), c.threadWidth},
        {QStringLiteral("allowRepeatedLines"), c.allowRepeatedLines},
        {QStringLiteral("startPin"), c.startPin},
        {QStringLiteral("minImprovement"), c.minImprovement},
        {QStringLiteral("edgeEmphasis"), project.edgeEmphasis},
    };

    QJsonObject root{
        {QStringLiteral("format"), QLatin1StringView(kFormat)},
        {QStringLiteral("version"), kVersion},
        {QStringLiteral("frame"), frame},
        {QStringLiteral("workingSize"), project.workingSize},
        {QStringLiteral("image"), image},
        {QStringLiteral("optimization"), optimization},
        {QStringLiteral("render"),
         QJsonObject{{QStringLiteral("threadColor"), project.threadColor.name()},
                     {QStringLiteral("backgroundColor"), project.backgroundColor.name()}}},
    };

    if (project.colorMode || !project.palette.empty()) {
        QJsonArray palette;
        for (const QColor &c : project.palette)
            palette.append(c.name());
        root.insert(QStringLiteral("color"),
                    QJsonObject{{QStringLiteral("enabled"), project.colorMode},
                                {QStringLiteral("palette"), palette},
                                {QStringLiteral("background"), project.colorBackground.name()},
                                {QStringLiteral("method"), project.layeredColors ? QStringLiteral("layered")
                                                                                 : QStringLiteral("interleaved")},
                                {QStringLiteral("layerOrder"), kLayerOrders[std::clamp(project.layerOrder, 0, 3)]}});
    }

    if (!project.sequence.empty() || !project.colorSteps.empty()) {
        QJsonObject result{{QStringLiteral("stopReason"), project.stopReason},
                           {QStringLiteral("initialError"), project.initialError},
                           {QStringLiteral("finalError"), project.finalError},
                           {QStringLiteral("threadLengthMm"), project.threadLengthMm},
                           {QStringLiteral("elapsedSeconds"), project.elapsedSeconds}};
        if (!project.colorSteps.empty()) {
            QJsonArray steps;
            for (const ColorStep &step : project.colorSteps)
                steps.append(QJsonArray{step.color, step.from, step.to});
            result.insert(QStringLiteral("steps"), steps);
            result.insert(QStringLiteral("segments"), static_cast<int>(project.colorSteps.size()));
        } else {
            QJsonArray sequence;
            for (int pin : project.sequence)
                sequence.append(pin);
            result.insert(QStringLiteral("sequence"), sequence);
            result.insert(QStringLiteral("segments"), static_cast<int>(project.sequence.size()) - 1);
        }
        root.insert(QStringLiteral("result"), result);
    }

    if (includePins) {
        const PinLayout layout = PinLayout::fromFrame(project.frame, project.workingSize);
        QJsonArray pins;
        for (int i = 0; i < layout.pinCount(); ++i) {
            const Vec2 &p = layout.pin(i);
            pins.append(QJsonObject{{QStringLiteral("index"), i},
                                    {QStringLiteral("xMm"), p.x * layout.mmPerUnit()},
                                    {QStringLiteral("yMm"), p.y * layout.mmPerUnit()}});
        }
        root.insert(QStringLiteral("pins"), pins);
    }
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

ProjectData ProjectFile::fromJson(const QByteArray &json)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        fail(QStringLiteral("invalid JSON: %1").arg(parseError.errorString()));
    if (!document.isObject())
        fail(QStringLiteral("project must be a JSON object"));
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1StringView(kFormat))
        fail(QStringLiteral("not a String Art Studio project"));
    const int version = integer(root, QStringLiteral("version"), 0);
    if (version < 1 || version > kVersion)
        fail(QStringLiteral("unsupported project version %1").arg(version));

    ProjectData project;
    const QJsonObject frame = requireObject(root, QStringLiteral("frame"));
    project.frame.kind = shapeFromName(frame.value(QStringLiteral("shape")).toString());
    project.frame.pinCount = integer(frame, QStringLiteral("pinCount"), project.frame.pinCount);
    project.frame.widthMm = number(frame, QStringLiteral("widthMm"), project.frame.widthMm);
    project.frame.heightMm = number(frame, QStringLiteral("heightMm"), project.frame.heightMm);
    if (project.frame.kind == PinLayoutKind::Custom) {
        for (const QJsonValue &v : frame.value(QStringLiteral("customPinsMm")).toArray()) {
            const QJsonArray xy = v.toArray();
            if (xy.size() != 2 || !xy[0].isDouble() || !xy[1].isDouble())
                fail(QStringLiteral("customPinsMm entries must be [x, y] numbers"));
            project.frame.customPinsMm.push_back({xy[0].toDouble(), xy[1].toDouble()});
        }
    }
    project.workingSize = integer(root, QStringLiteral("workingSize"), project.workingSize);

    const QJsonObject image = requireObject(root, QStringLiteral("image"));
    project.imagePath = image.value(QStringLiteral("path")).toString();
    project.rotationDegrees = number(image, QStringLiteral("rotationDegrees"), 0.0);
    project.tone.brightness = number(image, QStringLiteral("brightness"), 0.0);
    project.tone.contrast = number(image, QStringLiteral("contrast"), 1.0);
    project.tone.gamma = number(image, QStringLiteral("gamma"), 1.0);
    project.tone.invert = boolean(image, QStringLiteral("invert"), false);
    if (image.contains(QStringLiteral("crop"))) {
        const QJsonArray crop = image.value(QStringLiteral("crop")).toArray();
        if (crop.size() != 4)
            fail(QStringLiteral("crop must be [x, y, width, height]"));
        project.crop = QRectF(crop[0].toDouble(), crop[1].toDouble(), crop[2].toDouble(), crop[3].toDouble());
    }
    if (image.contains(QStringLiteral("data"))) {
        const auto decoded = QByteArray::fromBase64Encoding(image.value(QStringLiteral("data")).toString().toLatin1(),
                                                            QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded)
            fail(QStringLiteral("embedded image data is not valid base64"));
        project.imageData = *decoded;
    }

    const QJsonObject optimization = requireObject(root, QStringLiteral("optimization"));
    OptimizationConfig &c = project.config;
    c.maxSegments = integer(optimization, QStringLiteral("maxSegments"), c.maxSegments);
    c.maxThreadLengthMm = number(optimization, QStringLiteral("maxThreadLengthMm"), c.maxThreadLengthMm);
    c.minPinSeparation = integer(optimization, QStringLiteral("minPinSeparation"), c.minPinSeparation);
    c.threadOpacity = static_cast<float>(number(optimization, QStringLiteral("threadOpacity"), c.threadOpacity));
    c.threadWidth = number(optimization, QStringLiteral("threadWidth"), c.threadWidth);
    c.allowRepeatedLines = boolean(optimization, QStringLiteral("allowRepeatedLines"), c.allowRepeatedLines);
    c.startPin = integer(optimization, QStringLiteral("startPin"), c.startPin);
    c.minImprovement = number(optimization, QStringLiteral("minImprovement"), c.minImprovement);
    project.edgeEmphasis = number(optimization, QStringLiteral("edgeEmphasis"), 0.0);

    const QJsonObject render = root.value(QStringLiteral("render")).toObject();
    project.threadColor = color(render, QStringLiteral("threadColor"), project.threadColor);
    project.backgroundColor = color(render, QStringLiteral("backgroundColor"), project.backgroundColor);

    if (root.contains(QStringLiteral("color"))) {
        const QJsonObject colorMode = root.value(QStringLiteral("color")).toObject();
        project.colorMode = boolean(colorMode, QStringLiteral("enabled"), false);
        project.colorBackground = color(colorMode, QStringLiteral("background"), project.colorBackground);
        for (const QJsonValue &v : colorMode.value(QStringLiteral("palette")).toArray()) {
            const QColor c = QColor::fromString(v.toString());
            if (!c.isValid())
                fail(QStringLiteral("palette contains an invalid colour"));
            project.palette.push_back(c);
        }
        if (project.palette.size() > 8 || (project.colorMode && project.palette.empty()))
            fail(QStringLiteral("the palette must contain 1 to 8 colours"));
        // Files from before layered colours have no method: their results are interleaved.
        const QString method = colorMode.value(QStringLiteral("method")).toString(QStringLiteral("interleaved"));
        if (method != u"layered" && method != u"interleaved")
            fail(QStringLiteral("unknown colour method \"%1\"").arg(method));
        project.layeredColors = method == u"layered";
        const QString order = colorMode.value(QStringLiteral("layerOrder")).toString(QStringLiteral("auto"));
        const auto known = std::find(std::begin(kLayerOrders), std::end(kLayerOrders), order);
        if (known == std::end(kLayerOrders))
            fail(QStringLiteral("unknown layer order \"%1\"").arg(order));
        project.layerOrder = static_cast<int>(known - std::begin(kLayerOrders));
    }

    // Validate the frame and parameters against each other.
    int pinCount = 0;
    try {
        pinCount = PinLayout::fromFrame(project.frame, project.workingSize).pinCount();
        validateConfig(c, pinCount);
    } catch (const std::invalid_argument &e) {
        fail(QStringLiteral("invalid project settings: %1").arg(QString::fromUtf8(e.what())));
    }
    if (!(project.edgeEmphasis >= 0.0))
        fail(QStringLiteral("edgeEmphasis must not be negative"));

    if (root.contains(QStringLiteral("result"))) {
        const QJsonObject result = root.value(QStringLiteral("result")).toObject();
        // Colour steps: [colour, from, to]; every colour must form one continuous
        // thread that starts at the start pin.
        std::vector<int> current(project.palette.size(), project.config.startPin);
        for (const QJsonValue &v : result.value(QStringLiteral("steps")).toArray()) {
            const QJsonArray step = v.toArray();
            if (step.size() != 3)
                fail(QStringLiteral("result steps must be [colour, from, to]"));
            const int c = step[0].toInt(-1);
            const int from = step[1].toInt(-1);
            const int to = step[2].toInt(-1);
            if (c < 0 || c >= static_cast<int>(project.palette.size()) || from < 0 || from >= pinCount || to < 0
                || to >= pinCount)
                fail(QStringLiteral("result steps reference an invalid colour or pin"));
            if (current[static_cast<std::size_t>(c)] != from)
                fail(QStringLiteral("result steps do not form continuous threads"));
            current[static_cast<std::size_t>(c)] = to;
            project.colorSteps.push_back({c, from, to});
        }
        for (const QJsonValue &v : result.value(QStringLiteral("sequence")).toArray()) {
            const double pin = v.toDouble(-1.0);
            if (!v.isDouble() || pin != std::floor(pin) || pin < 0 || pin >= pinCount)
                fail(QStringLiteral("result sequence contains an invalid pin index"));
            project.sequence.push_back(static_cast<int>(pin));
        }
        project.stopReason = result.value(QStringLiteral("stopReason")).toString();
        project.initialError = number(result, QStringLiteral("initialError"), 0.0);
        project.finalError = number(result, QStringLiteral("finalError"), 0.0);
        project.threadLengthMm = number(result, QStringLiteral("threadLengthMm"), 0.0);
        project.elapsedSeconds = number(result, QStringLiteral("elapsedSeconds"), 0.0);
    }
    return project;
}

std::vector<Vec2> ProjectFile::parsePinCsv(const QString &text)
{
    static const QRegularExpression separators(QStringLiteral("[,;\\t ]+"));
    std::vector<Vec2> pins;
    int xColumn = 0;
    int yColumn = 1;
    bool firstDataLine = true;
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("\\r?\\n")));
    for (int lineNumber = 0; lineNumber < lines.size(); ++lineNumber) {
        const QString line = lines[lineNumber].trimmed();
        if (line.isEmpty() || line.startsWith(u'#'))
            continue;
        const QStringList fields = line.split(separators, Qt::SkipEmptyParts);
        bool numeric = true;
        for (const QString &f : fields) {
            bool ok = false;
            f.toDouble(&ok);
            numeric = numeric && ok;
        }
        if (firstDataLine && !numeric) {
            // Header row: look for x / y column names.
            for (int i = 0; i < fields.size(); ++i) {
                const QString name = fields[i].toLower();
                if (name == u"x" || name.startsWith(u"x_") || name == u"xmm")
                    xColumn = i;
                else if (name == u"y" || name.startsWith(u"y_") || name == u"ymm")
                    yColumn = i;
            }
            firstDataLine = false;
            continue;
        }
        firstDataLine = false;
        if (std::max(xColumn, yColumn) >= fields.size())
            fail(QStringLiteral("line %1: expected at least %2 columns").arg(lineNumber + 1).arg(std::max(xColumn, yColumn) + 1));
        bool okX = false;
        bool okY = false;
        const double x = fields[xColumn].toDouble(&okX);
        const double y = fields[yColumn].toDouble(&okY);
        if (!okX || !okY || !std::isfinite(x) || !std::isfinite(y))
            fail(QStringLiteral("line %1: invalid coordinates").arg(lineNumber + 1));
        pins.push_back({x, y});
    }
    if (static_cast<int>(pins.size()) < PinLayout::kMinPins)
        fail(QStringLiteral("at least %1 pins are required, found %2").arg(PinLayout::kMinPins).arg(pins.size()));
    if (static_cast<int>(pins.size()) > PinLayout::kMaxPins)
        fail(QStringLiteral("at most %1 pins are supported").arg(PinLayout::kMaxPins));
    return pins;
}

} // namespace sa
