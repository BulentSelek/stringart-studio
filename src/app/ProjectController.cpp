#include "ProjectController.h"

#include "ImageStore.h"
#include "OptimizationWorker.h"
#include "algorithms/ColorGreedyOptimizer.h"
#include "algorithms/GreedyOptimizer.h"
#include "algorithms/LeastSquaresOptimizer.h"
#include "algorithms/SequenceOptimizer.h"
#include "core/ExportManager.h"
#include "core/ImageProcessor.h"
#include "core/OptimizationProblem.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QImageReader>
#include <QSettings>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <exception>
#include <span>

namespace {

constexpr int kMaxSourceSize = 2048;
constexpr int kBaseUpdateDelayMs = 60;

QImage readImage(const QByteArray &bytes, QString *error)
{
    QBuffer buffer;
    buffer.setData(bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull() && error)
        *error = reader.errorString();
    return image;
}

} // namespace

ProjectController::ProjectController(std::shared_ptr<ImageStore> store, QObject *parent)
    : QObject(parent), m_store(std::move(store)), m_worker(new OptimizationWorker(m_store, this))
{
    m_baseTimer.setSingleShot(true);
    m_baseTimer.setInterval(kBaseUpdateDelayMs);
    connect(&m_baseTimer, &QTimer::timeout, this, &ProjectController::updateBase);
    connect(m_worker, &OptimizationWorker::progressed, this, &ProjectController::onWorkerProgress);
    connect(m_worker, &OptimizationWorker::finished, this, &ProjectController::onWorkerFinished);
    connect(this, &ProjectController::layoutChanged, this, &ProjectController::scheduleBaseUpdate);
    connect(this, &ProjectController::geometryChanged, this, &ProjectController::scheduleBaseUpdate);
    connect(this, &ProjectController::toneChanged, this, &ProjectController::updateProcessed);
    connect(this, &ProjectController::renderStyleChanged, this, &ProjectController::updateResultImages);
    connect(this, &ProjectController::colorModeChanged, this, &ProjectController::scheduleBaseUpdate);
    m_playTimer.setInterval(33);
    connect(&m_playTimer, &QTimer::timeout, this, &ProjectController::advancePlayback);
    for (auto signal : {&ProjectController::geometryChanged, &ProjectController::layoutChanged,
                        &ProjectController::imagesChanged})
        connect(this, signal, this, &ProjectController::effectiveCropChanged);
    setStatus(tr("Load an image to begin (File dialog or drag and drop)."));
}

ProjectController::~ProjectController() = default;

QString ProjectController::localPath(const QUrl &url)
{
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}

// ---------------------------------------------------------------- settings

sa::FrameSpec ProjectController::frameSpec() const
{
    sa::FrameSpec frame;
    frame.kind = static_cast<sa::PinLayoutKind>(m_frameShape);
    frame.pinCount = m_pinCount;
    frame.widthMm = m_frameWidthMm;
    frame.heightMm = m_frameShape == Circle ? m_frameWidthMm : m_frameHeightMm;
    if (m_frameShape == Custom)
        frame.customPinsMm = m_customPinsMm;
    return frame;
}

std::optional<sa::PinLayout> ProjectController::currentLayout() const
{
    try {
        return sa::PinLayout::fromFrame(frameSpec(), m_workingSize);
    } catch (const std::exception &) {
        return std::nullopt;
    }
}

int ProjectController::canvasWidth() const
{
    try {
        return sa::PinLayout::canvasSize(frameSpec(), m_workingSize).first;
    } catch (const std::exception &) {
        return m_workingSize;
    }
}

int ProjectController::canvasHeight() const
{
    try {
        return sa::PinLayout::canvasSize(frameSpec(), m_workingSize).second;
    } catch (const std::exception &) {
        return m_workingSize;
    }
}

double ProjectController::mmPerPixel() const
{
    const std::optional<sa::PinLayout> layout = currentLayout();
    return layout ? layout->mmPerUnit() : 0.0;
}

QVariantList ProjectController::pinPositions() const
{
    QVariantList positions;
    const std::optional<sa::PinLayout> layout = currentLayout();
    if (!layout)
        return positions;
    positions.reserve(layout->pinCount());
    for (const sa::Vec2 &p : layout->pins())
        positions.append(QPointF(p.x / layout->canvasWidth(), p.y / layout->canvasHeight()));
    return positions;
}

sa::ProjectData ProjectController::currentSettings() const
{
    sa::ProjectData p;
    p.frame = frameSpec();
    p.workingSize = m_workingSize;
    p.imagePath = m_imagePath;
    p.imageData = m_sourceBytes;
    p.crop = m_crop;
    p.rotationDegrees = m_rotation;
    p.tone = {m_brightness, m_contrast, m_gamma, m_invert};
    p.config.maxSegments = m_maxSegments;
    p.config.minPinSeparation = m_minSeparation;
    p.config.startPin = m_startPin;
    p.config.threadOpacity = static_cast<float>(m_opacity);
    p.config.threadWidth = m_threadWidth;
    p.config.maxThreadLengthMm = m_maxLengthM * 1000.0;
    p.config.allowRepeatedLines = m_allowRepeats;
    p.edgeEmphasis = m_edgeEmphasis;
    p.threadColor = m_threadColor;
    p.backgroundColor = m_backgroundColor;
    p.colorMode = m_colorMode;
    p.palette = m_palette;
    p.colorBackground = m_colorBackground;
    return p;
}

