#include "core/Compositing.h"

#include <array>

namespace sa {

const float *SrgbEncoder::table() noexcept
{
    // One extra entry so that encode(1.0f) can interpolate with i == kSize.
    static const std::array<float, kSize + 2> values = [] {
        std::array<float, kSize + 2> t{};
        for (int i = 0; i <= kSize; ++i)
            t[static_cast<std::size_t>(i)] = linearToSrgb(static_cast<float>(i) / kSize);
        t[kSize + 1] = t[kSize];
        return t;
    }();
    return values.data();
}

} // namespace sa
