#pragma once

#include "core/PinGeometry.h"
#include "core/StringArtResult.h"

#include <QColor>
#include <QRectF>
#include <QSizeF>
#include <QString>

#include <span>
#include <vector>

namespace sa {

// Thread needed for one colour of a result (one continuous thread).
struct ThreadUsage
{
    QColor color;
    int segments = 0;
    double lengthMm = 0.0; // pin-to-pin length; knots and wraps around pins are not included
};

// Single-colour sequence as steps of colour 0, so both result kinds share
// the workshop documents.
[[nodiscard]] std::vector<ColorStep> monochromeSteps(std::span<const int> sequence);

// Per palette entry, in palette order (unused colours have zero segments).
[[nodiscard]] std::vector<ThreadUsage> threadUsage(const PinLayout &layout, std::span<const ColorStep> steps,
                                                   std::span<const QColor> palette);

enum class TemplatePaper { A4, A3, Letter, SingleSheet };

struct TemplateOptions
{
    TemplatePaper paper = TemplatePaper::A4;
    double marginMm = 8.0;   // unprinted border of each sheet
    double overlapMm = 15.0; // shared strip between neighbouring tiles
    QString title;
};

// How a 1:1 drawing of `contentMm` (frame coordinates in mm) is split into
// printed sheets. Neighbouring tiles share `overlap` mm; the grid is centred
// on the content.
struct TilePlan
{
    QSizeF pageMm;      // sheet size, orientation included
    double marginMm = 0.0;
    double headerMm = 0.0; // strip above the drawing for the sheet name and map
    double overlapMm = 0.0;
    QPointF originMm;   // frame coordinate printed at the top-left of tile (0, 0)
    int rows = 1;
    int columns = 1;

    [[nodiscard]] QSizeF printableMm() const
    {
        return {pageMm.width() - 2.0 * marginMm, pageMm.height() - 2.0 * marginMm - headerMm};
    }
    // Frame-coordinate rectangle printed on tile (row, column).
    [[nodiscard]] QRectF tileRect(int row, int column) const;
    [[nodiscard]] int pageCount() const { return rows * columns; }
};

// Tiles for `contentMm`; picks the orientation that needs fewer sheets
// (portrait on ties). SingleSheet gives one page of the content size plus margins.
[[nodiscard]] TilePlan planTiles(const QRectF &contentMm, const TemplateOptions &options);

// Smallest label interval in {1, 2, 5, 10, 20, 25, 50, 100, ...} so that
// labels of height `labelHeightMm`, written radially, do not overlap for
// pins `minSpacingMm` apart.
[[nodiscard]] int pinLabelStep(double minSpacingMm, double labelHeightMm);

// Printable documents for building the piece by hand.
class WorkshopDocuments
{
public:
    // Short human-readable frame summary, e.g. "circular frame, 500 mm diameter, 240 pins".
    [[nodiscard]] static QString frameDescription(const FrameSpec &frame);

    // Frame region that the pin template draws (pins plus label space), in mm.
    [[nodiscard]] static QRectF templateContentMm(const PinLayout &layout);

    // 1:1 drilling template: frame outline, a crosshair per pin, pin numbers
    // (clockwise from pin 0), a 100 mm scale bar to check the print scale,
    // and for tiled output glue lines, registration marks and a sheet map.
    static bool writePinTemplatePdf(const QString &path, const PinLayout &layout, const TemplateOptions &options,
                                    QString *errorMessage = nullptr);

    struct GuideInfo
    {
        QString title;
        QString frameDescription; // e.g. "circular frame, 500 mm, 240 pins"
        std::vector<QColor> palette;
    };

    // A4 winding guide: summary with thread lengths, then every step in
    // order with a tick box, grouped in tens. Colour changes are marked and
    // multi-colour rows show the source pin, since each colour continues
    // from where its own thread stopped.
    static bool writeWindingGuidePdf(const QString &path, const PinLayout &layout, std::span<const ColorStep> steps,
                                     const GuideInfo &info, QString *errorMessage = nullptr);
};

} // namespace sa