sa::ProjectData ProjectController::projectForSaving() const
{
    // A result is only meaningful with the settings that produced it.
    if (!hasResult())
        return currentSettings();
    sa::ProjectData p = m_runSettings;
    p.imagePath = m_imagePath;
    p.imageData = m_sourceBytes;
    p.threadColor = m_threadColor;
    p.backgroundColor = m_backgroundColor;
    p.sequence = m_resultSequence;
    p.colorSteps = m_resultColorSteps;
    p.stopReason = m_stopReason;
    p.initialError = m_initialError;
    p.finalError = m_error;
    p.threadLengthMm = m_threadLengthMm;
    p.elapsedSeconds = m_elapsedSeconds;
    return p;
}

void ProjectController::applySettings(const sa::ProjectData &p)
{
    m_frameShape = static_cast<FrameShape>(p.frame.kind);
    m_pinCount = p.frame.pinCount;
    m_frameWidthMm = p.frame.widthMm;
    m_frameHeightMm = p.frame.heightMm;
    m_customPinsMm = p.frame.customPinsMm;
    m_workingSize = p.workingSize;
    m_crop = p.crop;
    m_rotation = p.rotationDegrees;
    m_brightness = p.tone.brightness;
    m_contrast = p.tone.contrast;
    m_gamma = p.tone.gamma;
    m_invert = p.tone.invert;
    m_maxSegments = p.config.maxSegments;
    m_minSeparation = p.config.minPinSeparation;
    m_startPin = p.config.startPin;
    m_opacity = p.config.threadOpacity;
    m_threadWidth = p.config.threadWidth;
    m_maxLengthM = p.config.maxThreadLengthMm / 1000.0;
    m_allowRepeats = p.config.allowRepeatedLines;
    m_edgeEmphasis = p.edgeEmphasis;
    m_threadColor = p.threadColor;
    m_backgroundColor = p.backgroundColor;
    m_colorMode = p.colorMode;
    if (!p.palette.empty())
        m_palette = p.palette;
    m_colorBackground = p.colorBackground;
    emit colorModeChanged();
    emit paletteChanged();
    emit layoutChanged();
    emit settingsChanged();
    emit toneChanged();
    emit geometryChanged();
    emit renderStyleChanged();
}

sa::RenderStyle ProjectController::renderStyle() const
{
    sa::RenderStyle style;
    style.threadWidth = m_threadWidth;
    style.opacity = static_cast<float>(m_opacity);
    style.background = m_backgroundColor;
    style.thread = m_threadColor;
    return style;
}

// ------------------------------------------------------------------ images

bool ProjectController::loadImage(const QUrl &url)
{
    if (m_running) {
        setStatus(tr("Cancel the running optimization before loading a new image."));
        return false;
    }
    const QString path = localPath(url);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setStatus(tr("Cannot open %1: %2").arg(QFileInfo(path).fileName(), file.errorString()));
        return false;
    }
    QByteArray bytes = file.readAll();
    QString error;
    QImage image = readImage(bytes, &error);
    if (image.isNull()) {
        setStatus(tr("Cannot open %1: %2").arg(QFileInfo(path).fileName(), error));
        return false;
    }
    const QSize size = image.size();
    m_crop = QRectF();
    m_rotation = 0.0;
    emit geometryChanged();
    setImage(std::move(image), std::move(bytes), path);
    clearResult();
    setStatus(tr("Loaded %1 (%2 x %3).").arg(m_imageName).arg(size.width()).arg(size.height()));
    return true;
}

