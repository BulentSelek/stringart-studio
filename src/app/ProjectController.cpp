#include "ProjectController.h"

#include "ImageStore.h"
#include "OptimizationWorker.h"
#include "algorithms/GreedyOptimizer.h"
#include "algorithms/LayeredColorOptimizer.h"
#include "algorithms/PaletteSuggester.h"
#include "core/ExportManager.h"
#include "core/ImageProcessor.h"
#include "core/LineRasterizer.h"
#include "core/OptimizationProblem.h"
#include "core/WorkshopDocuments.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QDateTime>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QtConcurrent/QtConcurrentRun>

#ifdef STRINGART_HAVE_SPEECH
#include <QTextToSpeech>
#endif

#include <algorithm>
#include <exception>
#include <span>

namespace {

constexpr int kMaxSourceSize = 2048;
constexpr int kBaseUpdateDelayMs = 60;
constexpr int kHistoryDelayMs = 400;

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
    for (auto signal : {&ProjectController::layoutChanged, &ProjectController::settingsChanged})
        connect(this, signal, this, &ProjectController::threadWidthPxChanged);
    m_historyTimer.setSingleShot(true);
    m_historyTimer.setInterval(kHistoryDelayMs);
    connect(&m_historyTimer, &QTimer::timeout, this, &ProjectController::recordHistory);
    for (auto signal : {&ProjectController::layoutChanged, &ProjectController::settingsChanged,
                        &ProjectController::renderStyleChanged, &ProjectController::toneChanged,
                        &ProjectController::geometryChanged, &ProjectController::colorModeChanged,
                        &ProjectController::paletteChanged}) {
        connect(this, signal, this, [this] {
            const bool wasActive = m_historyTimer.isActive();
            m_historyTimer.start();
            if (!wasActive)
                emit historyChanged();
        });
    }
    resetHistory();
    setStatus(tr("Load an image to begin (File dialog or drag and drop)."));
}

ProjectController::~ProjectController()
{
    // The suggestion task posts to this object; let it finish first.
    m_suggestionStop.request_stop();
    m_suggestion.waitForFinished();
}

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

double ProjectController::threadWidthPx() const
{
    const double mm = mmPerPixel();
    return mm > 0.0 ? std::clamp(m_threadDiameterMm / mm, 0.01, 64.0) : 1.0;
}

double ProjectController::mmPerPixel() const
{
    const std::optional<sa::PinLayout> layout = currentLayout();
    return layout ? layout->mmPerUnit() : 0.0;
}

namespace {

QVariantList normalizedPins(const std::optional<sa::PinLayout> &layout)
{
    QVariantList positions;
    if (!layout)
        return positions;
    positions.reserve(layout->pinCount());
    for (const sa::Vec2 &p : layout->pins())
        positions.append(QPointF(p.x / layout->canvasWidth(), p.y / layout->canvasHeight()));
    return positions;
}

} // namespace

QVariantList ProjectController::resultPinPositions() const
{
    return hasResult() ? normalizedPins(m_resultLayout) : QVariantList();
}

QVariantList ProjectController::pinPositions() const
{
    return normalizedPins(currentLayout());
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
    p.config.threadWidth = threadWidthPx();
    p.threadDiameterMm = m_threadDiameterMm;
    p.config.maxThreadLengthMm = m_maxLengthM * 1000.0;
    p.config.allowRepeatedLines = m_allowRepeats;
    p.edgeEmphasis = m_edgeEmphasis;
    p.threadColor = m_threadColor;
    p.backgroundColor = m_backgroundColor;
    p.colorMode = m_colorMode;
    p.palette = m_palette;
    p.colorBackground = m_colorBackground;
    p.layerOrder = m_layerOrder;
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
    m_threadDiameterMm = p.threadDiameterMm;
    m_maxLengthM = p.config.maxThreadLengthMm / 1000.0;
    m_allowRepeats = p.config.allowRepeatedLines;
    m_edgeEmphasis = p.edgeEmphasis;
    m_threadColor = p.threadColor;
    m_backgroundColor = p.backgroundColor;
    m_colorMode = p.colorMode;
    if (!p.palette.empty())
        m_palette = p.palette;
    m_colorBackground = p.colorBackground;
    m_layerOrder = std::clamp(p.layerOrder, 0, 3);
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
    style.threadWidth = threadWidthPx();
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
    clearRuns();
    resetHistory();
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
    if (static_cast<int>(m_palette.size()) >= sa::LayeredColorOptimizer::kMaxColors || !color.isValid())
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
        if (c.isValid() && static_cast<int>(palette.size()) < sa::LayeredColorOptimizer::kMaxColors)
            palette.push_back(c);
    }
    if (palette.empty() || !background.isValid())
        return;
    m_palette = std::move(palette);
    m_colorBackground = background;
    emit paletteChanged();
}

