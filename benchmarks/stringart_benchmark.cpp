#include "algorithms/GreedyOptimizer.h"
#include "core/ImageProcessor.h"

#include "SyntheticImages.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QImageReader>

#include <chrono>
#include <cstdio>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

// Runs one greedy optimization and prints measured runtime, CPU utilisation,
// peak resident memory and reconstruction error as a Markdown table row.
// Run one configuration per process so that peak memory is not shared
// between configurations (see benchmarks/run_benchmarks.sh).
namespace {

struct Usage
{
    double cpuSeconds = 0.0;
    double peakMiB = 0.0;
};

Usage processUsage()
{
    Usage usage;
#if defined(Q_OS_WIN)
    FILETIME creation, exit, kernel, user;
    if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) {
        auto seconds = [](FILETIME t) {
            return (static_cast<double>(t.dwHighDateTime) * 4294967296.0 + t.dwLowDateTime) * 1e-7;
        };
        usage.cpuSeconds = seconds(kernel) + seconds(user);
    }
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        usage.peakMiB = counters.PeakWorkingSetSize / (1024.0 * 1024.0);
#else
    rusage ru{};
    getrusage(RUSAGE_SELF, &ru);
    usage.cpuSeconds = ru.ru_utime.tv_sec + ru.ru_utime.tv_usec * 1e-6 + ru.ru_stime.tv_sec + ru.ru_stime.tv_usec * 1e-6;
    usage.peakMiB = ru.ru_maxrss / 1024.0; // KiB on Linux
#endif
    return usage;
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("String art greedy optimizer benchmark."));
    parser.addHelpOption();
    parser.addOptions({
        {QStringLiteral("size"), QStringLiteral("Working resolution."), QStringLiteral("px"), QStringLiteral("512")},
        {QStringLiteral("pins"), QStringLiteral("Pin count."), QStringLiteral("n"), QStringLiteral("200")},
        {QStringLiteral("segments"), QStringLiteral("Maximum segments."), QStringLiteral("n"), QStringLiteral("4000")},
        {QStringLiteral("threads"), QStringLiteral("Threads (0 = all cores, at most 16)."), QStringLiteral("n"), QStringLiteral("0")},
        {QStringLiteral("cache-mib"), QStringLiteral("Line cache budget in MiB."), QStringLiteral("n"), QStringLiteral("1024")},
        {QStringLiteral("image"), QStringLiteral("Input image (default: synthetic portrait)."), QStringLiteral("path")},
        {QStringLiteral("header"), QStringLiteral("Print the table header and exit.")},
    });
    parser.process(app);

    if (parser.isSet(QStringLiteral("header"))) {
        std::printf("| size | pins | segments | threads | line cache | runtime (s) | ms/segment | CPU util. | peak RAM (MiB) | "
                    "initial error | final error | stop reason |\n");
        std::printf("|---|---|---|---|---|---|---|---|---|---|---|---|\n");
        return 0;
    }

    const int size = parser.value(QStringLiteral("size")).toInt();
    const int pins = parser.value(QStringLiteral("pins")).toInt();
    QImage input;
    if (parser.isSet(QStringLiteral("image"))) {
        QImageReader reader(parser.value(QStringLiteral("image")));
        reader.setAutoTransform(true);
        input = reader.read();
    } else {
        input = sa::synthetic::portrait(1024);
    }
    if (input.isNull() || size < 16 || pins < 3) {
        std::fprintf(stderr, "invalid arguments\n");
        return 1;
    }

    const sa::GrayImage working = sa::ImageProcessor::prepareBase(input, size, size);
    const sa::OptimizationProblem problem =
        sa::OptimizationProblem::fromImage(working, sa::PinLayout::circular(pins, size, size));
    sa::OptimizationConfig config;
    config.maxSegments = parser.value(QStringLiteral("segments")).toInt();
    config.minPinSeparation = std::max(1, pins / 10);
    config.threadOpacity = 0.2f;
    config.threadWidth = 1.0;
    config.threadCount = parser.value(QStringLiteral("threads")).toInt();
    config.lineCacheBudgetBytes = parser.value(QStringLiteral("cache-mib")).toULongLong() * 1024 * 1024;
    const int threads = sa::threadCount(config);

    const Usage before = processUsage();
    const auto start = std::chrono::steady_clock::now();
    sa::GreedyOptimizer optimizer;
    const sa::StringArtResult result = optimizer.optimize(problem, config);
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const Usage after = processUsage();

    // CPU utilisation: CPU time / wall time / threads (100 % = all threads busy).
    const double cpu = after.cpuSeconds - before.cpuSeconds;
    std::printf("| %d | %d | %d | %d | %s %.0f MiB | %.2f | %.3f | %.0f %% of %d | %.0f | %.5f | %.5f | %s |\n", size, pins,
                result.segmentCount(), threads, result.lineCachePrecomputed ? "precomputed" : "on demand",
                result.lineCacheBytes / (1024.0 * 1024.0), wall, 1000.0 * wall / std::max(1, result.segmentCount()),
                100.0 * cpu / wall / threads, threads, after.peakMiB, result.initialError, result.finalError,
                sa::toString(result.stopReason));
    return 0;
}