bool ProjectController::setImage(QImage image, QByteArray bytes, const QString &path)
{
    m_source = image.width() > kMaxSourceSize || image.height() > kMaxSourceSize
                   ? image.scaled(kMaxSourceSize, kMaxSourceSize, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                   : std::move(image);
    m_sourceBytes = std::move(bytes);
    m_imagePath = path;
    m_imageName = QFileInfo(path).fileName();
    m_baseTimer.stop();
    updateBase();
    return !m_source.isNull();
}

void ProjectController::rotateBy(double degrees)
{
    m_rotation = std::remainder(m_rotation + degrees, 360.0);
    m_crop = QRectF(); // the old crop refers to the previous orientation
    emit geometryChanged();
}

void ProjectController::resetCrop()
{
    m_crop = QRectF();
    emit geometryChanged();
}

void ProjectController::fitWholeImage()
{
    if (m_source.isNull())
        return;
    const bool elliptical = m_frameShape == Circle || m_frameShape == Ellipse;
    m_crop = sa::ImageProcessor::containingCrop(m_rotatedSize, canvasWidth(), canvasHeight(), elliptical);
    emit geometryChanged();
}

QRectF ProjectController::effectiveCrop() const
{
    if (m_source.isNull())
        return {};
    return sa::ImageProcessor::effectiveCrop(m_rotatedSize, canvasWidth(), canvasHeight(), m_crop);
}

QVariantList ProjectController::palette() const
{
    QVariantList colors;
    for (const QColor &c : m_palette)
        colors.append(c);
    return colors;
}

void ProjectController::addPaletteColor(const QColor &color)
{
    if (static_cast<int>(m_palette.size()) >= sa::ColorGreedyOptimizer::kMaxColors || !color.isValid())
        return;
    m_palette.push_back(color);
    emit paletteChanged();
}

void ProjectController::removePaletteColor(int index)
{
    if (index < 0 || index >= static_cast<int>(m_palette.size()) || m_palette.size() <= 1)
        return;
    m_palette.erase(m_palette.begin() + index);
    emit paletteChanged();
}

void ProjectController::setPaletteColor(int index, const QColor &color)
{
    if (index < 0 || index >= static_cast<int>(m_palette.size()) || !color.isValid())
        return;
    m_palette[static_cast<std::size_t>(index)] = color;
    emit paletteChanged();
}

void ProjectController::setPalette(const QVariantList &colors, const QColor &background)
{
    std::vector<QColor> palette;
    for (const QVariant &v : colors) {
        const QColor c = v.value<QColor>();
        if (c.isValid() && static_cast<int>(palette.size()) < sa::ColorGreedyOptimizer::kMaxColors)
            palette.push_back(c);
    }
    if (palette.empty() || !background.isValid())
        return;
    m_palette = std::move(palette);
    m_colorBackground = background;
    emit paletteChanged();
}

void ProjectController::scheduleBaseUpdate()
{
    if (!m_source.isNull())
        m_baseTimer.start();
}

void ProjectController::updateBase()
{
    if (m_source.isNull()) {
        m_base = {};
        m_processed = {};
        m_histogram.clear();
        m_store->set(QStringLiteral("original"), QImage());
        m_store->set(QStringLiteral("processed"), QImage());
        ++m_originalRevision;
        ++m_processedRevision;
        emit imagesChanged();
        return;
    }
    const QImage rotated = sa::ImageProcessor::rotated(m_source, m_rotation);
    m_rotatedSize = rotated.size();
    m_store->set(QStringLiteral("original"), rotated);
    ++m_originalRevision;
    m_base = sa::ImageProcessor::prepareBase(rotated, canvasWidth(), canvasHeight(), m_crop, 0.0);
    m_baseRgb = m_colorMode ? sa::ImageProcessor::prepareBaseRgb(rotated, canvasWidth(), canvasHeight(), m_crop, 0.0)
                            : sa::RgbImage{};
    updateProcessed();
}

void ProjectController::updateProcessed()
{
    if (m_base.isNull())
        return;
    const sa::ToneSettings tone{m_brightness, m_contrast, m_gamma, m_invert};
    m_processed = sa::ImageProcessor::applyTone(m_base, tone);
    if (m_colorMode && !m_baseRgb.isNull()) {
        m_processedRgb = sa::ImageProcessor::applyTone(m_baseRgb, tone);
        m_store->set(QStringLiteral("processed"), sa::ImageProcessor::toQImage(m_processedRgb));
    } else {
        m_processedRgb = {};
        m_store->set(QStringLiteral("processed"), sa::ImageProcessor::toQImage(m_processed));
    }
    m_histogram.clear();
    for (int count : sa::ImageProcessor::histogram(m_processed))
        m_histogram.append(count);
    ++m_processedRevision;
    emit imagesChanged();
}

bool ProjectController::importPins(const QUrl &url)
{
    if (m_running)
        return false;
    const QString path = localPath(url);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setStatus(tr("Cannot open %1: %2").arg(QFileInfo(path).fileName(), file.errorString()));
        return false;
    }
    try {
        std::vector<sa::Vec2> pins = sa::ProjectFile::parsePinCsv(QString::fromUtf8(file.readAll()));
        sa::PinLayout::custom(pins, 512, 512); // validates geometry
        m_customPinsMm = std::move(pins);
    } catch (const std::exception &e) {
        setStatus(tr("Invalid pin file %1: %2").arg(QFileInfo(path).fileName(), QString::fromUtf8(e.what())));
        return false;
    }
    m_frameShape = Custom;
    emit layoutChanged();
    setStatus(tr("Imported %1 pins from %2.").arg(m_customPinsMm.size()).arg(QFileInfo(path).fileName()));
    return true;
}

// ------------------------------------------------------------ optimization