void ProjectController::suggestPalette(int count)
{
    if (m_suggestingPalette || m_running)
        return;
    if (m_baseTimer.isActive()) {
        m_baseTimer.stop();
        updateBase();
    }
    if (!m_colorMode || m_processedRgb.isNull()) {
        setStatus(tr("Load an image and turn on multi-colour threads to get a palette suggestion."));
        return;
    }
    count = std::clamp(count, 1, sa::LayeredColorOptimizer::kMaxColors);
    m_suggestionStop = std::stop_source();
    m_suggestingPalette = true;
    m_suggestionProgress = 0.0;
    emit suggestionChanged();
    setStatus(tr("Searching for %n thread colour(s)...", nullptr, count));
    struct Outcome
    {
        std::vector<QColor> palette;
        QString failure;
    };
    const QColor background = m_colorBackground;
    QFuture<Outcome> task =
        QtConcurrent::run([this, image = m_processedRgb, frame = frameSpec(), config = currentSettings().config,
                           workingSize = m_workingSize, count, background,
                           order = static_cast<sa::LayerOrder>(std::clamp(m_layerOrder, 0, 3)),
                           stop = m_suggestionStop.get_token()] {
            Outcome outcome;
            try {
                outcome.palette = sa::PaletteSuggester::suggest(
                    image, frame, config, workingSize, count, background, order, stop, [this](int done, int total) {
                        QMetaObject::invokeMethod(this, [this, done, total] {
                            m_suggestionProgress = static_cast<double>(done) / std::max(1, total);
                            emit suggestionChanged();
                        }, Qt::QueuedConnection);
                    });
            } catch (const std::exception &e) {
                outcome.failure = QString::fromUtf8(e.what());
            }
            return outcome;
        });
    // The destructor waits for the worker task only, never for this continuation.
    m_suggestion = QFuture<void>(task);
    task.then(this, [this, count, background](const Outcome &outcome) {
            const bool cancelled = m_suggestionStop.stop_requested();
            m_suggestingPalette = false;
            emit suggestionChanged();
            if (!outcome.failure.isEmpty()) {
                setStatus(tr("Palette suggestion failed: %1").arg(outcome.failure));
            } else if (cancelled) {
                setStatus(tr("Palette suggestion cancelled."));
            } else if (!outcome.palette.empty()) {
                QVariantList colors;
                for (const QColor &c : outcome.palette)
                    colors.append(c);
                setPalette(colors, background);
                setStatus(outcome.palette.size() < static_cast<std::size_t>(count)
                              ? tr("Suggested %1 colours; the image does not support more distinct ones.")
                                    .arg(outcome.palette.size())
                              : tr("Suggested %n colour(s) for this image and frame.", nullptr, count));
            }
        });
}

