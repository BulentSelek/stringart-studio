#pragma once

#include "core/ImageProcessor.h"
#include "core/OptimizationConfig.h"
#include "core/PinGeometry.h"
#include "core/StringArtResult.h"

#include <QByteArray>
#include <QColor>
#include <QRectF>
#include <QString>

#include <vector>

namespace sa {

// Everything needed to reproduce a project: input image, preprocessing,
// frame, optimization parameters and the resulting sequence. The rendered
// result is not stored; it is re-rendered from the sequence (deterministic).
struct ProjectData
{
    FrameSpec frame;
    int workingSize = 512;

    QString imagePath;          // original location (informational when embedded)
    QByteArray imageData;       // original encoded file bytes; empty = reference only
    QRectF crop;                // normalized, empty = automatic
    double rotationDegrees = 0.0;
    ToneSettings tone;

    OptimizationConfig config;  // config.threadWidth is in working pixels
    double threadDiameterMm = 0.3; // the same thread in millimetres on the frame (what the UI edits)
    double edgeEmphasis = 0.0;
    QColor threadColor = Qt::black;
    QColor backgroundColor = Qt::white;

    // Multi-colour mode: one continuous thread per palette colour.
    bool colorMode = false;
    std::vector<QColor> palette;
    QColor colorBackground = Qt::white;
    int layerOrder = 0; // layers: 0 auto, 1 light to dark, 2 dark to light, 3 palette order (= sa::LayerOrder)

    std::vector<int> sequence;           // single-colour result; empty = no result
    std::vector<ColorStep> colorSteps;   // multi-colour result in global winding order
    QString stopReason;
    double initialError = 0.0;
    double finalError = 0.0;
    double threadLengthMm = 0.0;
    double elapsedSeconds = 0.0;
};

// JSON (de)serialization. Format "stringart-studio-project", version 1.
//
// The same format serves two purposes:
// - project file (.sasproj): includes the embedded image bytes;
// - JSON export: no image bytes, plus a "pins" array with the coordinates of
//   every pin in millimetres for use by other tools.
// Loading ignores the "pins" array; pins are recomputed from "frame".
class ProjectFile
{
public:
    static constexpr int kVersion = 1;

    [[nodiscard]] static QByteArray toJson(const ProjectData &project, bool embedImage, bool includePins);

    // Throws std::runtime_error with a readable message on invalid input,
    // including sequences that reference pins outside the frame.
    [[nodiscard]] static ProjectData fromJson(const QByteArray &json);

    // Parses custom pin coordinates (millimetres, perimeter order) from CSV.
    // Separators: comma, semicolon, tab or spaces. An optional header row may
    // name the columns (x / x_mm and y / y_mm); otherwise the first two
    // numeric columns are used. Empty lines and lines starting with '#' are
    // skipped. Throws std::runtime_error on malformed input.
    [[nodiscard]] static std::vector<Vec2> parsePinCsv(const QString &text);

    [[nodiscard]] static QString shapeName(PinLayoutKind kind);
};

} // namespace sa
