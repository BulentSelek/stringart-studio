#include "core/OptimizationConfig.h"

#include <cmath>
#include <stdexcept>

namespace sa {

void validateConfig(const OptimizationConfig &config, int pinCount)
{
    if (config.maxSegments < 0)
        throw std::invalid_argument("maxSegments must not be negative");
    if (!(config.maxThreadLengthMm >= 0.0))
        throw std::invalid_argument("maxThreadLengthMm must not be negative");
    if (config.minPinSeparation < 1)
        throw std::invalid_argument("minPinSeparation must be at least 1");
    if (!(config.threadOpacity >= 0.0f && config.threadOpacity <= 1.0f))
        throw std::invalid_argument("threadOpacity must be in [0, 1]");
    if (!(config.threadWidth > 0.0 && config.threadWidth <= 64.0))
        throw std::invalid_argument("threadWidth must be in (0, 64]");
    if (config.startPin < 0 || config.startPin >= pinCount)
        throw std::invalid_argument("startPin is out of range");
    if (!std::isfinite(config.minImprovement))
        throw std::invalid_argument("minImprovement must be finite");
    if (config.threadCount < 0)
        throw std::invalid_argument("threadCount must not be negative");
}

} // namespace sa
