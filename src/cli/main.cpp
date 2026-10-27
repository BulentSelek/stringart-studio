#include "algorithms/GreedyOptimizer.h"
#include "algorithms/SequenceOptimizer.h"
#include "core/ExportManager.h"
#include "core/ImageProcessor.h"
#include "core/ProjectFile.h"
#include "core/ThreadRenderer.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>

#include <cstdio>
#include <cstdlib>
#include <exception>

namespace {

void fail(const QString &message)
{
    std::fprintf(stderr, "error: %s\n", qPrintable(message));
    std::exit(1);
}

int intOption(const QCommandLineParser &parser, const QString &name)
{
    bool ok = false;
    const int value = parser.value(name).toInt(&ok);
    if (!ok)
        fail(QStringLiteral("--%1 expects an integer").arg(name));
    return value;
}

double doubleOption(const QCommandLineParser &parser, const QString &name)
{
    bool ok = false;
    const double value = parser.value(name).toDouble(&ok);
    if (!ok)
        fail(QStringLiteral("--%1 expects a number").arg(name));
    return value;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("stringart-cli"));
    QCoreApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Generate string art from an image (greedy optimizer)."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("input"), QStringLiteral("Input image."));
    parser.addOptions({
        {QStringLiteral("pins"), QStringLiteral("Number of pins on the circle."), QStringLiteral("n"), QStringLiteral("240")},
        {QStringLiteral("size"), QStringLiteral("Working resolution in pixels (square)."), QStringLiteral("px"), QStringLiteral("512")},
        {QStringLiteral("segments"), QStringLiteral("Maximum thread segments."), QStringLiteral("n"), QStringLiteral("3000")},
        {QStringLiteral("opacity"), QStringLiteral("Thread opacity in [0, 1]."), QStringLiteral("a"), QStringLiteral("0.2")},
        {QStringLiteral("width"), QStringLiteral("Thread width in working pixels."), QStringLiteral("px"), QStringLiteral("1.0")},
        {QStringLiteral("min-sep"), QStringLiteral("Minimum pin separation."), QStringLiteral("n"), QStringLiteral("20")},
        {QStringLiteral("start-pin"), QStringLiteral("Starting pin."), QStringLiteral("n"), QStringLiteral("0")},
        {QStringLiteral("max-length-mm"), QStringLiteral("Maximum thread length in mm (0 = unlimited)."), QStringLiteral("mm"), QStringLiteral("0")},
        {QStringLiteral("shape"), QStringLiteral("Frame: circle, ellipse, rectangle or custom."), QStringLiteral("shape"), QStringLiteral("circle")},
        {{QStringLiteral("width-mm"), QStringLiteral("diameter-mm")}, QStringLiteral("Frame width (circle: diameter) in mm."), QStringLiteral("mm"), QStringLiteral("500")},
        {QStringLiteral("height-mm"), QStringLiteral("Frame height in mm (ellipse, rectangle)."), QStringLiteral("mm"), QStringLiteral("500")},
        {QStringLiteral("pins-csv"), QStringLiteral("Custom pin positions (x, y in mm); implies --shape custom."), QStringLiteral("path")},
        {QStringLiteral("rotate"), QStringLiteral("Rotate the input clockwise by degrees."), QStringLiteral("deg"), QStringLiteral("0")},
        {QStringLiteral("refine-passes"), QStringLiteral("Sequence refinement passes after greedy (0 = off)."), QStringLiteral("n"), QStringLiteral("0")},
        {QStringLiteral("refine-neighborhood"), QStringLiteral("Refinement candidates within +-n pins (0 = all)."), QStringLiteral("n"), QStringLiteral("20")},
        {QStringLiteral("edge-emphasis"), QStringLiteral("Extra error weight on edges (0 = plain MSE)."), QStringLiteral("k"), QStringLiteral("0")},
        {QStringLiteral("brightness"), QStringLiteral("Brightness offset [-1, 1]."), QStringLiteral("v"), QStringLiteral("0")},
        {QStringLiteral("contrast"), QStringLiteral("Contrast multiplier."), QStringLiteral("v"), QStringLiteral("1")},
        {QStringLiteral("gamma"), QStringLiteral("Gamma (> 1 brightens)."), QStringLiteral("v"), QStringLiteral("1")},
        {QStringLiteral("invert"), QStringLiteral("Invert the input image.")},
        {QStringLiteral("repeat"), QStringLiteral("Allow repeated lines.")},
        {QStringLiteral("threads"), QStringLiteral("Worker threads (0 = all cores)."), QStringLiteral("n"), QStringLiteral("0")},
        {QStringLiteral("png"), QStringLiteral("Output PNG path."), QStringLiteral("path")},
        {QStringLiteral("png-size"), QStringLiteral("Output PNG size in pixels."), QStringLiteral("px"), QStringLiteral("2048")},
        {QStringLiteral("csv"), QStringLiteral("Output CSV path."), QStringLiteral("path")},
        {QStringLiteral("csv-units"), QStringLiteral("CSV units: mm or px."), QStringLiteral("unit"), QStringLiteral("mm")},
        {QStringLiteral("svg"), QStringLiteral("Output SVG path (millimetres)."), QStringLiteral("path")},
        {QStringLiteral("json"), QStringLiteral("Output JSON path (settings, pins, sequence)."), QStringLiteral("path")},
        {QStringLiteral("quiet"), QStringLiteral("Do not print progress.")},
    });
    parser.process(app);

