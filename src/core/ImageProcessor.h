#pragma once

#include "core/GrayImage.h"

#include <QImage>
#include <QRectF>

#include <vector>

namespace sa {

// Tone adjustments applied to display-encoded (sRGB) gray values in [0, 1].
// Order: invert, contrast around mid-gray, brightness offset, gamma.
struct ToneSettings
{
    double brightness = 0.0; // additive offset, typically [-1, 1]
    double contrast = 1.0;   // multiplier around 0.5, >= 0
    double gamma = 1.0;      // v' = v^(1/gamma); > 1 brightens mid-tones
    bool invert = false;

    friend bool operator==(const ToneSettings &, const ToneSettings &) = default;
};

// Non-destructive preprocessing pipeline. The original QImage is never
// modified; each stage returns a new working image:
//
//   original --prepareBase--> base (rotated, cropped, resampled, grayscale)
//            --applyTone----> working image (display-encoded gray), which is
//                             the optimization target (OptimizationProblem)
//
// prepareBase is the expensive step (resampling a large photo), so callers
// cache its result and re-run only applyTone when tone settings change.
class ImageProcessor
{
public:
    // Rotates `original` by rotationDegrees (clockwise, canvas expanded, new
    // corners white), crops it to effectiveCrop(), resamples to
    // outWidth x outHeight and converts to gray using Rec. 709 luminance
    // computed in linear light, re-encoded to sRGB. Returns a null image if
    // `original` is null.
    [[nodiscard]] static GrayImage prepareBase(const QImage &original, int outWidth, int outHeight,
                                               const QRectF &normalizedCrop = {}, double rotationDegrees = 0.0);

    // Clockwise rotation with an expanded canvas; uncovered areas transparent.
    [[nodiscard]] static QImage rotated(const QImage &image, double degrees);

    // The crop actually used, in [0,1] fractions of an image of `imageSize`:
    // the largest rectangle with the output aspect ratio centred inside
    // `normalizedCrop` (or inside the whole image if it is empty), so the
    // working image is never distorted.
    [[nodiscard]] static QRectF effectiveCrop(const QSize &imageSize, int outWidth, int outHeight,
                                              const QRectF &normalizedCrop);

    [[nodiscard]] static GrayImage applyTone(const GrayImage &base, const ToneSettings &tone);

    // Converts display-encoded gray to linear-light values (the transmittance
    // target used by the optimizer).
    [[nodiscard]] static std::vector<float> toLinear(const GrayImage &encoded);

    // 8-bit preview of a display-encoded gray image.
    [[nodiscard]] static QImage toQImage(const GrayImage &encoded);

    // 256-bin histogram of a display-encoded gray image.
    [[nodiscard]] static std::vector<int> histogram(const GrayImage &encoded);
};

} // namespace sa