void ProjectController::start()
{
    if (m_running || m_source.isNull())
        return;
    if (m_baseTimer.isActive()) {
        m_baseTimer.stop();
        updateBase();
    }
    try {
        const sa::ProjectData settings = currentSettings();
        const sa::PinLayout layout = sa::PinLayout::fromFrame(settings.frame, settings.workingSize);
        sa::validateConfig(settings.config, layout.pinCount());
        sa::OptimizationProblem problem =
            sa::OptimizationProblem::fromImage(m_processed, layout, settings.edgeEmphasis);

        m_runSettings = settings;
        m_runSettings.imageData.clear();
        m_resultLayout = layout;
        m_resultTarget = problem.target;
        m_resultWeights = problem.weights;
        stopPlayback();
        m_playback.reset();
        m_colorPlayback.reset();
        m_resultSequence.clear();
        m_resultTransmittance.clear();
        m_resultColorSteps.clear();
        m_segments = 0;
        m_runMaxSegments = settings.config.maxSegments;
        m_error = m_initialError = m_elapsedSeconds = m_threadLengthMm = 0.0;
        m_stopReason.clear();
        emit progressChanged();

        m_refining = false;
        if (settings.colorMode) {
            if (m_processedRgb.isNull())
                throw std::invalid_argument("colour image is not ready");
            m_resultColorTarget = m_processedRgb.channels;
            m_worker->start(std::make_unique<sa::ColorGreedyOptimizer>(m_processedRgb.channels, settings.palette,
                                                                       settings.colorBackground),
                            std::move(problem), settings.config, renderStyle(), m_processedRgb.channels);
        } else {
            m_worker->start(std::make_unique<sa::GreedyOptimizer>(), std::move(problem), settings.config,
                            renderStyle());
        }
        setState(true, false);
        setStatus(tr("Optimizing..."));
    } catch (const std::exception &e) {
        setStatus(tr("Invalid settings: %1").arg(QString::fromUtf8(e.what())));
    }
}

void ProjectController::refineSequence()
{
    if (m_running || !hasResult() || !m_resultLayout || m_resultTarget.empty())
        return;
    if (resultIsColor()) {
        setStatus(tr("Sequence refinement is available for single-colour results only."));
        return;
    }
    if (m_runSettings.config.threadOpacity >= 0.999f) {
        setStatus(tr("Sequence refinement needs a thread opacity below 1."));
        return;
    }
    sa::OptimizationProblem problem;
    problem.width = m_resultLayout->canvasWidth();
    problem.height = m_resultLayout->canvasHeight();
    problem.target = m_resultTarget;
    problem.weights = m_resultWeights;
    problem.pins = *m_resultLayout;

    stopPlayback();
    m_refining = true;
    m_refineStartError = m_initialError;
    m_runMaxSegments = static_cast<int>(m_resultSequence.size()) - 1;
    sa::SequenceRefinementSettings settings;
    settings.maxPasses = m_refinePasses;
    settings.neighborhood = m_refineNeighborhood;
    m_worker->start(std::make_unique<sa::SequenceOptimizer>(m_resultSequence, settings), std::move(problem),
                    m_runSettings.config, renderStyle());
    setState(true, false);
    setStatus(tr("Refining the sequence..."));
}

void ProjectController::computeVirtual()
{
    if (m_running || m_computingVirtual || !hasResult() || !m_resultLayout || m_resultTarget.empty())
        return;
    if (resultIsColor()) {
        setStatus(tr("The virtual reconstruction is available for single-colour results only."));
        return;
    }
    const float opacity = m_runSettings.config.threadOpacity;
    if (opacity <= 0.0f || opacity >= 0.999f) {
        setStatus(tr("The virtual reconstruction needs a thread opacity between 0 and 1."));
        return;
    }
    sa::OptimizationProblem problem;
    problem.width = m_resultLayout->canvasWidth();
    problem.height = m_resultLayout->canvasHeight();
    problem.target = m_resultTarget;
    problem.weights = m_resultWeights;
    problem.pins = *m_resultLayout;
    const sa::OptimizationConfig config = m_runSettings.config;
    const std::vector<int> sequence = m_resultSequence;
    const int generation = m_resultGeneration;
    const QColor background = m_backgroundColor;
    const QColor thread = m_threadColor;

    m_computingVirtual = true;
    emit virtualChanged();
    setStatus(tr("Computing the virtual least-squares reconstruction..."));
    struct Outcome
    {
        QImage image;
        double error = 0.0;
        int lines = 0;
        QString failure;
    };
    QtConcurrent::run([problem = std::move(problem), config, sequence, background, thread] {
        Outcome outcome;
        try {
            const sa::LeastSquaresResult ls = sa::LeastSquaresOptimizer::solve(problem, config, sequence);
            outcome.image = sa::ThreadRenderer::toImage(ls.transmittance, ls.width, ls.height, background, thread);
            outcome.error = ls.virtualError;
            outcome.lines = static_cast<int>(ls.lines.size());
        } catch (const std::exception &e) {
            outcome.failure = QString::fromUtf8(e.what());
        }
        return outcome;
    }).then(this, [this, generation](const Outcome &outcome) {
        m_computingVirtual = false;
        if (generation != m_resultGeneration) { // the result changed meanwhile
            emit virtualChanged();
            return;
        }
        if (!outcome.failure.isEmpty()) {
            emit virtualChanged();
            setStatus(tr("Virtual reconstruction failed: %1").arg(outcome.failure));
            return;
        }
        m_store->set(QStringLiteral("virtual"), outcome.image);
        m_virtualError = outcome.error;
        m_virtualLines = outcome.lines;
        ++m_virtualRevision;
        emit virtualChanged();
        setStatus(tr("Virtual reconstruction: error %1 with ideal weights on %2 lines, vs %3 for the actual thread.")
                      .arg(m_virtualError, 0, 'f', 5)
                      .arg(m_virtualLines)
                      .arg(m_error, 0, 'f', 5));
    });
}

