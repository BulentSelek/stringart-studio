#include "ProjectController.h"

#include "ImageStore.h"
#include "OptimizationWorker.h"
#include "core/ExportManager.h"
#include "core/ImageProcessor.h"
#include "core/OptimizationProblem.h"

#include <QFileInfo>
#include <QFuture>
#include <QImageReader>
#include <QtConcurrent/QtConcurrentRun>

#include <cmath>
#include <exception>

namespace {

constexpr int kMaxDisplaySize = 2048;

} // namespace

ProjectController::ProjectController(std::shared_ptr<ImageStore> store, QObject *parent)
    : QObject(parent), m_store(std::move(store)), m_worker(new OptimizationWorker(m_store, this))
{
    connect(m_worker, &OptimizationWorker::progressed, this, &ProjectController::onWorkerProgress);
    connect(m_worker, &OptimizationWorker::finished, this, &ProjectController::onWorkerFinished);
    connect(this, &ProjectController::workingSizeChanged, this, &ProjectController::updateBase);
    connect(this, &ProjectController::toneChanged, this, &ProjectController::updateProcessed);
    connect(this, &ProjectController::pinCountChanged, this, &ProjectController::pinPositionsChanged);
    connect(this, &ProjectController::renderStyleChanged, this, &ProjectController::updateResultImage);
    setStatus(tr("Load an image to begin (File dialog or drag and drop)."));
}

ProjectController::~ProjectController() = default;

bool ProjectController::loadImage(const QUrl &url)
{
    if (m_running) {
        setStatus(tr("Cancel the running optimization before loading a new image."));
        return false;
    }
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull()) {
        setStatus(tr("Cannot open %1: %2").arg(QFileInfo(path).fileName(), reader.errorString()));
        return false;
    }
    m_original = std::move(image);
    m_imageName = QFileInfo(path).fileName();
    const QImage display = m_original.width() > kMaxDisplaySize || m_original.height() > kMaxDisplaySize
                               ? m_original.scaled(kMaxDisplaySize, kMaxDisplaySize, Qt::KeepAspectRatio,
                                                   Qt::SmoothTransformation)
                               : m_original;
    m_store->set(QStringLiteral("original"), display);
    ++m_originalRevision;

    m_resultSequence.clear();
    m_resultLayout.reset();
    m_store->set(QStringLiteral("result"), QImage());
    ++m_resultRevision;
    emit resultRevisionChanged();
    emit stateChanged();

    updateBase();
    setStatus(tr("Loaded %1 (%2 x %3).").arg(m_imageName).arg(m_original.width()).arg(m_original.height()));
    return true;
}

void ProjectController::updateBase()
{
    if (m_original.isNull())
        return;
    m_base = sa::ImageProcessor::prepareBase(m_original, m_workingSize, m_workingSize);
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

QVariantList ProjectController::pinPositions() const
{
    QVariantList positions;
    if (m_pinCount < sa::PinLayout::kMinPins || m_pinCount > sa::PinLayout::kMaxPins)
        return positions;
    const sa::PinLayout layout = sa::PinLayout::circular(m_pinCount, 1000, 1000);
    positions.reserve(layout.pinCount());
    for (const sa::Vec2 &p : layout.pins())
        positions.append(QPointF(p.x / 1000.0, p.y / 1000.0));
    return positions;
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

void ProjectController::start()
{
    if (m_running || m_processed.isNull())
        return;
    try {
        const sa::PinLayout layout = sa::PinLayout::circular(m_pinCount, m_workingSize, m_workingSize, m_diameterMm);
        sa::OptimizationProblem problem = sa::OptimizationProblem::fromImage(m_processed, layout);

        sa::OptimizationConfig config;
        config.maxSegments = m_maxSegments;
        config.minPinSeparation = m_minSeparation;
        config.startPin = m_startPin;
        config.threadOpacity = static_cast<float>(m_opacity);
        config.threadWidth = m_threadWidth;
        config.maxThreadLengthMm = m_maxLengthM * 1000.0;
        config.allowRepeatedLines = m_allowRepeats;
        sa::validateConfig(config, layout.pinCount());

        m_resultLayout = layout;
        m_resultStyle = renderStyle();
        m_resultSequence.clear();
        m_segments = 0;
        m_runMaxSegments = m_maxSegments;
        m_error = m_initialError = m_elapsedSeconds = m_threadLengthMm = 0.0;
        emit progressChanged();

        m_worker->start(std::move(problem), config, m_resultStyle);
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
        m_resultSequence = std::move(result->sequence);
        m_resultTransmittance = std::move(result->transmittance);
        emit stateChanged();
        setStatus(tr("Finished: %1 segments in %2 s (%3).")
                      .arg(segmentCount)
                      .arg(result->elapsedSeconds, 0, 'f', 2)
                      .arg(QString::fromUtf8(sa::toString(result->stopReason))));
    }
}

void ProjectController::updateResultImage()
{
    if (m_running || !hasResult() || !m_resultLayout)
        return;
    m_resultStyle.background = m_backgroundColor;
    m_resultStyle.thread = m_threadColor;
    m_store->set(QStringLiteral("result"),
                 sa::ThreadRenderer::toImage(m_resultTransmittance, m_resultLayout->canvasWidth(),
                                             m_resultLayout->canvasHeight(), m_resultStyle.background,
                                             m_resultStyle.thread));
    ++m_resultRevision;
    emit resultRevisionChanged();
}

void ProjectController::exportPng(const QUrl &url)
{
    if (!hasResult() || m_exporting)
        return;
    const QString path = url.toLocalFile();
    const sa::PinLayout layout = *m_resultLayout;
    const std::vector<int> sequence = m_resultSequence;
    sa::RenderStyle style = m_resultStyle;
    style.background = m_backgroundColor;
    style.thread = m_threadColor;
    const double scale = static_cast<double>(m_exportSize) / layout.canvasWidth();

    m_exporting = true;
    emit stateChanged();
    setStatus(tr("Rendering %1 x %1 PNG...").arg(m_exportSize));
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
    const QString path = url.toLocalFile();
    QString error;
    const bool ok = sa::ExportManager::writeCsv(path, *m_resultLayout, m_resultSequence,
                                                m_csvMillimeters ? sa::LengthUnit::Millimeters
                                                                 : sa::LengthUnit::Pixels,
                                                &error);
    setStatus(ok ? tr("Saved %1 (%2 segments).").arg(path).arg(m_resultSequence.size() - 1)
                 : tr("CSV export failed: %1").arg(error));
}

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
