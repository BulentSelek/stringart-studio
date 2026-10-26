#include "OptimizationWorker.h"

#include "ImageStore.h"
#include "algorithms/GreedyOptimizer.h"

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

void OptimizationWorker::start(sa::OptimizationProblem problem, sa::OptimizationConfig config, sa::RenderStyle style)
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
    m_thread = std::jthread([this, problem = std::move(problem), config, style](std::stop_token stop) {
        run(stop, problem, config, style);
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

void OptimizationWorker::publishPreview(const sa::OptimizationProgress &progress, const sa::RenderStyle &style,
                                        const sa::OptimizationProblem &problem)
{
    m_store->set(QStringLiteral("result"), sa::ThreadRenderer::toImage(progress.transmittance, progress.width,
                                                                       progress.height, style.background, style.thread));
    m_store->set(QStringLiteral("residual"),
                 sa::ThreadRenderer::residualImage(progress.transmittance, problem.target, problem.weights,
                                                   progress.width, progress.height));
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

void OptimizationWorker::run(std::stop_token stop, const sa::OptimizationProblem &problem,
                             const sa::OptimizationConfig &config, const sa::RenderStyle &style)
{
    using Clock = std::chrono::steady_clock;
    auto lastPreview = Clock::now() - std::chrono::hours(1);

    auto onProgress = [&](const sa::OptimizationProgress &progress) {
        const auto now = Clock::now();
        if (now - lastPreview >= std::chrono::milliseconds(kPreviewIntervalMs)) {
            lastPreview = now;
            publishPreview(progress, style, problem);
        }
        std::unique_lock lock(m_mutex);
        if (m_paused) {
            lock.unlock();
            publishPreview(progress, style, problem); // show the exact paused state
            lock.lock();
            m_resumed.wait(lock, stop, [this] { return !m_paused; });
        }
    };

    try {
        sa::GreedyOptimizer optimizer;
        sa::StringArtResult result = optimizer.optimize(problem, config, stop, onProgress);
        m_store->set(QStringLiteral("result"), sa::ThreadRenderer::toImage(result.transmittance, result.width,
                                                                           result.height, style.background,
                                                                           style.thread));
        m_store->set(QStringLiteral("residual"),
                     sa::ThreadRenderer::residualImage(result.transmittance, problem.target, problem.weights,
                                                       result.width, result.height));
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
