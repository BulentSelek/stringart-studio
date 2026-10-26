#pragma once

#include "core/PinGeometry.h"

#include <QImage>
#include <QString>

#include <span>

namespace sa {

enum class LengthUnit { Pixels, Millimeters };

class ExportManager
{
public:
    // CSV winding sequence, one row per segment:
    //   segment,from_pin,to_pin,from_x,from_y,to_x,to_y,length,cumulative_length
    // Column suffixes give the unit (_px or _mm). Pins are 0-based, pin 0 at
    // the top, increasing clockwise. Coordinates are relative to the canvas
    // top-left corner, y pointing down.
    [[nodiscard]] static QString csvText(const PinLayout &layout, std::span<const int> sequence, LengthUnit unit);

    static bool writeCsv(const QString &path, const PinLayout &layout, std::span<const int> sequence,
                         LengthUnit unit, QString *errorMessage = nullptr);

    static bool writePng(const QString &path, const QImage &image, QString *errorMessage = nullptr);
};

} // namespace sa