void ProjectController::cancelPaletteSuggestion()
{
    if (m_suggestingPalette)
        m_suggestionStop.request_stop();
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

        if (settings.colorMode) {
            if (m_processedRgb.isNull())
                throw std::invalid_argument("colour image is not ready");
            m_resultColorTarget = m_processedRgb.channels;
            sa::LayeredColorSettings layered;
            layered.order = static_cast<sa::LayerOrder>(std::clamp(settings.layerOrder, 0, 3));
            auto optimizer = std::make_unique<sa::LayeredColorOptimizer>(m_processedRgb.channels, settings.palette,
                                                                         settings.colorBackground, layered);
            m_worker->start(std::move(optimizer), std::move(problem), settings.config, renderStyle(),
                            m_processedRgb.channels);
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
        m_stopReason = QString::fromUtf8(sa::toString(result->stopReason));
        if (!result->colorSteps.empty())
            setColorResult(std::move(result->colorSteps), std::move(result->rgb));
        else
            setResult(std::move(result->sequence), std::move(result->transmittance));
        recordRun();
        setStatus(tr("Finished: %1 segments in %2 s (%3).")
                      .arg(segmentCount)
                      .arg(result->elapsedSeconds, 0, 'f', 2)
                      .arg(m_stopReason));
    }
}