void ProjectController::pause()
{
    if (!m_running || m_paused)
        return;
    m_worker->pause();
    setState(true, true);
    setStatus(tr("Paused."));
}

void ProjectController::resume()
{
    if (!m_running || !m_paused)
        return;
    m_worker->resume();
    setState(true, false);
    setStatus(tr("Optimizing..."));
}

void ProjectController::cancel()
{
    if (!m_running)
        return;
    m_worker->cancel();
    setStatus(tr("Cancelling..."));
}

void ProjectController::onWorkerProgress()
{
    const OptimizationWorker::Snapshot s = m_worker->snapshot();
    m_segments = s.segments;
    m_runMaxSegments = s.maxSegments;
    m_error = s.error;
    m_initialError = s.initialError;
    m_elapsedSeconds = s.elapsedSeconds;
    m_threadLengthMm = s.threadLengthMm;
    emit progressChanged();
    ++m_resultRevision;
    emit resultRevisionChanged();
}

void ProjectController::onWorkerFinished(bool success)
{
    onWorkerProgress();
    setState(false, false);
    if (!success) {
        setStatus(tr("Optimization failed: %1").arg(m_worker->errorMessage()));
        return;
    }
    if (std::optional<sa::StringArtResult> result = m_worker->takeResult()) {
        const int segmentCount = result->segmentCount();
        if (m_refining) {
            m_refining = false;
            m_initialError = m_refineStartError; // keep the empty-canvas baseline for the reduction figure
            m_error = result->finalError;
            m_segments = segmentCount;
            m_elapsedSeconds += result->elapsedSeconds;
            emit progressChanged();
            setResult(std::move(result->sequence), std::move(result->transmittance));
            setStatus(tr("Refined in %1 s: error %2 → %3, %4 segments (%5).")
                          .arg(result->elapsedSeconds, 0, 'f', 2)
                          .arg(result->initialError, 0, 'f', 5)
                          .arg(result->finalError, 0, 'f', 5)
                          .arg(segmentCount)
                          .arg(QString::fromUtf8(sa::toString(result->stopReason))));
            return;
        }
        m_stopReason = QString::fromUtf8(sa::toString(result->stopReason));
        if (!result->colorSteps.empty())
            setColorResult(std::move(result->colorSteps), std::move(result->rgb));
        else
            setResult(std::move(result->sequence), std::move(result->transmittance));
        setStatus(tr("Finished: %1 segments in %2 s (%3).")
                      .arg(segmentCount)
                      .arg(result->elapsedSeconds, 0, 'f', 2)
                      .arg(m_stopReason));
    }
}

void ProjectController::resetResultState()
{
    stopPlayback();
    ++m_resultGeneration;
    m_virtualLines = 0;
    m_virtualError = 0.0;
    emit virtualChanged();
    m_playback.reset();
    m_colorPlayback.reset();
    m_resultSequence.clear();
    m_resultTransmittance.clear();
    m_resultColorSteps.clear();
}

void ProjectController::setColorResult(std::vector<sa::ColorStep> steps, std::vector<float> rgb)
{
    resetResultState();
    m_resultColorSteps = std::move(steps);
    m_colorPlayback = std::make_unique<sa::ColorPlaybackRenderer>(
        *m_resultLayout, m_resultColorSteps, m_runSettings.palette, m_runSettings.colorBackground,
        m_runSettings.config.threadWidth, m_runSettings.config.threadOpacity, std::move(rgb));
    m_playbackPosition = m_colorPlayback->segmentCount();
    emit stateChanged();
    emit playbackChanged();
    updateResultImages();
}

void ProjectController::setResult(std::vector<int> sequence, std::vector<float> transmittance)
{
    resetResultState();
    m_resultSequence = std::move(sequence);
    m_resultTransmittance = std::move(transmittance);
    m_playback = std::make_unique<sa::PlaybackRenderer>(*m_resultLayout, m_resultSequence,
                                                        m_runSettings.config.threadWidth,
                                                        m_runSettings.config.threadOpacity, m_resultTransmittance);
    m_playbackPosition = m_playback->segmentCount();
    emit stateChanged();
    emit playbackChanged();
    updateResultImages();
}

void ProjectController::updateResultImages()
{
    if (m_running || !hasResult() || !m_resultLayout)
        return;
    const int w = m_resultLayout->canvasWidth();
    const int h = m_resultLayout->canvasHeight();
    if (m_colorPlayback) {
        const std::span<const float> rgb = m_colorPlayback->seek(m_playbackPosition);
        m_store->set(QStringLiteral("result"), sa::ColorRenderer::toImage(rgb, w, h));
        m_store->set(QStringLiteral("residual"),
                     sa::ColorRenderer::residualImage(rgb, m_resultColorTarget, m_resultWeights, w, h));
        ++m_resultRevision;
        emit resultRevisionChanged();
        return;
    }
    if (!m_playback)
        return;
    const std::span<const float> t = m_playback->seek(m_playbackPosition);
    m_store->set(QStringLiteral("result"), sa::ThreadRenderer::toImage(t, w, h, m_backgroundColor, m_threadColor));
    m_store->set(QStringLiteral("residual"),
                 sa::ThreadRenderer::residualImage(t, m_resultTarget, m_resultWeights, w, h));
    ++m_resultRevision;
    emit resultRevisionChanged();
}

