#include "OptimizationWorker.h"

#include "ImageStore.h"
#include "core/ColorRenderer.h"

#include <chrono>
#include <exception>

OptimizationWorker::OptimizationWorker(std::shared_ptr<ImageStore> store, QObject *parent)
    : QObject(parent), m_store(std::move(store))
{
}

OptimizationWorker::~OptimizationWorker()
{
    cancel();
    if (m_thread.joinable())
        m_thread.join();
}

void OptimizationWorker::start(std::unique_ptr<sa::IOptimizer> optimizer, sa::OptimizationProblem problem,
                               sa::OptimizationConfig config, sa::RenderStyle style,
                               std::array<std::vector<float>, 3> colorTarget)
{
    cancel();
    if (m_thread.joinable())
        m_thread.join();
    {
        const std::lock_guard lock(m_mutex);
        m_paused = false;
        m_snapshot = Snapshot{};
        m_snapshot.maxSegments = config.maxSegments;
        m_result.reset();
        m_error.clear();
    }
    m_thread = std::jthread([this, optimizer = std::move(optimizer), problem = std::move(problem), config, style,
                             colorTarget = std::move(colorTarget)](std::stop_token stop) {
        run(stop, *optimizer, problem, config, style, colorTarget);
    });
}

void OptimizationWorker::pause()
{
    const std::lock_guard lock(m_mutex);
    m_paused = true;
}

void OptimizationWorker::resume()
{
    {
        const std::lock_guard lock(m_mutex);
        m_paused = false;
    }
    m_resumed.notify_all();
}

void OptimizationWorker::cancel()
{
    m_thread.request_stop(); // also wakes a paused wait (condition_variable_any + stop_token)
}

OptimizationWorker::Snapshot OptimizationWorker::snapshot() const
{
    const std::lock_guard lock(m_mutex);
    return m_snapshot;
}

std::optional<sa::StringArtResult> OptimizationWorker::takeResult()
{
    const std::lock_guard lock(m_mutex);
    return std::exchange(m_result, std::nullopt);
}

QString OptimizationWorker::errorMessage() const
{
    const std::lock_guard lock(m_mutex);
    return m_error;
}

void OptimizationWorker::publishImages(std::span<const float> transmittance, std::span<const float> rgb, int width,
                                       int height, const sa::RenderStyle &style,
                                       const sa::OptimizationProblem &problem,
                                       const std::array<std::vector<float>, 3> &colorTarget)
{
    if (!rgb.empty()) {
        m_store->set(QStringLiteral("result"), sa::ColorRenderer::toImage(rgb, width, height));
        m_store->set(QStringLiteral("residual"),
                     sa::ColorRenderer::residualImage(rgb, colorTarget, problem.weights, width, height));
        return;
    }
    m_store->set(QStringLiteral("result"),
                 sa::ThreadRenderer::toImage(transmittance, width, height, style.background, style.thread));
    m_store->set(QStringLiteral("residual"),
                 sa::ThreadRenderer::residualImage(transmittance, problem.target, problem.weights, width, height));
}

void OptimizationWorker::publishProgress(const sa::OptimizationProgress &progress, const sa::RenderStyle &style,
                                         const sa::OptimizationProblem &problem,
                                         const std::array<std::vector<float>, 3> &colorTarget)
{
    publishImages(progress.transmittance, progress.rgb, progress.width, progress.height, style, problem, colorTarget);
    {
        const std::lock_guard lock(m_mutex);
        m_snapshot.segments = progress.segments;
        m_snapshot.maxSegments = progress.maxSegments;
        m_snapshot.error = progress.error;
        m_snapshot.initialError = progress.initialError;
        m_snapshot.threadLengthMm = progress.threadLengthMm;
        m_snapshot.elapsedSeconds = progress.elapsedSeconds;
    }
    emit progressed();
}

void OptimizationWorker::run(std::stop_token stop, sa::IOptimizer &optimizer, const sa::OptimizationProblem &problem,
                             const sa::OptimizationConfig &config, const sa::RenderStyle &style,
                             const std::array<std::vector<float>, 3> &colorTarget)
{
    using Clock = std::chrono::steady_clock;
    auto lastPreview = Clock::now() - std::chrono::hours(1);

    auto onProgress = [&](const sa::OptimizationProgress &progress) {
        const auto now = Clock::now();
        if (now - lastPreview >= std::chrono::milliseconds(kPreviewIntervalMs)) {
            lastPreview = now;
            publishProgress(progress, style, problem, colorTarget);
        }
        std::unique_lock lock(m_mutex);
        if (m_paused) {
            lock.unlock();
            publishProgress(progress, style, problem, colorTarget); // show the exact paused state
            lock.lock();
            m_resumed.wait(lock, stop, [this] { return !m_paused; });
        }
    };

    try {
        sa::StringArtResult result = optimizer.optimize(problem, config, stop, onProgress);
        publishImages(result.transmittance, result.rgb, result.width, result.height, style, problem, colorTarget);
        {
            const std::lock_guard lock(m_mutex);
            m_snapshot.segments = result.segmentCount();
            m_snapshot.error = result.finalError;
            m_snapshot.initialError = result.initialError;
            m_snapshot.threadLengthMm = result.threadLengthMm;
            m_snapshot.elapsedSeconds = result.elapsedSeconds;
            m_result = std::move(result);
        }
        emit finished(true);
    } catch (const std::exception &e) {
        {
            const std::lock_guard lock(m_mutex);
            m_error = QString::fromUtf8(e.what());
        }
        emit finished(false);
    }
}