void ProjectController::resetResultState()
{
    endWinding();
    stopPlayback();
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
    if (m_winding) {
        QSettings settings;
        settings.setValue(windingKey(), m_playbackPosition);
    }
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

// ---------------------------------------------------------------- workshop

std::vector<sa::ColorStep> ProjectController::resultSteps() const
{
    return resultIsColor() ? m_resultColorSteps : sa::monochromeSteps(m_resultSequence);
}

std::vector<QColor> ProjectController::resultPalette() const
{
    return resultIsColor() ? m_runSettings.palette : std::vector<QColor>{m_threadColor};
}

QVariantList ProjectController::threadUsage() const
{
    QVariantList rows;
    if (!hasResult() || !m_resultLayout)
        return rows;
    const std::vector<sa::ColorStep> steps = resultSteps();
    const std::vector<QColor> palette = resultPalette();
    for (const sa::ThreadUsage &u : sa::threadUsage(*m_resultLayout, steps, palette)) {
        if (u.segments > 0) {
            rows.append(QVariantMap{{QStringLiteral("color"), u.color},
                                    {QStringLiteral("segments"), u.segments},
                                    {QStringLiteral("lengthM"), u.lengthMm / 1000.0}});
        }
    }
    return rows;
}

int ProjectController::currentColorIndex() const
{
    if (!resultIsColor() || m_playbackPosition <= 0)
        return 0;
    return m_resultColorSteps[static_cast<std::size_t>(m_playbackPosition) - 1].color;
}

bool ProjectController::threadChangesHere() const
{
    if (!resultIsColor() || m_playbackPosition <= 1)
        return false;
    const auto i = static_cast<std::size_t>(m_playbackPosition) - 1;
    return m_resultColorSteps[i].color != m_resultColorSteps[i - 1].color;
}

QVariantList ProjectController::stepsAhead(int count) const
{
    QVariantList steps;
    if (!hasResult())
        return steps;
    const int total = resultSegments();
    for (int k = m_playbackPosition + 1; k <= std::min(total, m_playbackPosition + count); ++k) {
        const auto i = static_cast<std::size_t>(k) - 1;
        const sa::ColorStep s = resultIsColor() ? m_resultColorSteps[i]
                                                : sa::ColorStep{0, m_resultSequence[i], m_resultSequence[i + 1]};
        steps.append(QVariantMap{{QStringLiteral("from"), s.from},
                                 {QStringLiteral("to"), s.to},
                                 {QStringLiteral("color"), s.color}});
    }
    return steps;
}

QString ProjectController::windingKey() const
{
    // Identifies the result, so reopening the same project resumes winding.
    QCryptographicHash hash(QCryptographicHash::Sha1);
    if (resultIsColor()) {
        for (const sa::ColorStep &s : m_resultColorSteps)
            hash.addData(QByteArrayView(reinterpret_cast<const char *>(&s), sizeof(s)));
    } else {
        hash.addData(QByteArrayView(reinterpret_cast<const char *>(m_resultSequence.data()),
                                    static_cast<qsizetype>(m_resultSequence.size() * sizeof(int))));
    }
    return QStringLiteral("Winding/") + QString::fromLatin1(hash.result().toHex().left(16));
}

void ProjectController::beginWinding()
{
    if (!hasResult() || m_running || m_winding)
        return;
    stopPlayback();
    const int saved = QSettings().value(windingKey(), 1).toInt();
    m_winding = true;
    emit windingChanged();
    setPlaybackPosition(std::clamp(saved, 1, resultSegments()));
    setStatus(tr("Winding mode: step %1 of %2.").arg(m_playbackPosition).arg(resultSegments()));
}

void ProjectController::endWinding()
{
    if (!m_winding)
        return;
    m_winding = false;
    emit windingChanged();
    setStatus(tr("Winding paused at step %1 of %2; winding mode resumes there.")
                  .arg(m_playbackPosition)
                  .arg(resultSegments()));
    setPlaybackPosition(resultSegments());
}

bool ProjectController::speechAvailable()
{
#ifdef STRINGART_HAVE_SPEECH
    return !QTextToSpeech::availableEngines().isEmpty();
#else
    return false;
#endif
}

void ProjectController::speak(const QString &text)
{
#ifdef STRINGART_HAVE_SPEECH
    if (!m_speech)
        m_speech = new QTextToSpeech(this);
    m_speech->stop();
    m_speech->say(text);
#else
    Q_UNUSED(text);
#endif
}

void ProjectController::exportPinTemplate(const QUrl &url, int paper)
{
    if (m_exporting)
        return;
    const std::optional<sa::PinLayout> layout = hasResult() ? m_resultLayout : currentLayout();
    if (!layout) {
        setStatus(tr("The frame settings are invalid."));
        return;
    }
    sa::TemplateOptions options;
    options.paper = static_cast<sa::TemplatePaper>(std::clamp(paper, 0, 3));
    options.title = sa::WorkshopDocuments::frameDescription(hasResult() ? m_runSettings.frame : frameSpec());
    const QString path = localPath(url);
    m_exporting = true;
    emit stateChanged();
    setStatus(tr("Writing the pin template..."));
    QtConcurrent::run([path, layout = *layout, options] {
        QString error;
        sa::WorkshopDocuments::writePinTemplatePdf(path, layout, options, &error);
        const int pages = sa::planTiles(sa::WorkshopDocuments::templateContentMm(layout), options).pageCount();
        return std::pair{error, pages};
    }).then(this, [this, path](const std::pair<QString, int> &outcome) {
        m_exporting = false;
        emit stateChanged();
        setStatus(outcome.first.isEmpty()
                      ? tr("Saved %1 (%n sheet(s)). Print at 100% and check the scale bar.", nullptr, outcome.second)
                            .arg(path)
                      : tr("Template export failed: %1").arg(outcome.first));
    });
}

void ProjectController::exportWindingGuide(const QUrl &url)
{
    if (!hasResult() || m_exporting)
        return;
    sa::WorkshopDocuments::GuideInfo info;
    info.title = m_imageName.isEmpty() ? tr("String art") : QFileInfo(m_imageName).completeBaseName();
    info.frameDescription = sa::WorkshopDocuments::frameDescription(m_runSettings.frame);
    info.palette = resultPalette();
    const QString path = localPath(url);
    m_exporting = true;
    emit stateChanged();
    setStatus(tr("Writing the winding guide..."));
    QtConcurrent::run([path, layout = *m_resultLayout, steps = resultSteps(), info] {
        QString error;
        sa::WorkshopDocuments::writeWindingGuidePdf(path, layout, steps, info, &error);
        return error;
    }).then(this, [this, path](const QString &error) {
        m_exporting = false;
        emit stateChanged();
        setStatus(error.isEmpty() ? tr("Saved %1.").arg(path) : tr("Guide export failed: %1").arg(error));
    });
}

// ------------------------------------------------- history, presets, runs

QByteArray ProjectController::parameterSnapshot() const
{
    sa::ProjectData p = currentSettings();
    p.imagePath.clear();
    p.imageData.clear();
    return sa::ProjectFile::toJson(p, false, false);
}

void ProjectController::resetHistory()
{
    m_historyTimer.stop();
    m_history = {parameterSnapshot()};
    m_historyIndex = 0;
    emit historyChanged();
}

void ProjectController::recordHistory()
{
    m_historyTimer.stop();
    QByteArray snapshot = parameterSnapshot();
    if (m_historyIndex >= 0 && m_history[static_cast<std::size_t>(m_historyIndex)] == snapshot) {
        emit historyChanged();
        return;
    }
    m_history.resize(static_cast<std::size_t>(m_historyIndex) + 1); // drops the redo branch
    m_history.push_back(std::move(snapshot));
    if (static_cast<int>(m_history.size()) > kMaxHistory)
        m_history.erase(m_history.begin());
    m_historyIndex = static_cast<int>(m_history.size()) - 1;
    emit historyChanged();
}

void ProjectController::undo()
{
    if (m_running)
        return;
    if (m_historyTimer.isActive())
        recordHistory(); // pending edits become the step to undo
    if (m_historyIndex <= 0)
        return;
    --m_historyIndex;
    applySettings(sa::ProjectFile::fromJson(m_history[static_cast<std::size_t>(m_historyIndex)]));
    m_historyTimer.stop(); // the signals of applySettings are not a new edit
    emit historyChanged();
    setStatus(tr("Undone."));
}

void ProjectController::redo()
{
    if (m_running || m_historyTimer.isActive() || m_historyIndex + 1 >= static_cast<int>(m_history.size()))
        return;
    ++m_historyIndex;
    applySettings(sa::ProjectFile::fromJson(m_history[static_cast<std::size_t>(m_historyIndex)]));
    m_historyTimer.stop();
    emit historyChanged();
    setStatus(tr("Redone."));
}

namespace {

constexpr auto kPresetsKey = "Presets/parameters";

QJsonObject readPresets()
{
    return QJsonDocument::fromJson(QSettings().value(QLatin1StringView(kPresetsKey)).toByteArray()).object();
}

} // namespace

QStringList ProjectController::presetNames() const
{
    QStringList names = readPresets().keys();
    names.sort(Qt::CaseInsensitive);
    return names;
}

bool ProjectController::savePreset(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return false;
    sa::ProjectData p = currentSettings();
    p.imagePath.clear();
    p.imageData.clear();
    p.crop = QRectF();
    p.rotationDegrees = 0.0;
    QJsonObject presets = readPresets();
    presets.insert(trimmed, QString::fromUtf8(sa::ProjectFile::toJson(p, false, false)));
    QSettings().setValue(QLatin1StringView(kPresetsKey), QJsonDocument(presets).toJson(QJsonDocument::Compact));
    emit presetsChanged();
    setStatus(tr("Saved preset \"%1\".").arg(trimmed));
    return true;
}

bool ProjectController::loadPreset(const QString &name)
{
    if (m_running)
        return false;
    const QJsonValue value = readPresets().value(name);
    if (!value.isString())
        return false;
    if (m_historyTimer.isActive())
        recordHistory(); // keep pending edits as their own undo step
    try {
        sa::ProjectData p = sa::ProjectFile::fromJson(value.toString().toUtf8());
        p.crop = m_crop; // presets never move the crop
        p.rotationDegrees = m_rotation;
        applySettings(p);
    } catch (const std::exception &e) {
        setStatus(tr("Preset \"%1\" is invalid: %2").arg(name, QString::fromUtf8(e.what())));
        return false;
    }
    setStatus(tr("Loaded preset \"%1\".").arg(name));
    return true;
}

void ProjectController::deletePreset(const QString &name)
{
    QJsonObject presets = readPresets();
    if (!presets.contains(name))
        return;
    presets.remove(name);
    QSettings().setValue(QLatin1StringView(kPresetsKey), QJsonDocument(presets).toJson(QJsonDocument::Compact));
    emit presetsChanged();
    setStatus(tr("Deleted preset \"%1\".").arg(name));
}

void ProjectController::recordRun()
{
    RunRecord run;
    run.id = m_nextRunId++;
    run.settings = m_runSettings;
    run.error = m_error;
    run.initialError = m_initialError;
    run.lengthM = m_threadLengthMm / 1000.0;
    run.seconds = m_elapsedSeconds;
    run.segments = resultSegments();
    run.time = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    m_store->set(QStringLiteral("run%1").arg(run.id), m_store->get(QStringLiteral("result")));
    m_runs.push_back(std::move(run));
    if (static_cast<int>(m_runs.size()) > kMaxRuns) {
        m_store->set(QStringLiteral("run%1").arg(m_runs.front().id), QImage());
        m_runs.erase(m_runs.begin());
    }
    emit runHistoryChanged();
}

void ProjectController::clearRuns()
{
    for (const RunRecord &run : m_runs)
        m_store->set(QStringLiteral("run%1").arg(run.id), QImage());
    m_runs.clear();
    emit runHistoryChanged();
}

QVariantList ProjectController::runHistory() const
{
    QVariantList rows;
    for (auto it = m_runs.rbegin(); it != m_runs.rend(); ++it) {
        const sa::ProjectData &p = it->settings;
        QString summary = tr("%1 pins, opacity %2, thread %3 mm")
                              .arg(p.frame.kind == sa::PinLayoutKind::Custom
                                       ? static_cast<int>(p.frame.customPinsMm.size())
                                       : p.frame.pinCount)
                              .arg(p.config.threadOpacity, 0, 'f', 2)
                              .arg(p.threadDiameterMm, 0, 'f', 2);
        if (p.edgeEmphasis > 0.0)
            summary += tr(", edges %1").arg(p.edgeEmphasis, 0, 'f', 1);
        if (p.config.minPinSeparation > 0)
            summary += tr(", min. separation %1").arg(p.config.minPinSeparation);
        QVariantList colors;
        if (p.colorMode) {
            for (const QColor &c : p.palette)
                colors.append(c);
        }
        rows.append(QVariantMap{{QStringLiteral("id"), it->id},
                                {QStringLiteral("error"), it->error},
                                {QStringLiteral("initialError"), it->initialError},
                                {QStringLiteral("segments"), it->segments},
                                {QStringLiteral("lengthM"), it->lengthM},
                                {QStringLiteral("seconds"), it->seconds},
                                {QStringLiteral("summary"), summary},
                                {QStringLiteral("colors"), colors},
                                {QStringLiteral("time"), it->time}});
    }
    return rows;
}

void ProjectController::restoreRunSettings(int id)
{
    if (m_running)
        return;
    const auto it = std::find_if(m_runs.begin(), m_runs.end(), [id](const RunRecord &r) { return r.id == id; });
    if (it == m_runs.end())
        return;
    if (m_historyTimer.isActive())
        recordHistory();
    applySettings(it->settings);
    setStatus(tr("Restored the settings of run #%1. Start to recompute it.").arg(id));
}

void ProjectController::removeRun(int id)
{
    const auto it = std::find_if(m_runs.begin(), m_runs.end(), [id](const RunRecord &r) { return r.id == id; });
    if (it == m_runs.end())
        return;
    m_store->set(QStringLiteral("run%1").arg(id), QImage());
    m_runs.erase(it);
    emit runHistoryChanged();
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
    clearRuns();
    resetHistory();
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
    settings.endGroup();
}

void ProjectController::restoreSettings(QSettings &settings)
{
    settings.beginGroup(QStringLiteral("Parameters"));
    const QByteArray json = settings.value(QStringLiteral("project")).toByteArray();
    m_exportSize = settings.value(QStringLiteral("exportSize"), m_exportSize).toInt();
    m_csvMillimeters = settings.value(QStringLiteral("csvMillimeters"), m_csvMillimeters).toBool();
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
    resetHistory();
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
