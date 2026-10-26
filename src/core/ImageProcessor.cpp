#include "core/ImageProcessor.h"

#include "core/Compositing.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace sa {

namespace {

const std::array<float, 256> &srgbToLinearTable()
{
    static const std::array<float, 256> table = [] {
        std::array<float, 256> t{};
        for (int i = 0; i < 256; ++i)
            t[i] = srgbToLinear(i / 255.0f);
        return t;
    }();
    return table;
}

QRect resolveCrop(const QImage &image, int outWidth, int outHeight, const QRectF &normalizedCrop)
{
    const QRect bounds = image.rect();
    if (!normalizedCrop.isEmpty()) {
        const QRectF r(normalizedCrop.x() * bounds.width(), normalizedCrop.y() * bounds.height(),
                       normalizedCrop.width() * bounds.width(), normalizedCrop.height() * bounds.height());
        const QRect crop = r.toAlignedRect().intersected(bounds);
        if (!crop.isEmpty())
            return crop;
    }
    // Largest centred rectangle with the output aspect ratio.
    const double aspect = static_cast<double>(outWidth) / outHeight;
    int w = bounds.width();
    int h = static_cast<int>(std::lround(w / aspect));
    if (h > bounds.height()) {
        h = bounds.height();
        w = static_cast<int>(std::lround(h * aspect));
    }
    w = std::clamp(w, 1, bounds.width());
    h = std::clamp(h, 1, bounds.height());
    return {(bounds.width() - w) / 2, (bounds.height() - h) / 2, w, h};
}

} // namespace

GrayImage ImageProcessor::prepareBase(const QImage &original, int outWidth, int outHeight,
                                      const QRectF &normalizedCrop)
{
    if (original.isNull() || outWidth <= 0 || outHeight <= 0)
        return {};

    const QRect crop = resolveCrop(original, outWidth, outHeight, normalizedCrop);
    // Composite transparent images onto white, the default canvas colour.
    QImage rgb = original.copy(crop).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (rgb.hasAlphaChannel()) {
        QImage flattened(rgb.size(), QImage::Format_RGB32);
        flattened.fill(Qt::white);
        for (int y = 0; y < rgb.height(); ++y) {
            const auto *src = reinterpret_cast<const QRgb *>(rgb.constScanLine(y));
            auto *dst = reinterpret_cast<QRgb *>(flattened.scanLine(y));
            for (int x = 0; x < rgb.width(); ++x) {
                const QRgb p = src[x];
                const int inv = 255 - qAlpha(p);
                dst[x] = qRgb(qRed(p) + inv, qGreen(p) + inv, qBlue(p) + inv);
            }
        }
        rgb = std::move(flattened);
    }
    const QImage scaled = rgb.scaled(outWidth, outHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                              .convertToFormat(QImage::Format_RGB32);

    const auto &lut = srgbToLinearTable();
    GrayImage out(outWidth, outHeight);
    for (int y = 0; y < outHeight; ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(scaled.constScanLine(y));
        for (int x = 0; x < outWidth; ++x) {
            const QRgb p = line[x];
            const float luminance = 0.2126f * lut[qRed(p)] + 0.7152f * lut[qGreen(p)] + 0.0722f * lut[qBlue(p)];
            out.at(x, y) = linearToSrgb(luminance);
        }
    }
    return out;
}

GrayImage ImageProcessor::applyTone(const GrayImage &base, const ToneSettings &tone)
{
    GrayImage out = base;
    const float contrast = static_cast<float>(std::max(0.0, tone.contrast));
    const float brightness = static_cast<float>(tone.brightness);
    const float invGamma = static_cast<float>(1.0 / std::max(0.01, tone.gamma));
    for (float &v : out.pixels) {
        float t = tone.invert ? 1.0f - v : v;
        t = (t - 0.5f) * contrast + 0.5f + brightness;
        t = std::clamp(t, 0.0f, 1.0f);
        if (invGamma != 1.0f)
            t = std::pow(t, invGamma);
        v = t;
    }
    return out;
}

std::vector<float> ImageProcessor::toLinear(const GrayImage &encoded)
{
    std::vector<float> linear(encoded.pixels.size());
    std::transform(encoded.pixels.begin(), encoded.pixels.end(), linear.begin(),
                   [](float v) { return srgbToLinear(v); });
    return linear;
}

QImage ImageProcessor::toQImage(const GrayImage &encoded)
{
    if (encoded.isNull())
        return {};
    QImage image(encoded.width, encoded.height, QImage::Format_Grayscale8);
    for (int y = 0; y < encoded.height; ++y) {
        uchar *line = image.scanLine(y);
        for (int x = 0; x < encoded.width; ++x)
            line[x] = static_cast<uchar>(std::lround(std::clamp(encoded.at(x, y), 0.0f, 1.0f) * 255.0f));
    }
    return image;
}

std::vector<int> ImageProcessor::histogram(const GrayImage &encoded)
{
    std::vector<int> bins(256, 0);
    for (float v : encoded.pixels)
        ++bins[static_cast<std::size_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f))];
    return bins;
}

} // namespace sa
