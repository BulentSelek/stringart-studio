#pragma once

#include "core/PinGeometry.h"
#include "core/StringArtResult.h"
#include "core/ThreadRenderer.h"

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

    // SVG in millimetres (viewBox = physical frame size): background, one
    // <line> per segment and optionally the pins. Each segment is a separate
    // element with stroke-opacity = thread opacity, so overlaps compound
    // multiplicatively as in the compositing model; viewers composite in
    // sRGB rather than linear light, so mid-tones look slightly darker than
    // the PNG render.
    [[nodiscard]] static QString svgText(const PinLayout &layout, std::span<const int> sequence,
                                         const RenderStyle &style, bool includePins = true);

    static bool writeSvg(const QString &path, const PinLayout &layout, std::span<const int> sequence,
                         const RenderStyle &style, bool includePins = true, QString *errorMessage = nullptr);

    // Multi-colour CSV in global winding order:
    //   step,color,color_hex,from_pin,to_pin,from_x,from_y,to_x,to_y,length,color_cumulative_length
    // `color` is the palette index; each colour's rows form one continuous thread.
    [[nodiscard]] static QString colorCsvText(const PinLayout &layout, std::span<const ColorStep> steps,
                                              std::span<const QColor> palette, LengthUnit unit);

    // Multi-colour SVG: one <line> per step in winding order, coloured per thread.
    [[nodiscard]] static QString colorSvgText(const PinLayout &layout, std::span<const ColorStep> steps,
                                              std::span<const QColor> palette, const QColor &background,
                                              double threadWidth, float opacity, bool includePins = true);

    // Writes text atomically (QSaveFile).
    static bool writeText(const QString &path, const QByteArray &data, QString *errorMessage = nullptr);

    static bool writePng(const QString &path, const QImage &image, QString *errorMessage = nullptr);
};

} // namespace sa
