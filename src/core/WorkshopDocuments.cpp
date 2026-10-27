#include "core/WorkshopDocuments.h"

#include <QFileInfo>
#include <QFontMetricsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace sa {

namespace {

constexpr double kFontPixels = 100.0; // text is laid out at this pixel size and scaled to millimetres
constexpr double kLabelHeightMm = 2.4;
constexpr double kLabelSpaceMm = 11.0; // room outside the pins for their numbers
constexpr int kPdfResolution = 600;

QPointF toMm(const PinLayout &layout, const Vec2 &p)
{
    return {p.x * layout.mmPerUnit(), p.y * layout.mmPerUnit()};
}

QRectF pinBoundsMm(const PinLayout &layout)
{
    double left = std::numeric_limits<double>::max();
    double top = left;
    double right = std::numeric_limits<double>::lowest();
    double bottom = right;
    for (const Vec2 &v : layout.pins()) {
        const QPointF p = toMm(layout, v);
        left = std::min(left, p.x());
        right = std::max(right, p.x());
        top = std::min(top, p.y());
        bottom = std::max(bottom, p.y());
    }
    return QRectF(QPointF(left, top), QPointF(right, bottom));
}

QPointF pinCentroidMm(const PinLayout &layout)
{
    QPointF sum;
    for (const Vec2 &v : layout.pins())
        sum += toMm(layout, v);
    return sum / std::max(1, layout.pinCount());
}

double minNeighbourSpacingMm(const PinLayout &layout)
{
    double spacing = std::numeric_limits<double>::max();
    const int n = layout.pinCount();
    for (int i = 0; i < n; ++i)
        spacing = std::min(spacing, layout.distance(i, (i + 1) % n) * layout.mmPerUnit());
    return spacing;
}

QFont scaledFont(bool bold)
{
    QFont font(QStringLiteral("Sans Serif"));
    font.setStyleHint(QFont::SansSerif);
    font.setPixelSize(static_cast<int>(kFontPixels));
    font.setBold(bold);
    return font;
}

// Draws `text` with an em height of `heightMm` at `pos` (painter units are
// millimetres), rotated by `angle` degrees about `pos`.
void drawText(QPainter &painter, QPointF pos, double heightMm, const QString &text, Qt::Alignment align,
              const QColor &color = Qt::black, bool bold = false, double angle = 0.0)
{
    const QFont font = scaledFont(bold);
    const QFontMetricsF metrics(font);
    double x = 0.0;
    if (align & Qt::AlignRight)
        x = -metrics.horizontalAdvance(text);
    else if (align & Qt::AlignHCenter)
        x = -metrics.horizontalAdvance(text) / 2.0;
    double y = 0.0; // baseline
    if (align & Qt::AlignVCenter)
        y = (metrics.ascent() - metrics.descent()) / 2.0;
    else if (align & Qt::AlignTop)
        y = metrics.ascent();
    else if (align & Qt::AlignBottom)
        y = -metrics.descent();
    painter.save();
    painter.translate(pos);
    painter.rotate(angle);
    painter.scale(heightMm / kFontPixels, heightMm / kFontPixels);
    painter.setFont(font);
    painter.setPen(color);
    painter.drawText(QPointF(x, y), text);
    painter.restore();
}

double textWidthMm(const QString &text, double heightMm, bool bold = false)
{
    return QFontMetricsF(scaledFont(bold)).horizontalAdvance(text) * heightMm / kFontPixels;
}

QPen mmPen(const QColor &color, double widthMm, Qt::PenStyle style = Qt::SolidLine)
{
    QPen pen(color, widthMm, style, Qt::FlatCap, Qt::MiterJoin);
    return pen;
}

bool startPdf(QPdfWriter &writer, const QSizeF &pageMm, const QString &title)
{
    writer.setResolution(kPdfResolution);
    writer.setTitle(title);
    writer.setCreator(QStringLiteral("String Art Studio"));
    const QPageLayout::Orientation orientation =
        pageMm.width() > pageMm.height() ? QPageLayout::Landscape : QPageLayout::Portrait;
    // QPageSize is always portrait; the orientation swaps the sides back.
    const QPageSize portrait(QSizeF(std::min(pageMm.width(), pageMm.height()),
                                    std::max(pageMm.width(), pageMm.height())),
                             QPageSize::Millimeter, QString(), QPageSize::ExactMatch);
    return writer.setPageLayout(
        QPageLayout(portrait, orientation, QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter));
}

// Painter units become millimetres from the top-left corner of the sheet.
void resetToMm(QPainter &painter)
{
    painter.resetTransform();
    const double dotsPerMm = kPdfResolution / 25.4;
    painter.scale(dotsPerMm, dotsPerMm);
}

QSizeF paperSizeMm(TemplatePaper paper)
{
    switch (paper) {
    case TemplatePaper::A3:
        return {297.0, 420.0};
    case TemplatePaper::Letter:
        return {215.9, 279.4};
    case TemplatePaper::A4:
    case TemplatePaper::SingleSheet:
        break;
    }
    return {210.0, 297.0};
}

TilePlan planFor(const QRectF &content, QSizeF page, const TemplateOptions &options)
{
    TilePlan plan;
    plan.pageMm = page;
    plan.marginMm = options.marginMm;
    plan.headerMm = 11.0;
    plan.overlapMm = options.overlapMm;
    const QSizeF printable = plan.printableMm();
    const double stepX = printable.width() - plan.overlapMm;
    const double stepY = printable.height() - plan.overlapMm;
    plan.columns = std::max(1, static_cast<int>(std::ceil((content.width() - plan.overlapMm) / stepX - 1e-9)));
    plan.rows = std::max(1, static_cast<int>(std::ceil((content.height() - plan.overlapMm) / stepY - 1e-9)));
    const double gridWidth = (plan.columns - 1) * stepX + printable.width();
    const double gridHeight = (plan.rows - 1) * stepY + printable.height();
    plan.originMm = QPointF(content.center().x() - gridWidth / 2.0, content.center().y() - gridHeight / 2.0);
    return plan;
}

QString sheetName(int row, int column)
{
    // Rows are letters (A, B, ...), columns numbers, as on a map.
    QString letters;
    for (int r = row + 1; r > 0; r = (r - 1) / 26)
        letters.prepend(QChar(u'A' + (r - 1) % 26));
    return letters + QString::number(column + 1);
}

void drawRegistrationMark(QPainter &painter, QPointF p)
{
    painter.setPen(mmPen(Qt::black, 0.2));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(p, 2.0, 2.0);
    painter.drawLine(QPointF(p.x() - 3.5, p.y()), QPointF(p.x() + 3.5, p.y()));
    painter.drawLine(QPointF(p.x(), p.y() - 3.5), QPointF(p.x(), p.y() + 3.5));
}

// Everything drawn in frame coordinates (mm), identical on every sheet.
void drawTemplate(QPainter &painter, const PinLayout &layout, const QString &title, const TilePlan *tiles,
                  const QRectF &content)
{
    const int n = layout.pinCount();
    const QPointF centroid = pinCentroidMm(layout);
    const QRectF bounds = pinBoundsMm(layout);

    // Frame outline through the pins.
    QPolygonF outline;
    for (const Vec2 &v : layout.pins())
        outline << toMm(layout, v);
    painter.setPen(mmPen(QColor(150, 150, 150), 0.25));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(outline);

    // Centre mark, title and scale bar.
    const QPointF centre = bounds.center();
    painter.setPen(mmPen(QColor(120, 120, 120), 0.2));
    painter.drawLine(QPointF(centre.x() - 6, centre.y()), QPointF(centre.x() + 6, centre.y()));
    painter.drawLine(QPointF(centre.x(), centre.y() - 6), QPointF(centre.x(), centre.y() + 6));
    const double barLength = bounds.width() >= 160.0 ? 100.0 : 50.0;
    if (bounds.width() >= barLength + 20.0 && bounds.height() >= 50.0) {
        drawText(painter, QPointF(centre.x(), centre.y() - 12), 5.0, title, Qt::AlignHCenter | Qt::AlignBottom,
                 Qt::black, true);
        drawText(painter, QPointF(centre.x(), centre.y() - 8), 3.0,
                 QStringLiteral("%1 pins, numbered clockwise from 0").arg(n), Qt::AlignHCenter | Qt::AlignBottom,
                 QColor(70, 70, 70));
        const double y = centre.y() + 14.0;
        const double x0 = centre.x() - barLength / 2.0;
        painter.setPen(mmPen(Qt::black, 0.3));
        painter.drawLine(QPointF(x0, y), QPointF(x0 + barLength, y));
        for (int k = 0; k <= static_cast<int>(barLength) / 10; ++k) {
            const double tick = (k % 5 == 0) ? 3.0 : 1.5;
            painter.drawLine(QPointF(x0 + 10.0 * k, y - tick), QPointF(x0 + 10.0 * k, y));
        }
        drawText(painter, QPointF(centre.x(), y + 2.0), 3.0,
                 QStringLiteral("%1 mm: measure to check the print scale (100%, no fit-to-page)")
                     .arg(barLength, 0, 'f', 0),
                 Qt::AlignHCenter | Qt::AlignTop, QColor(70, 70, 70));
    }

    // Pins: drill crosshair, ring and radial number.
    const int step = pinLabelStep(minNeighbourSpacingMm(layout), kLabelHeightMm);
    for (int i = 0; i < n; ++i) {
        const QPointF p = toMm(layout, layout.pin(i));
        QPointF d = p - centroid;
        const double len = std::hypot(d.x(), d.y());
        d = len > 0.0 ? d / len : QPointF(0.0, -1.0);
        const bool labelled = i % step == 0;
        const bool major = i % (step * 10) == 0;
        painter.setPen(mmPen(i == 0 ? QColor(198, 40, 40) : QColor(0, 0, 0), i == 0 ? 0.3 : 0.15));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(p, 0.8, 0.8);
        painter.drawLine(QPointF(p.x() - 1.6, p.y()), QPointF(p.x() + 1.6, p.y()));
        painter.drawLine(QPointF(p.x(), p.y() - 1.6), QPointF(p.x(), p.y() + 1.6));
        const double tick = labelled ? (major ? 3.0 : 2.2) : 1.4;
        painter.drawLine(p + d * 1.2, p + d * (1.2 + tick));
        if (!labelled)
            continue;
        double angle = std::atan2(d.y(), d.x()) * 180.0 / std::numbers::pi;
        const QPointF anchor = p + d * (1.2 + tick + 0.8);
        const bool flip = angle > 90.0 || angle < -90.0; // keep the text upright
        if (flip)
            angle += 180.0;
        drawText(painter, anchor, kLabelHeightMm * (major || i == 0 ? 1.15 : 1.0), QString::number(i),
                 (flip ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter,
                 i == 0 ? QColor(198, 40, 40) : Qt::black, major || i == 0, angle);
    }

    if (!tiles)
        return;
    // Glue lines in the middle of each overlap strip with registration marks
    // on them; both neighbouring sheets print them at the same frame position.
    const QSizeF printable = tiles->printableMm();
    const double stepX = printable.width() - tiles->overlapMm;
    const double stepY = printable.height() - tiles->overlapMm;
    const double gridRight = tiles->originMm.x() + (tiles->columns - 1) * stepX + printable.width();
    const double gridBottom = tiles->originMm.y() + (tiles->rows - 1) * stepY + printable.height();
    painter.setPen(mmPen(QColor(0, 110, 200), 0.2, Qt::DashLine));
    std::vector<double> xs;
    std::vector<double> ys;
    for (int c = 1; c < tiles->columns; ++c) {
        const double x = tiles->originMm.x() + c * stepX + tiles->overlapMm / 2.0;
        xs.push_back(x);
        painter.drawLine(QPointF(x, tiles->originMm.y()), QPointF(x, gridBottom));
    }
    for (int r = 1; r < tiles->rows; ++r) {
        const double y = tiles->originMm.y() + r * stepY + tiles->overlapMm / 2.0;
        ys.push_back(y);
        painter.drawLine(QPointF(tiles->originMm.x(), y), QPointF(gridRight, y));
    }
    // Marks keep clear of pins so they are never mistaken for drill points.
    auto mark = [&](QPointF p) {
        for (const Vec2 &v : layout.pins()) {
            const QPointF d = toMm(layout, v) - p;
            if (d.x() * d.x() + d.y() * d.y() < 8.0 * 8.0)
                return;
        }
        drawRegistrationMark(painter, p);
    };
    for (double x : xs) {
        for (double y = content.top() + 10.0; y < content.bottom(); y += 30.0)
            mark(QPointF(x, y));
    }
    for (double y : ys) {
        for (double x = content.left() + 10.0; x < content.right(); x += 30.0)
            mark(QPointF(x, y));
    }
}

void drawSheetLabel(QPainter &painter, const TilePlan &plan, int row, int column)
{
    // In the header strip above the drawing, in sheet millimetres.
    const double cell = 2.6;
    const QPointF origin(plan.marginMm, plan.marginMm);
    const QString name = QStringLiteral("Sheet %1  (%2 of %3)")
                             .arg(sheetName(row, column))
                             .arg(row * plan.columns + column + 1)
                             .arg(plan.pageCount());
    drawText(painter, origin, 3.4, name, Qt::AlignLeft | Qt::AlignTop, Qt::black, true);
    drawText(painter, origin + QPointF(0.0, 4.8), 2.5,
             QStringLiteral("Print at 100%. Trim one sheet at the dashed line, align the round marks, tape."),
             Qt::AlignLeft | Qt::AlignTop, QColor(70, 70, 70));
    const double mapLeft = plan.pageMm.width() - plan.marginMm - plan.columns * cell;
    painter.setPen(mmPen(QColor(120, 120, 120), 0.15));
    for (int r = 0; r < plan.rows; ++r) {
        for (int c = 0; c < plan.columns; ++c) {
            painter.setBrush(r == row && c == column ? QColor(0, 110, 200) : QColor(235, 235, 235));
            painter.drawRect(QRectF(QPointF(mapLeft + c * cell, origin.y() + r * cell), QSizeF(cell, cell)));
        }
    }
}

bool finish(QPainter &painter, const QString &path, QString *errorMessage)
{
    if (!painter.end() || !QFileInfo(path).isFile()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("cannot write %1").arg(path);
        return false;
    }
    return true;
}

} // namespace

std::vector<ColorStep> monochromeSteps(std::span<const int> sequence)
{
    std::vector<ColorStep> steps;
    for (std::size_t i = 0; i + 1 < sequence.size(); ++i)
        steps.push_back({0, sequence[i], sequence[i + 1]});
    return steps;
}

std::vector<ThreadUsage> threadUsage(const PinLayout &layout, std::span<const ColorStep> steps,
                                     std::span<const QColor> palette)
{
    std::vector<ThreadUsage> usage(palette.size());
    for (std::size_t c = 0; c < palette.size(); ++c)
        usage[c].color = palette[c];
    for (const ColorStep &s : steps) {
        ThreadUsage &u = usage.at(static_cast<std::size_t>(s.color));
        ++u.segments;
        u.lengthMm += layout.distance(s.from, s.to) * layout.mmPerUnit();
    }
    return usage;
}

QRectF TilePlan::tileRect(int row, int column) const
{
    const QSizeF printable = printableMm();
    return {originMm.x() + column * (printable.width() - overlapMm),
            originMm.y() + row * (printable.height() - overlapMm), printable.width(), printable.height()};
}

TilePlan planTiles(const QRectF &contentMm, const TemplateOptions &options)
{
    if (options.paper == TemplatePaper::SingleSheet) {
        TilePlan plan;
        plan.marginMm = options.marginMm;
        plan.overlapMm = 0.0;
        plan.pageMm = QSizeF(contentMm.width() + 2.0 * options.marginMm, contentMm.height() + 2.0 * options.marginMm);
        plan.originMm = contentMm.topLeft();
        return plan;
    }
    const QSizeF paper = paperSizeMm(options.paper);
    const TilePlan portrait = planFor(contentMm, paper, options);
    const TilePlan landscape = planFor(contentMm, paper.transposed(), options);
    return landscape.pageCount() < portrait.pageCount() ? landscape : portrait;
}

int pinLabelStep(double minSpacingMm, double labelHeightMm)
{
    const double needed = labelHeightMm * 1.3;
    for (int decade = 1; decade < 100000; decade *= 10) {
        for (int m : {10, 20, 25, 50}) {
            const int step = std::max(1, m * decade / 10);
            if (minSpacingMm * step >= needed)
                return step;
        }
    }
    return 100000;
}

QString WorkshopDocuments::frameDescription(const FrameSpec &frame)
{
    auto mm = [](double v) { return QString::number(v, 'f', 0); };
    switch (frame.kind) {
    case PinLayoutKind::Circular:
        return QStringLiteral("circular frame, %1 mm diameter, %2 pins").arg(mm(frame.widthMm)).arg(frame.pinCount);
    case PinLayoutKind::Elliptical:
        return QStringLiteral("elliptical frame, %1 x %2 mm, %3 pins")
            .arg(mm(frame.widthMm), mm(frame.heightMm))
            .arg(frame.pinCount);
    case PinLayoutKind::Rectangular:
        return QStringLiteral("rectangular frame, %1 x %2 mm, %3 pins")
            .arg(mm(frame.widthMm), mm(frame.heightMm))
            .arg(frame.pinCount);
    case PinLayoutKind::Custom:
        break;
    }
    return QStringLiteral("custom frame, %1 pins").arg(frame.customPinsMm.size());
}

QRectF WorkshopDocuments::templateContentMm(const PinLayout &layout)
{
    return pinBoundsMm(layout).adjusted(-kLabelSpaceMm, -kLabelSpaceMm, kLabelSpaceMm, kLabelSpaceMm);
}

bool WorkshopDocuments::writePinTemplatePdf(const QString &path, const PinLayout &layout,
                                            const TemplateOptions &options, QString *errorMessage)
{
    const QRectF content = templateContentMm(layout);
    const TilePlan plan = planTiles(content, options);
    const bool tiled = options.paper != TemplatePaper::SingleSheet;
    const QString title = options.title.isEmpty() ? QStringLiteral("Pin template") : options.title;

    QPdfWriter writer(path);
    if (!startPdf(writer, plan.pageMm, title)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("invalid page size");
        return false;
    }
    QPainter painter;
    if (!painter.begin(&writer)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("cannot write %1").arg(path);
        return false;
    }
    painter.setRenderHint(QPainter::Antialiasing);
    for (int row = 0; row < plan.rows; ++row) {
        for (int column = 0; column < plan.columns; ++column) {
            if (row > 0 || column > 0)
                writer.newPage();
            resetToMm(painter);
            const QRectF tile = plan.tileRect(row, column);
            painter.save();
            painter.translate(plan.marginMm, plan.marginMm + plan.headerMm);
            painter.setClipRect(QRectF(QPointF(0, 0), plan.printableMm()));
            painter.translate(-tile.topLeft());
            drawTemplate(painter, layout, title, tiled ? &plan : nullptr, content);
            painter.restore();
            if (tiled)
                drawSheetLabel(painter, plan, row, column);
        }
    }
    return finish(painter, path, errorMessage);
}

bool WorkshopDocuments::writeWindingGuidePdf(const QString &path, const PinLayout &layout,
                                             std::span<const ColorStep> steps, const GuideInfo &info,
                                             QString *errorMessage)
{
    const bool multiColour = info.palette.size() > 1;
    const std::vector<ThreadUsage> usage = threadUsage(layout, steps, info.palette);
    int usedColours = 0;
    double totalMm = 0.0;
    for (const ThreadUsage &u : usage) {
        usedColours += u.segments > 0 ? 1 : 0;
        totalMm += u.lengthMm;
    }

    const QSizeF page(210.0, 297.0);
    const double margin = 12.0;
    const double contentWidth = page.width() - 2.0 * margin;
    const double rowHeight = 5.0;
    const double footerTop = page.height() - margin - 5.0;
    const int columns = multiColour ? 4 : 5;
    const double columnWidth = contentWidth / columns;
    const double headerHeight = 34.0 + 6.0 * usedColours;
    const int rowsFirst = std::max(1, static_cast<int>((footerTop - margin - headerHeight) / rowHeight));
    const int rowsOther = static_cast<int>((footerTop - margin - 8.0) / rowHeight);
    const int count = static_cast<int>(steps.size());
    const int firstPageSteps = rowsFirst * columns;
    const int otherPageSteps = rowsOther * columns;
    const int pages =
        1 + (count > firstPageSteps ? (count - firstPageSteps + otherPageSteps - 1) / otherPageSteps : 0);

    const QString title = info.title.isEmpty() ? QStringLiteral("Winding guide") : info.title;
    QPdfWriter writer(path);
    if (!startPdf(writer, page, title)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("invalid page size");
        return false;
    }
    QPainter painter;
    if (!painter.begin(&writer)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("cannot write %1").arg(path);
        return false;
    }
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor grey(100, 100, 100);

    int next = 0;
    for (int pageIndex = 0; pageIndex < pages; ++pageIndex) {
        if (pageIndex > 0)
            writer.newPage();
        resetToMm(painter);
        double top = margin;
        if (pageIndex == 0) {
            drawText(painter, QPointF(margin, top), 7.0, title, Qt::AlignLeft | Qt::AlignTop, Qt::black, true);
            top += 9.0;
            drawText(painter, QPointF(margin, top), 3.4, info.frameDescription, Qt::AlignLeft | Qt::AlignTop, grey);
            top += 5.0;
            drawText(painter, QPointF(margin, top), 3.4,
                     QStringLiteral("%1 steps, %2 m of thread in total. Pin numbers match the pin template "
                                    "(clockwise from pin 0).")
                         .arg(count)
                         .arg(totalMm / 1000.0, 0, 'f', 1),
                     Qt::AlignLeft | Qt::AlignTop, grey);
            top += 8.0;
            const QStringList headers{QStringLiteral("Thread"), QStringLiteral("Colour"), QStringLiteral("Starts at"),
                                      QStringLiteral("Segments"), QStringLiteral("Length"),
                                      QStringLiteral("With 10% reserve")};
            const double xs[] = {margin, margin + 22, margin + 52, margin + 78, margin + 102, margin + 126};
            for (int h = 0; h < headers.size(); ++h)
                drawText(painter, QPointF(xs[h], top), 3.0, headers[h], Qt::AlignLeft | Qt::AlignTop, grey, true);
            top += 5.0;
            for (std::size_t c = 0; c < usage.size(); ++c) {
                if (usage[c].segments == 0)
                    continue;
                const auto first = std::find_if(steps.begin(), steps.end(),
                                                [c](const ColorStep &s) { return s.color == static_cast<int>(c); });
                painter.setPen(mmPen(grey, 0.15));
                painter.setBrush(usage[c].color);
                painter.drawRect(QRectF(xs[1], top + 0.3, 4.0, 4.0));
                const QStringList cells{QString::number(c + 1), usage[c].color.name(),
                                        QStringLiteral("pin %1").arg(first->from), QString::number(usage[c].segments),
                                        QStringLiteral("%1 m").arg(usage[c].lengthMm / 1000.0, 0, 'f', 1),
                                        QStringLiteral("%1 m").arg(usage[c].lengthMm * 1.1 / 1000.0, 0, 'f', 1)};
                for (int k = 0; k < cells.size(); ++k) {
                    const double x = k == 1 ? xs[1] + 6.0 : xs[k];
                    drawText(painter, QPointF(x, top + 2.3), 3.2, cells[k], Qt::AlignLeft | Qt::AlignVCenter);
                }
                top += 6.0;
            }
            top = margin + headerHeight;
        } else {
            drawText(painter, QPointF(margin, top), 3.4, title, Qt::AlignLeft | Qt::AlignTop, grey, true);
            top += 8.0;
        }
        painter.setPen(mmPen(QColor(180, 180, 180), 0.2));
        painter.drawLine(QPointF(margin, top - 1.5), QPointF(page.width() - margin, top - 1.5));

        const int rows = pageIndex == 0 ? rowsFirst : rowsOther;
        const int firstOnPage = next;
        for (int column = 0; column < columns && next < count; ++column) {
            const double x = margin + column * columnWidth;
            for (int row = 0; row < rows && next < count; ++row, ++next) {
                const ColorStep &s = steps[static_cast<std::size_t>(next)];
                const double y = top + row * rowHeight;
                const double mid = y + rowHeight / 2.0;
                const bool colourChange = multiColour && next > 0 && steps[static_cast<std::size_t>(next) - 1].color != s.color;
                if (next % 10 == 0 && row > 0) {
                    painter.setPen(mmPen(QColor(200, 200, 200), 0.2));
                    painter.drawLine(QPointF(x, y), QPointF(x + columnWidth - 3.0, y));
                }
                if (colourChange) {
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(QColor(255, 236, 179));
                    painter.drawRect(QRectF(x, y + 0.3, columnWidth - 3.0, rowHeight - 0.6));
                }
                painter.setPen(mmPen(grey, 0.2));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(QRectF(x + 0.5, mid - 1.5, 3.0, 3.0));
                drawText(painter, QPointF(x + 14.0, mid), 2.7, QString::number(next + 1),
                         Qt::AlignRight | Qt::AlignVCenter, grey);
                double cursor = x + 16.0;
                if (multiColour) {
                    painter.setPen(mmPen(grey, 0.15));
                    painter.setBrush(info.palette.at(static_cast<std::size_t>(s.color)));
                    painter.drawRect(QRectF(cursor, mid - 1.6, 3.2, 3.2));
                    cursor += 4.5;
                    const QString from = QString::number(s.from);
                    drawText(painter, QPointF(cursor, mid), 2.7, from, Qt::AlignLeft | Qt::AlignVCenter, grey);
                    cursor += textWidthMm(from, 2.7) + 1.0;
                }
                drawText(painter, QPointF(cursor, mid), 3.0, QStringLiteral("→"), Qt::AlignLeft | Qt::AlignVCenter,
                         grey);
                drawText(painter, QPointF(cursor + 4.0, mid), 3.8, QString::number(s.to),
                         Qt::AlignLeft | Qt::AlignVCenter, Qt::black, true);
            }
        }
        const QString footer = count > 0 ? QStringLiteral("Page %1 of %2   ·   steps %3–%4")
                                               .arg(pageIndex + 1)
                                               .arg(pages)
                                               .arg(firstOnPage + 1)
                                               .arg(next)
                                         : QStringLiteral("Page %1 of %2").arg(pageIndex + 1).arg(pages);
        drawText(painter, QPointF(page.width() / 2.0, page.height() - margin), 2.8, footer,
                 Qt::AlignHCenter | Qt::AlignBottom, grey);
    }
    return finish(painter, path, errorMessage);
}

} // namespace sa