void ProjectController::setPlaybackPosition(int position)
{
    position = std::clamp(position, 0, resultSegments());
    if (position == m_playbackPosition)
        return;
    m_playbackPosition = position;
    updateResultImages();
    emit playbackChanged();
}

int ProjectController::currentFromPin() const
{
    if (m_playbackPosition <= 0 || !hasResult())
        return -1;
    if (resultIsColor())
        return m_resultColorSteps[static_cast<std::size_t>(m_playbackPosition) - 1].from;
    return m_resultSequence[static_cast<std::size_t>(m_playbackPosition) - 1];
}

int ProjectController::currentToPin() const
{
    if (m_playbackPosition <= 0 || !hasResult())
        return -1;
    if (resultIsColor())
        return m_resultColorSteps[static_cast<std::size_t>(m_playbackPosition) - 1].to;
    return m_resultSequence[static_cast<std::size_t>(m_playbackPosition)];
}

QColor ProjectController::currentThreadColor() const
{
    if (resultIsColor() && m_playbackPosition > 0) {
        const int c = m_resultColorSteps[static_cast<std::size_t>(m_playbackPosition) - 1].color;
        return m_runSettings.palette.at(static_cast<std::size_t>(c));
    }
    return m_threadColor;
}

QVariantList ProjectController::currentSegmentLine() const
{
    if (m_playbackPosition <= 0 || !hasResult() || !m_resultLayout)
        return {};
    const sa::Vec2 &a = m_resultLayout->pin(currentFromPin());
    const sa::Vec2 &b = m_resultLayout->pin(currentToPin());
    const double w = m_resultLayout->canvasWidth();
    const double h = m_resultLayout->canvasHeight();
    return {QPointF(a.x / w, a.y / h), QPointF(b.x / w, b.y / h)};
}

void ProjectController::play()
{
    if (!hasResult() || m_running || m_playTimer.isActive())
        return;
    if (m_playbackPosition >= resultSegments())
        setPlaybackPosition(0);
    m_playbackCarry = 0.0;
    m_playClock.start();
    m_playTimer.start();
    emit playbackChanged();
}

void ProjectController::stopPlayback()
{
    if (!m_playTimer.isActive())
        return;
    m_playTimer.stop();
    emit playbackChanged();
}

void ProjectController::stepPlayback(int delta)
{
    stopPlayback();
    setPlaybackPosition(m_playbackPosition + delta);
}

void ProjectController::advancePlayback()
{
    m_playbackCarry += m_playbackSpeed * m_playClock.restart() / 1000.0;
    const int steps = static_cast<int>(m_playbackCarry);
    m_playbackCarry -= steps;
    if (steps > 0)
        setPlaybackPosition(m_playbackPosition + steps);
    if (m_playbackPosition >= resultSegments())
        stopPlayback();
}

void ProjectController::exportFrame(const QUrl &url)
{
    if (!hasResult() || m_exporting)
        return;
    const QString path = localPath(url);
    const sa::PinLayout layout = *m_resultLayout;
    const std::vector<int> sequence = m_resultSequence;
    sa::RenderStyle style = renderStyle();
    style.threadWidth = m_runSettings.config.threadWidth;
    style.opacity = m_runSettings.config.threadOpacity;
    const double scale = static_cast<double>(m_exportSize) / std::max(layout.canvasWidth(), layout.canvasHeight());
    const auto segments = static_cast<std::size_t>(m_playbackPosition);
    const std::vector<sa::ColorStep> steps = m_resultColorSteps;
    const sa::ProjectData run = m_runSettings;

    m_exporting = true;
    emit stateChanged();
    setStatus(tr("Rendering frame %1...").arg(m_playbackPosition));
    QtConcurrent::run([path, layout, sequence, steps, run, style, scale, segments] {
        QString error;
        const QImage image = steps.empty() ? sa::ThreadRenderer::render(layout, sequence, style, scale, segments)
                                           : sa::ColorRenderer::render(layout, steps, run.palette, run.colorBackground,
                                                                       style.threadWidth, style.opacity, scale,
                                                                       segments);
        sa::ExportManager::writePng(path, image, &error);
        return error;
    }).then(this, [this, path](const QString &error) {
        m_exporting = false;
        emit stateChanged();
        setStatus(error.isEmpty() ? tr("Saved %1.").arg(path) : tr("Frame export failed: %1").arg(error));
    });
}

void ProjectController::clearResult()
{
    ++m_resultGeneration;
    m_virtualLines = 0;
    m_virtualError = 0.0;
    m_store->set(QStringLiteral("virtual"), QImage());
    emit virtualChanged();
    resetResultState();
    m_playbackPosition = 0;
    emit playbackChanged();
    m_resultLayout.reset();
    m_resultTarget.clear();
    m_resultWeights.clear();
    m_segments = m_runMaxSegments = 0;
    m_error = m_initialError = m_elapsedSeconds = m_threadLengthMm = 0.0;
    m_stopReason.clear();
    m_store->set(QStringLiteral("result"), QImage());
    m_store->set(QStringLiteral("residual"), QImage());
    ++m_resultRevision;
    emit resultRevisionChanged();
    emit progressChanged();
    emit stateChanged();
}

