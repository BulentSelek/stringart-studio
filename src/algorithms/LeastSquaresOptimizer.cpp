#include "algorithms/LeastSquaresOptimizer.h"

#include "algorithms/GreedyOptimizer.h"
#include "core/Compositing.h"
#include "core/LineRasterizer.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace sa {

namespace {

struct DensitySample
{
    std::uint32_t index;
    float density; // -ln(1 - a c)
};

double weightedDensitySse(const std::vector<double> &residual, std::span<const float> weights)
{
    double sum = 0.0;
    for (std::size_t i = 0; i < residual.size(); ++i)
        sum += weights[i] * residual[i] * residual[i];
    return sum;
}

} // namespace

LeastSquaresResult LeastSquaresOptimizer::solve(const OptimizationProblem &problem, const OptimizationConfig &config,
                                                const std::vector<int> &sequence,
                                                const LeastSquaresSettings &settings, std::stop_token stop)
{
    problem.validate();
    const PinLayout &layout = problem.pins;
    validateConfig(config, layout.pinCount());
    if (config.threadOpacity >= 0.999f || config.threadOpacity <= 0.0f)
        throw std::invalid_argument("least-squares refinement requires 0 < opacity < 1");
    if (!(settings.maxWeight > 0.0) || settings.maxIterations < 0 || !(settings.minTransmittance > 0.0))
        throw std::invalid_argument("invalid least-squares settings");

    // Distinct lines and their multiplicities, in first-use order.
    std::map<std::pair<int, int>, std::size_t> lineIndex;
    LeastSquaresResult result;
    result.width = problem.width;
    result.height = problem.height;
    for (std::size_t i = 1; i < sequence.size(); ++i) {
        const int a = sequence[i - 1];
        const int b = sequence[i];
        if (a < 0 || b < 0 || a >= layout.pinCount() || b >= layout.pinCount() || a == b)
            throw std::invalid_argument("sequence references an invalid pin pair");
        const auto key = std::minmax(a, b);
        const auto [it, inserted] = lineIndex.try_emplace(key, result.lines.size());
        if (inserted) {
            result.lines.push_back(key);
            result.initialWeights.push_back(0.0);
        }
        result.initialWeights[it->second] += 1.0;
    }

    // Per-line density samples.
    std::vector<std::vector<DensitySample>> columns(result.lines.size());
    std::vector<LineSample> samples;
    for (std::size_t l = 0; l < result.lines.size(); ++l) {
        rasterizePinLine(layout, result.lines[l].first, result.lines[l].second, config.threadWidth, problem.width,
                         problem.height, samples);
        columns[l].reserve(samples.size());
        for (const LineSample &s : samples)
            columns[l].push_back({s.index, -std::log(1.0f - config.threadOpacity * s.coverage)});
    }

    const std::span<const float> weights = problem.weights;
    const double maxWeight = settings.maxWeight;
    std::vector<double> x = result.initialWeights;
    for (double &v : x)
        v = std::min(v, maxWeight);

    // Residual r = D - D*.
    std::vector<double> residual(problem.target.size());
    for (std::size_t p = 0; p < residual.size(); ++p) {
        const double t = std::max<double>(srgbToLinear(problem.target[p]), settings.minTransmittance);
        residual[p] = std::log(t); // = -D*(p) with D = 0
    }
    for (std::size_t l = 0; l < columns.size(); ++l) {
        for (const DensitySample &s : columns[l])
            residual[s.index] += x[l] * s.density;
    }
    result.initialObjective = weightedDensitySse(residual, weights);

    double objective = result.initialObjective;
    for (int iteration = 0; iteration < settings.maxIterations; ++iteration) {
        if (stop.stop_requested()) {
            result.cancelled = true;
            break;
        }
        for (std::size_t l = 0; l < columns.size(); ++l) {
            double g = 0.0;
            double h = 0.0;
            for (const DensitySample &s : columns[l]) {
                const double wd = weights[s.index] * s.density;
                g += wd * residual[s.index];
                h += wd * s.density;
            }
            if (h <= 0.0)
                continue; // line covers only zero-weight pixels
            const double next = std::clamp(x[l] - g / h, 0.0, maxWeight);
            const double step = next - x[l];
            if (step == 0.0)
                continue;
            x[l] = next;
            for (const DensitySample &s : columns[l])
                residual[s.index] += step * s.density;
        }
        ++result.iterations;
        const double updated = weightedDensitySse(residual, weights);
        const bool converged = objective - updated <= settings.tolerance * std::max(objective, 1e-12);
        objective = updated;
        if (converged)
            break;
    }
    result.finalObjective = objective;
    result.weights = std::move(x);

    // Virtual reconstruction and its error under the standard objective.
    result.transmittance.assign(problem.target.size(), 1.0f);
    std::vector<double> density(problem.target.size(), 0.0);
    for (std::size_t l = 0; l < columns.size(); ++l) {
        for (const DensitySample &s : columns[l])
            density[s.index] += result.weights[l] * s.density;
    }
    for (std::size_t p = 0; p < density.size(); ++p)
        result.transmittance[p] = static_cast<float>(std::exp(-density[p]));
    double weightSum = 0.0;
    for (float w : problem.weights)
        weightSum += w;
    result.virtualError = weightSum > 0.0
                              ? GreedyOptimizer::weightedSse(result.transmittance, problem.target, problem.weights)
                                    / weightSum
                              : 0.0;
    return result;
}

} // namespace sa
