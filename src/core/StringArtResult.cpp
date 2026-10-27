#include "core/StringArtResult.h"

namespace sa {

const char *toString(StopReason reason) noexcept
{
    switch (reason) {
    case StopReason::MaxSegments:
        return "maximum segment count reached";
    case StopReason::NoImprovement:
        return "no candidate improves the error";
    case StopReason::NoValidCandidates:
        return "no valid connection from the current pin";
    case StopReason::MaxThreadLength:
        return "maximum thread length reached";
    case StopReason::Cancelled:
        return "cancelled";
    case StopReason::MaxPasses:
        return "maximum refinement passes reached";
    }
    return "unknown";
}

} // namespace sa