// ------------------------------------------------------------------ export

void ProjectController::exportPng(const QUrl &url)
{
    if (!hasResult() || m_exporting)
        return;
    const QString path = localPath(url);
    const sa::PinLayout layout = *m_resultLayout;
    const std::vector<int> sequence = m_resultSequence;
    sa::RenderStyle style = renderStyle();
    style.threadWidth = m_runSettings.config.threadWidth;
    style.opacity = m_runSettings.config.threadOpacity;
    const double scale = static_cast<double>(m_exportSize) / std::max(layout.canvasWidth(), layout.canvasHeight());
    const std::vector<sa::ColorStep> steps = m_resultColorSteps;
    const sa::ProjectData run = m_runSettings;

    m_exporting = true;
    emit stateChanged();
    setStatus(tr("Rendering %1 px PNG...").arg(m_exportSize));
    QtConcurrent::run([path, layout, sequence, steps, run, style, scale] {
        QString error;
        const QImage image = steps.empty() ? sa::ThreadRenderer::render(layout, sequence, style, scale)
                                           : sa::ColorRenderer::render(layout, steps, run.palette, run.colorBackground,
                                                                       style.threadWidth, style.opacity, scale);
        sa::ExportManager::writePng(path, image, &error);
        return error;
    }).then(this, [this, path](const QString &error) {
        m_exporting = false;
        emit stateChanged();
        setStatus(error.isEmpty() ? tr("Saved %1.").arg(path) : tr("PNG export failed: %1").arg(error));
    });
}

void ProjectController::exportCsv(const QUrl &url)
{
    if (!hasResult())
        return;
    const QString path = localPath(url);
    QString error;
    const sa::LengthUnit unit = m_csvMillimeters ? sa::LengthUnit::Millimeters : sa::LengthUnit::Pixels;
    const bool ok =
        resultIsColor()
            ? sa::ExportManager::writeText(path,
                                           sa::ExportManager::colorCsvText(*m_resultLayout, m_resultColorSteps,
                                                                           m_runSettings.palette, unit)
                                               .toUtf8(),
                                           &error)
            : sa::ExportManager::writeCsv(path, *m_resultLayout, m_resultSequence, unit, &error);
    setStatus(ok ? tr("Saved %1 (%2 segments).").arg(path).arg(resultSegments())
                 : tr("CSV export failed: %1").arg(error));
}

void ProjectController::exportSvg(const QUrl &url)
{
    if (!hasResult())
        return;
    const QString path = localPath(url);
    sa::RenderStyle style = renderStyle();
    style.threadWidth = m_runSettings.config.threadWidth;
    style.opacity = m_runSettings.config.threadOpacity;
    QString error;
    const bool ok =
        resultIsColor()
            ? sa::ExportManager::writeText(path,
                                           sa::ExportManager::colorSvgText(*m_resultLayout, m_resultColorSteps,
                                                                           m_runSettings.palette,
                                                                           m_runSettings.colorBackground,
                                                                           style.threadWidth, style.opacity, true)
                                               .toUtf8(),
                                           &error)
            : sa::ExportManager::writeSvg(path, *m_resultLayout, m_resultSequence, style, true, &error);
    setStatus(ok ? tr("Saved %1.").arg(path) : tr("SVG export failed: %1").arg(error));
}

void ProjectController::exportJson(const QUrl &url)
{
    const QString path = localPath(url);
    QString error;
    bool ok = false;
    try {
        ok = sa::ExportManager::writeText(path, sa::ProjectFile::toJson(projectForSaving(), false, true), &error);
    } catch (const std::exception &e) {
        error = QString::fromUtf8(e.what());
    }
    setStatus(ok ? tr("Saved %1.").arg(path) : tr("JSON export failed: %1").arg(error));
}

// ---------------------------------------------------------------- projects

bool ProjectController::saveProject(const QUrl &url)
{
    if (m_running) {
        setStatus(tr("Wait for the optimization to finish before saving."));
        return false;
    }
    const QString path = localPath(url);
    QString error;
    if (!sa::ExportManager::writeText(path, sa::ProjectFile::toJson(projectForSaving(), true, false), &error)) {
        setStatus(tr("Saving failed: %1").arg(error));
        return false;
    }
    setStatus(hasResult() ? tr("Saved project %1 with the result and the settings of the last run.").arg(path)
                          : tr("Saved project %1.").arg(path));
    return true;
}

