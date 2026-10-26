#include "core/ExportManager.h"

#include <QSaveFile>

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

bool ExportManager::writeCsv(const QString &path, const PinLayout &layout, std::span<const int> sequence,
                             LengthUnit unit, QString *errorMessage)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage)
            *errorMessage = file.errorString();
        return false;
    }
    file.write(csvText(layout, sequence, unit).toUtf8());
    if (!file.commit()) {
        if (errorMessage)
            *errorMessage = file.errorString();
        return false;
    }
    return true;
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
