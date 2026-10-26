#pragma once

#include "core/OptimizationConfig.h"
#include "core/OptimizationProblem.h"
#include "core/StringArtResult.h"
#include "core/ThreadRenderer.h"

#include <QObject>
#include <QString>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

class ImageStore;

// Runs the greedy optimizer on a dedicated std::jthread. The GUI thread only
// receives throttled notifications: the worker renders a preview at most
// every kPreviewIntervalMs into the shared ImageStore and emits progressed();
// the controller then reads snapshot(). Pausing blocks inside the progress
// callback; cancelling requests a stop on the thread's stop_token.
class OptimizationWorker final : public QObject
{
    Q_OBJECT

public:
    static constexpr int kPreviewIntervalMs = 120;

    struct Snapshot
    {
        int segments = 0;
        int maxSegments = 0;
        double error = 0.0;
        double initialError = 0.0;
        double threadLengthMm = 0.0;
        double elapsedSeconds = 0.0;
    };

    explicit OptimizationWorker(std::shared_ptr<ImageStore> store, QObject *parent = nullptr);
    ~OptimizationWorker() override;

    // Starts a run; any previous run is cancelled and joined first.
    void start(sa::OptimizationProblem problem, sa::OptimizationConfig config, sa::RenderStyle style);
    void pause();
    void resume();
    void cancel();

    [[nodiscard]] Snapshot snapshot() const;
    // Result of the last finished run; empty while running or after failure.
    [[nodiscard]] std::optional<sa::StringArtResult> takeResult();
    [[nodiscard]] QString errorMessage() const;

signals:
    void progressed();
    void finished(bool success);

private:
    void run(std::stop_token stop, const sa::OptimizationProblem &problem, const sa::OptimizationConfig &config,
             const sa::RenderStyle &style);
    void publishPreview(const sa::OptimizationProgress &progress, const sa::RenderStyle &style);

    std::shared_ptr<ImageStore> m_store;
    mutable std::mutex m_mutex;
    std::condition_variable_any m_resumed;
    bool m_paused = false;
    Snapshot m_snapshot;
    std::optional<sa::StringArtResult> m_result;
    QString m_error;
    std::jthread m_thread; // declared last: joined before the members above are destroyed
};