bool ProjectController::openProject(const QUrl &url)
{
    if (m_running) {
        setStatus(tr("Cancel the running optimization before opening a project."));
        return false;
    }
    const QString path = localPath(url);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setStatus(tr("Cannot open %1: %2").arg(QFileInfo(path).fileName(), file.errorString()));
        return false;
    }
    sa::ProjectData project;
    try {
        project = sa::ProjectFile::fromJson(file.readAll());
    } catch (const std::exception &e) {
        setStatus(tr("Cannot open %1: %2").arg(QFileInfo(path).fileName(), QString::fromUtf8(e.what())));
        return false;
    }

    // Image: embedded bytes first, then the referenced file.
    QString imageNote;
    QByteArray bytes = project.imageData;
    if (bytes.isEmpty() && !project.imagePath.isEmpty()) {
        QFile imageFile(project.imagePath);
        if (imageFile.open(QIODevice::ReadOnly))
            bytes = imageFile.readAll();
    }
    QString imageError;
    QImage image = bytes.isEmpty() ? QImage() : readImage(bytes, &imageError);
    if (image.isNull())
        imageNote = tr(" The input image is not available.");

    clearResult();
    applySettings(project);
    if (image.isNull()) {
        m_source = QImage();
        m_sourceBytes.clear();
        m_imagePath = project.imagePath;
        m_imageName = QFileInfo(project.imagePath).fileName();
        m_baseTimer.stop();
        updateBase();
    } else {
        setImage(std::move(image), std::move(bytes), project.imagePath);
    }

    if (project.sequence.size() > 1 || !project.colorSteps.empty()) {
        const sa::PinLayout layout = sa::PinLayout::fromFrame(project.frame, project.workingSize);
        m_runSettings = project;
        m_runSettings.imageData.clear();
        m_resultLayout = layout;
        if (m_processed.width == layout.canvasWidth() && m_processed.height == layout.canvasHeight()) {
            const sa::OptimizationProblem problem =
                sa::OptimizationProblem::fromImage(m_processed, layout, project.edgeEmphasis);
            m_resultTarget = problem.target;
            m_resultWeights = problem.weights;
            if (!m_processedRgb.isNull())
                m_resultColorTarget = m_processedRgb.channels;
        }
        m_segments = project.colorSteps.empty() ? static_cast<int>(project.sequence.size()) - 1
                                                : static_cast<int>(project.colorSteps.size());
        m_runMaxSegments = project.config.maxSegments;
        m_error = project.finalError;
        m_initialError = project.initialError;
        m_threadLengthMm = project.threadLengthMm;
        m_elapsedSeconds = project.elapsedSeconds;
        m_stopReason = project.stopReason;
        emit progressChanged();
        if (!project.colorSteps.empty())
            setColorResult(project.colorSteps, {});
        else
            setResult(project.sequence, sa::ThreadRenderer::renderTransmittance(layout, project.sequence,
                                                                                project.config.threadWidth,
                                                                                project.config.threadOpacity));
    }
    setStatus(tr("Opened %1.").arg(QFileInfo(path).fileName()) + imageNote);
    return true;
}

// ---------------------------------------------------------------- settings

void ProjectController::saveSettings(QSettings &settings) const
{
    // Reuses the validated project format for the parameters.
    sa::ProjectData parameters = currentSettings();
    parameters.imagePath.clear();
    parameters.imageData.clear();
    parameters.crop = QRectF();
    parameters.rotationDegrees = 0.0;
    settings.beginGroup(QStringLiteral("Parameters"));
    settings.setValue(QStringLiteral("project"), sa::ProjectFile::toJson(parameters, false, false));
    settings.setValue(QStringLiteral("exportSize"), m_exportSize);
    settings.setValue(QStringLiteral("csvMillimeters"), m_csvMillimeters);
    settings.setValue(QStringLiteral("refinePasses"), m_refinePasses);
    settings.setValue(QStringLiteral("refineNeighborhood"), m_refineNeighborhood);
    settings.endGroup();
}

void ProjectController::restoreSettings(QSettings &settings)
{
    settings.beginGroup(QStringLiteral("Parameters"));
    const QByteArray json = settings.value(QStringLiteral("project")).toByteArray();
    m_exportSize = settings.value(QStringLiteral("exportSize"), m_exportSize).toInt();
    m_csvMillimeters = settings.value(QStringLiteral("csvMillimeters"), m_csvMillimeters).toBool();
    m_refinePasses = std::clamp(settings.value(QStringLiteral("refinePasses"), m_refinePasses).toInt(), 1, 50);
    m_refineNeighborhood =
        std::clamp(settings.value(QStringLiteral("refineNeighborhood"), m_refineNeighborhood).toInt(), 0, 1000);
    settings.endGroup();
    emit settingsChanged();
    if (json.isEmpty())
        return;
    try {
        sa::ProjectData parameters = sa::ProjectFile::fromJson(json);
        parameters.crop = m_crop;
        parameters.rotationDegrees = m_rotation;
        applySettings(parameters);
    } catch (const std::exception &) {
        // Stale or corrupt settings: keep the defaults.
    }
}

// ------------------------------------------------------------------- state

void ProjectController::setStatus(const QString &text)
{
    if (text == m_statusText)
        return;
    m_statusText = text;
    emit statusTextChanged();
}

void ProjectController::setState(bool running, bool paused)
{
    if (running == m_running && paused == m_paused)
        return;
    m_running = running;
    m_paused = paused;
    emit stateChanged();
}
