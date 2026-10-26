#pragma once

#include "core/PinGeometry.h"

#include <cstdint>
#include <vector>

namespace sa {

struct LineSample
{
    std::uint32_t index; // y * imageWidth + x
    float coverage;      // fraction of the pixel covered, in (0, 1]
};

// Rasterizes an anti-aliased line segment of the given width (in pixels).
//
// The thread is a band of the given width. The segment is walked along its
// major axis one pixel column (or row) at a time; at the column centre the
// band spans a minor-axis interval of length width / cos(angle), and each
// pixel's coverage is the exact overlap of that interval with the pixel.
// This conserves ink regardless of sub-pixel phase: every column receives
// exactly width / cos(angle), so the summed coverage of a segment equals
// width * length up to the end columns. Coverage is capped at 1. The segment
// is clipped to its extent along the major axis (flat caps) and to the image.
//
// To make results independent of direction, callers should pass endpoints in
// a canonical order (see rasterizePinLine). `out` is cleared first.
void rasterizeLine(Vec2 a, Vec2 b, double width, int imageWidth, int imageHeight, std::vector<LineSample> &out);

// Rasterizes the line between two pins, always ordering endpoints by pin index
// so that (a, b) and (b, a) produce bit-identical samples. Pin coordinates are
// multiplied by `scale` (used for high-resolution rendering).
void rasterizePinLine(const PinLayout &layout, int a, int b, double width, int imageWidth, int imageHeight,
                      std::vector<LineSample> &out, double scale = 1.0);

} // namespace sa
