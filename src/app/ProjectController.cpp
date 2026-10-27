#include "ProjectController.h"

#include "ImageStore.h"
#include "OptimizationWorker.h"
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
    updateProcessed();
}

void ProjectController::updateProcessed()
{
    if (m_base.isNull())
        return;
    m_processed = sa::ImageProcessor::applyTone(m_base, {m_brightness, m_contrast, m_gamma, m_invert});
    m_store->set(QStringLiteral("processed"), sa::ImageProcessor::toQImage(m_processed));
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
        m_resultSequence.clear();
        m_resultTransmittance.clear();
        m_segments = 0;
        m_runMaxSegments = settings.config.maxSegments;
        m_error = m_initialError = m_elapsedSeconds = m_threadLengthMm = 0.0;
        m_stopReason.clear();
        emit progressChanged();

        m_worker->start(std::move(problem), settings.config, renderStyle());
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
        setResult(std::move(result->sequence), std::move(result->transmittance));
        setStatus(tr("Finished: %1 segments in %2 s (%3).")
                      .arg(segmentCount)
                      .arg(result->elapsedSeconds, 0, 'f', 2)
                      .arg(m_stopReason));
    }
}

void ProjectController::setResult(std::vector<int> sequence, std::vector<float> transmittance)
{
    stopPlayback();
    m_resultSequence = std::move(sequence);
    m_resultTransmittance = std::move(transmittance);
    m_playback = std::make_unique<sa::PlaybackRenderer>(*m_resultLayout, m_resultSequence,
                                                        m_runSettings.config.threadWidth,
                                                        m_runSettings.config.threadOpacity);
    m_playbackPosition = m_playback->segmentCount();
    emit stateChanged();
    emit playbackChanged();
    updateResultImages();
}

void ProjectController::updateResultImages()
{
    if (m_running || !hasResult() || !m_resultLayout || !m_playback)
        return;
    const int w = m_resultLayout->canvasWidth();
    const int h = m_resultLayout->canvasHeight();
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
    return m_playbackPosition > 0 && hasResult() ? m_resultSequence[static_cast<std::size_t>(m_playbackPosition) - 1]
                                                 : -1;
}

int ProjectController::currentToPin() const
{
    return m_playbackPosition > 0 && hasResult() ? m_resultSequence[static_cast<std::size_t>(m_playbackPosition)]
                                                 : -1;
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

    m_exporting = true;
    emit stateChanged();
    setStatus(tr("Rendering frame %1...").arg(m_playbackPosition));
    QtConcurrent::run([path, layout, sequence, style, scale, segments] {
        QString error;
        sa::ExportManager::writePng(path, sa::ThreadRenderer::render(layout, sequence, style, scale, segments), &error);
        return error;
    }).then(this, [this, path](const QString &error) {
        m_exporting = false;
        emit stateChanged();
        setStatus(error.isEmpty() ? tr("Saved %1.").arg(path) : tr("Frame export failed: %1").arg(error));
    });
}

void ProjectController::clearResult()
{
    stopPlayback();
    m_playback.reset();
    m_playbackPosition = 0;
    emit playbackChanged();
    m_resultSequence.clear();
    m_resultTransmittance.clear();
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

    m_exporting = true;
    emit stateChanged();
    setStatus(tr("Rendering %1 px PNG...").arg(m_exportSize));
    QtConcurrent::run([path, layout, sequence, style, scale] {
        QString error;
        sa::ExportManager::writePng(path, sa::ThreadRenderer::render(layout, sequence, style, scale), &error);
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
    const bool ok = sa::ExportManager::writeCsv(path, *m_resultLayout, m_resultSequence,
                                                m_csvMillimeters ? sa::LengthUnit::Millimeters
                                                                 : sa::LengthUnit::Pixels,
                                                &error);
    setStatus(ok ? tr("Saved %1 (%2 segments).").arg(path).arg(m_resultSequence.size() - 1)
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
    const bool ok = sa::ExportManager::writeSvg(path, *m_resultLayout, m_resultSequence, style, true, &error);
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

    if (project.sequence.size() > 1) {
        const sa::PinLayout layout = sa::PinLayout::fromFrame(project.frame, project.workingSize);
        m_runSettings = project;
        m_runSettings.imageData.clear();
        m_resultLayout = layout;
        if (m_processed.width == layout.canvasWidth() && m_processed.height == layout.canvasHeight()) {
            const sa::OptimizationProblem problem =
                sa::OptimizationProblem::fromImage(m_processed, layout, project.edgeEmphasis);
            m_resultTarget = problem.target;
            m_resultWeights = problem.weights;
        }
        m_segments = static_cast<int>(project.sequence.size()) - 1;
        m_runMaxSegments = project.config.maxSegments;
        m_error = project.finalError;
        m_initialError = project.initialError;
        m_threadLengthMm = project.threadLengthMm;
        m_elapsedSeconds = project.elapsedSeconds;
        m_stopReason = project.stopReason;
        emit progressChanged();
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