    const QStringList positional = parser.positionalArguments();
    if (positional.size() != 1)
        parser.showHelp(1);

    QImageReader reader(positional.first());
    reader.setAutoTransform(true);
    const QImage original = reader.read();
    if (original.isNull())
        fail(QStringLiteral("cannot read %1: %2").arg(positional.first(), reader.errorString()));

    try {
        const int size = intOption(parser, QStringLiteral("size"));
        if (size < 16 || size > 8192)
            fail(QStringLiteral("--size must be in [16, 8192]"));

        sa::ToneSettings tone;
        tone.brightness = doubleOption(parser, QStringLiteral("brightness"));
        tone.contrast = doubleOption(parser, QStringLiteral("contrast"));
        tone.gamma = doubleOption(parser, QStringLiteral("gamma"));
        tone.invert = parser.isSet(QStringLiteral("invert"));
        sa::FrameSpec frame;
        const QString shape = parser.isSet(QStringLiteral("pins-csv")) ? QStringLiteral("custom")
                                                                       : parser.value(QStringLiteral("shape"));
        if (shape == u"circle")
            frame.kind = sa::PinLayoutKind::Circular;
        else if (shape == u"ellipse")
            frame.kind = sa::PinLayoutKind::Elliptical;
        else if (shape == u"rectangle")
            frame.kind = sa::PinLayoutKind::Rectangular;
        else if (shape == u"custom")
            frame.kind = sa::PinLayoutKind::Custom;
        else
            fail(QStringLiteral("--shape must be circle, ellipse, rectangle or custom"));
        frame.pinCount = intOption(parser, QStringLiteral("pins"));
        frame.widthMm = doubleOption(parser, QStringLiteral("width-mm"));
        frame.heightMm = frame.kind == sa::PinLayoutKind::Circular ? frame.widthMm
                                                                   : doubleOption(parser, QStringLiteral("height-mm"));
        if (frame.kind == sa::PinLayoutKind::Custom) {
            QFile pinFile(parser.value(QStringLiteral("pins-csv")));
            if (!pinFile.open(QIODevice::ReadOnly | QIODevice::Text))
                fail(QStringLiteral("--shape custom needs a readable --pins-csv file"));
            frame.customPinsMm = sa::ProjectFile::parsePinCsv(QString::fromUtf8(pinFile.readAll()));
        }
        const sa::PinLayout pins = sa::PinLayout::fromFrame(frame, size);
        const double rotation = doubleOption(parser, QStringLiteral("rotate"));
        const double edgeEmphasis = doubleOption(parser, QStringLiteral("edge-emphasis"));
        const sa::GrayImage working = sa::ImageProcessor::applyTone(
            sa::ImageProcessor::prepareBase(original, pins.canvasWidth(), pins.canvasHeight(), {}, rotation), tone);
        const sa::OptimizationProblem problem = sa::OptimizationProblem::fromImage(working, pins, edgeEmphasis);

        sa::OptimizationConfig config;
        config.maxSegments = intOption(parser, QStringLiteral("segments"));
        config.threadOpacity = static_cast<float>(doubleOption(parser, QStringLiteral("opacity")));
        config.threadWidth = doubleOption(parser, QStringLiteral("width"));
        config.minPinSeparation = intOption(parser, QStringLiteral("min-sep"));
        config.startPin = intOption(parser, QStringLiteral("start-pin"));
        config.maxThreadLengthMm = doubleOption(parser, QStringLiteral("max-length-mm"));
        config.allowRepeatedLines = parser.isSet(QStringLiteral("repeat"));
        config.threadCount = intOption(parser, QStringLiteral("threads"));

        const bool quiet = parser.isSet(QStringLiteral("quiet"));
        int lastReported = -1;
        sa::GreedyOptimizer optimizer;
        sa::StringArtResult result =
            optimizer.optimize(problem, config, {}, [&](const sa::OptimizationProgress &p) {
                if (quiet || p.segments == lastReported || (p.segments % 250 != 0 && p.segments != p.maxSegments))
                    return;
                lastReported = p.segments;
                std::fprintf(stderr, "\r%6d / %d segments  error %.6f  %.1f s", p.segments, p.maxSegments, p.error,
                             p.elapsedSeconds);
            });
        if (!quiet)
            std::fprintf(stderr, "\n");

        const int refinePasses = intOption(parser, QStringLiteral("refine-passes"));
        if (refinePasses > 0) {
            const double greedyError = result.finalError;
            const double greedySeconds = result.elapsedSeconds;
            sa::SequenceOptimizer refiner(result.sequence,
                                          {refinePasses, intOption(parser, QStringLiteral("refine-neighborhood"))});
            const double initialError = result.initialError;
            result = refiner.optimize(problem, config);
            std::printf("refinement:      %.6f -> %.6f, %d moves in %d passes, %.3f s (%s)\n", greedyError,
                        result.finalError, refiner.improvements(), refiner.passes(), result.elapsedSeconds,
                        sa::toString(result.stopReason));
            result.initialError = initialError;
            result.elapsedSeconds += greedySeconds;
        }

        std::printf("segments:        %d\n", result.segmentCount());
        std::printf("stop reason:     %s\n", sa::toString(result.stopReason));
        std::printf("initial error:   %.6f (weighted MSE, sRGB-encoded)\n", result.initialError);
        std::printf("final error:     %.6f\n", result.finalError);
        std::printf("thread length:   %.2f m\n", result.threadLengthMm / 1000.0);
        std::printf("time:            %.3f s\n", result.elapsedSeconds);
        std::printf("line cache:      %s (%.1f MiB)\n", result.lineCachePrecomputed ? "precomputed" : "on demand",
                    result.lineCacheBytes / (1024.0 * 1024.0));

        if (parser.isSet(QStringLiteral("png"))) {
            sa::RenderStyle style;
            style.threadWidth = config.threadWidth;
            style.opacity = config.threadOpacity;
            const double scale = static_cast<double>(intOption(parser, QStringLiteral("png-size"))) / size;
            // --png-size is the long side, like --size.
            QString error;
            if (!sa::ExportManager::writePng(parser.value(QStringLiteral("png")),
                                             sa::ThreadRenderer::render(pins, result.sequence, style, scale), &error))
                fail(QStringLiteral("PNG export failed: %1").arg(error));
        }
        if (parser.isSet(QStringLiteral("csv"))) {
            const QString units = parser.value(QStringLiteral("csv-units"));
            if (units != u"mm" && units != u"px")
                fail(QStringLiteral("--csv-units must be mm or px"));
            QString error;
            if (!sa::ExportManager::writeCsv(parser.value(QStringLiteral("csv")), pins, result.sequence,
                                             units == u"mm" ? sa::LengthUnit::Millimeters : sa::LengthUnit::Pixels,
                                             &error))
                fail(QStringLiteral("CSV export failed: %1").arg(error));
        }
        if (parser.isSet(QStringLiteral("svg"))) {
            sa::RenderStyle style;
            style.threadWidth = config.threadWidth;
            style.opacity = config.threadOpacity;
            QString error;
            if (!sa::ExportManager::writeSvg(parser.value(QStringLiteral("svg")), pins, result.sequence, style, true,
                                             &error))
                fail(QStringLiteral("SVG export failed: %1").arg(error));
        }
        if (parser.isSet(QStringLiteral("json"))) {
            sa::ProjectData project;
            project.frame = frame;
            project.workingSize = size;
            project.imagePath = QFileInfo(positional.first()).absoluteFilePath();
            project.rotationDegrees = rotation;
            project.tone = tone;
            project.config = config;
            project.edgeEmphasis = edgeEmphasis;
            project.sequence = result.sequence;
            project.stopReason = QString::fromUtf8(sa::toString(result.stopReason));
            project.initialError = result.initialError;
            project.finalError = result.finalError;
            project.threadLengthMm = result.threadLengthMm;
            project.elapsedSeconds = result.elapsedSeconds;
            QString error;
            if (!sa::ExportManager::writeText(parser.value(QStringLiteral("json")),
                                              sa::ProjectFile::toJson(project, false, true), &error))
                fail(QStringLiteral("JSON export failed: %1").arg(error));
        }
    } catch (const std::exception &e) {
        fail(QString::fromUtf8(e.what()));
    }
    return 0;
}
