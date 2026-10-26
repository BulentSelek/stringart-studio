#include "core/ExportManager.h"

#include <QSaveFile>

#include <algorithm>

namespace sa {

QString ExportManager::csvText(const PinLayout &layout, std::span<const int> sequence, LengthUnit unit)
{
    const bool mm = unit == LengthUnit::Millimeters;
    const QString suffix = mm ? QStringLiteral("_mm") : QStringLiteral("_px");
    const double factor = mm ? layout.mmPerUnit() : 1.0;
    const int precision = mm ? 2 : 3;
    auto number = [precision](double v) { return QString::number(v, 'f', precision); };

    QString text;
    text += QStringLiteral("segment,from_pin,to_pin,from_x%1,from_y%1,to_x%1,to_y%1,length%1,cumulative_length%1\n")
                .arg(suffix);
    double cumulative = 0.0;
    for (std::size_t i = 0; i + 1 < sequence.size(); ++i) {
        const int from = sequence[i];
        const int to = sequence[i + 1];
        const Vec2 &p = layout.pin(from);
        const Vec2 &q = layout.pin(to);
        const double length = layout.distance(from, to) * factor;
        cumulative += length;
        text += QString::number(i + 1) + u',' + QString::number(from) + u',' + QString::number(to) + u','
                + number(p.x * factor) + u',' + number(p.y * factor) + u',' + number(q.x * factor) + u','
                + number(q.y * factor) + u',' + number(length) + u',' + number(cumulative) + u'\n';
    }
    return text;
}

bool ExportManager::writeText(const QString &path, const QByteArray &data, QString *errorMessage)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        if (errorMessage)
            *errorMessage = file.errorString();
        return false;
    }
    return true;
}

bool ExportManager::writeCsv(const QString &path, const PinLayout &layout, std::span<const int> sequence,
                             LengthUnit unit, QString *errorMessage)
{
    return writeText(path, csvText(layout, sequence, unit).toUtf8(), errorMessage);
}

QString ExportManager::svgText(const PinLayout &layout, std::span<const int> sequence, const RenderStyle &style,
                               bool includePins)
{
    const double mm = layout.mmPerUnit();
    auto n = [](double v) { return QString::number(v, 'f', 3); };
    const double width = layout.canvasWidth() * mm;
    const double height = layout.canvasHeight() * mm;

    QString svg;
    svg += QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    svg += QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%1mm\" height=\"%2mm\" "
                          "viewBox=\"0 0 %1 %2\">\n")
               .arg(n(width), n(height));
    svg += QStringLiteral("<rect width=\"%1\" height=\"%2\" fill=\"%3\"/>\n")
               .arg(n(width), n(height), style.background.name());
    svg += QStringLiteral("<g id=\"thread\" stroke=\"%1\" stroke-opacity=\"%2\" stroke-width=\"%3\" "
                          "stroke-linecap=\"butt\">\n")
               .arg(style.thread.name(), QString::number(style.opacity, 'f', 4), n(style.threadWidth * mm));
    for (std::size_t i = 0; i + 1 < sequence.size(); ++i) {
        const Vec2 &p = layout.pin(sequence[i]);
        const Vec2 &q = layout.pin(sequence[i + 1]);
        svg += QStringLiteral("<line x1=\"%1\" y1=\"%2\" x2=\"%3\" y2=\"%4\"/>\n")
                   .arg(n(p.x * mm), n(p.y * mm), n(q.x * mm), n(q.y * mm));
    }
    svg += QStringLiteral("</g>\n");
    if (includePins) {
        double spacing = layout.distance(0, 1);
        for (int i = 1; i < layout.pinCount(); ++i)
            spacing = std::min(spacing, layout.distance(i, (i + 1) % layout.pinCount()));
        const double radius = std::max(0.1, spacing * mm * 0.25);
        svg += QStringLiteral("<g id=\"pins\" fill=\"#c62828\">\n");
        for (int i = 0; i < layout.pinCount(); ++i) {
            const Vec2 &p = layout.pin(i);
            svg += QStringLiteral("<circle id=\"pin%1\" cx=\"%2\" cy=\"%3\" r=\"%4\"/>\n")
                       .arg(i)
                       .arg(n(p.x * mm), n(p.y * mm), n(radius));
        }
        svg += QStringLiteral("</g>\n");
    }
    svg += QStringLiteral("</svg>\n");
    return svg;
}

bool ExportManager::writeSvg(const QString &path, const PinLayout &layout, std::span<const int> sequence,
                             const RenderStyle &style, bool includePins, QString *errorMessage)
{
    return writeText(path, svgText(layout, sequence, style, includePins).toUtf8(), errorMessage);
}

bool ExportManager::writePng(const QString &path, const QImage &image, QString *errorMessage)
{
    if (image.isNull()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("no image to export");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG") || !file.commit()) {
        if (errorMessage)
            *errorMessage = file.errorString().isEmpty() ? QStringLiteral("PNG encoding failed") : file.errorString();
        return false;
    }
    return true;
}

} // namespace sa
