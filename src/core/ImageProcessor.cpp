#include "core/ImageProcessor.h"

#include "core/Compositing.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <QPainter>
#include <QTransform>

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

// Largest rectangle with the output aspect ratio, centred inside `area`.
QRectF fitAspect(const QRectF &area, double aspect)
{
    double w = area.width();
    double h = w / aspect;
    if (h > area.height()) {
        h = area.height();
        w = h * aspect;
    }
    return {area.center().x() - w / 2.0, area.center().y() - h / 2.0, w, h};
}

} // namespace

QImage ImageProcessor::rotated(const QImage &image, double degrees)
{
    if (image.isNull() || std::abs(std::remainder(degrees, 360.0)) < 1e-9)
        return image;
    // Corners uncovered by the rotation become transparent, which prepareBase
    // treats as empty canvas.
    return image.convertToFormat(QImage::Format_ARGB32_Premultiplied)
        .transformed(QTransform().rotate(degrees), Qt::SmoothTransformation);
}

QRectF ImageProcessor::effectiveCrop(const QSize &imageSize, int outWidth, int outHeight,
                                     const QRectF &normalizedCrop)
{
    if (imageSize.isEmpty() || outWidth <= 0 || outHeight <= 0)
        return {};
    const QRectF bounds(0.0, 0.0, imageSize.width(), imageSize.height());
    QRectF area = bounds;
    if (!normalizedCrop.isEmpty()) {
        const QRectF requested(normalizedCrop.x() * bounds.width(), normalizedCrop.y() * bounds.height(),
                               normalizedCrop.width() * bounds.width(), normalizedCrop.height() * bounds.height());
        // The crop may extend beyond the image (padding); it only has to overlap it.
        if (!requested.intersected(bounds).isEmpty())
            area = requested;
    }
    const QRectF fitted = fitAspect(area, static_cast<double>(outWidth) / outHeight);
    return {fitted.x() / bounds.width(), fitted.y() / bounds.height(), fitted.width() / bounds.width(),
            fitted.height() / bounds.height()};
}

QRectF ImageProcessor::containingCrop(const QSize &imageSize, int outWidth, int outHeight, bool elliptical)
{
    if (imageSize.isEmpty() || outWidth <= 2 || outHeight <= 2)
        return {};
    const double aspect = static_cast<double>(outWidth) / outHeight;
    double w = 0.0;
    if (elliptical) {
        // Ellipse with semi-axes A and A / aspect through the image corners:
        // (W/2A)^2 + (aspect * H/2A)^2 = 1.
        w = 2.0 * std::hypot(imageSize.width() / 2.0, aspect * imageSize.height() / 2.0);
    } else {
        w = std::max<double>(imageSize.width(), imageSize.height() * aspect);
    }
    // Pins sit one output pixel inside the canvas edge.
    w *= std::max(outWidth / (outWidth - 2.0), outHeight / (outHeight - 2.0));
    const double h = w / aspect;
    const double nw = w / imageSize.width();
    const double nh = h / imageSize.height();
    return {(1.0 - nw) / 2.0, (1.0 - nh) / 2.0, nw, nh};
}

QImage ImageProcessor::cropCanvas(const QImage &original, int outWidth, int outHeight, const QRectF &normalizedCrop,
                                  double rotationDegrees)
{
    if (original.isNull() || outWidth <= 0 || outHeight <= 0)
        return {};

    const QImage source = rotated(original, rotationDegrees);
    const QRectF n = effectiveCrop(source.size(), outWidth, outHeight, normalizedCrop);
    const QRectF crop(n.x() * source.width(), n.y() * source.height(), n.width() * source.width(),
                      n.height() * source.height());

    // Resample the part of the image inside the crop (area-averaging scale),
    // then place it on a transparent canvas; the rest of the crop is padding.
    const QRect inside = crop.toAlignedRect().intersected(source.rect());
    const double sx = outWidth / crop.width();
    const double sy = outHeight / crop.height();
    const QRectF target((inside.x() - crop.x()) * sx, (inside.y() - crop.y()) * sy, inside.width() * sx,
                        inside.height() * sy);
    QImage canvas(outWidth, outHeight, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    if (!inside.isEmpty()) {
        const QImage part = source.copy(inside)
                                .convertToFormat(QImage::Format_ARGB32_Premultiplied)
                                .scaled(std::max(1, static_cast<int>(std::lround(target.width()))),
                                        std::max(1, static_cast<int>(std::lround(target.height()))),
                                        Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QPainter painter(&canvas);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(target, part);
    }

    return canvas;
}

GrayImage ImageProcessor::prepareBase(const QImage &original, int outWidth, int outHeight,
                                      const QRectF &normalizedCrop, double rotationDegrees)
{
    const QImage canvas = cropCanvas(original, outWidth, outHeight, normalizedCrop, rotationDegrees);
    if (canvas.isNull())
        return {};

    const auto &lut = srgbToLinearTable();
    GrayImage out(outWidth, outHeight);
    bool translucent = false;
    std::vector<float> alpha(out.pixels.size(), 1.0f);
    for (int y = 0; y < outHeight; ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(canvas.constScanLine(y));
        for (int x = 0; x < outWidth; ++x) {
            const QRgb p = line[x];
            const int a = qAlpha(p);
            const std::size_t i = static_cast<std::size_t>(y) * outWidth + x;
            if (a == 0) {
                out.pixels[i] = 1.0f;
                alpha[i] = 0.0f;
                translucent = true;
                continue;
            }
            auto channel = [a](int c) { return std::min(255, (c * 255 + a / 2) / a); }; // un-premultiply
            const float luminance = 0.2126f * lut[static_cast<std::size_t>(channel(qRed(p)))]
                                  + 0.7152f * lut[static_cast<std::size_t>(channel(qGreen(p)))]
                                  + 0.0722f * lut[static_cast<std::size_t>(channel(qBlue(p)))];
            out.pixels[i] = linearToSrgb(luminance);
            if (a < 255) {
                alpha[i] = a / 255.0f;
                translucent = true;
            }
        }
    }
    if (translucent)
        out.alpha = std::move(alpha);
    return out;
}

RgbImage ImageProcessor::prepareBaseRgb(const QImage &original, int outWidth, int outHeight,
                                        const QRectF &normalizedCrop, double rotationDegrees)
{
    const QImage canvas = cropCanvas(original, outWidth, outHeight, normalizedCrop, rotationDegrees);
    if (canvas.isNull())
        return {};
    RgbImage out(outWidth, outHeight);
    bool translucent = false;
    std::vector<float> alpha(static_cast<std::size_t>(outWidth) * static_cast<std::size_t>(outHeight), 1.0f);
    for (int y = 0; y < outHeight; ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(canvas.constScanLine(y));
        for (int x = 0; x < outWidth; ++x) {
            const QRgb p = line[x];
            const int a = qAlpha(p);
            const std::size_t i = static_cast<std::size_t>(y) * outWidth + x;
            if (a == 0) {
                for (auto &channel : out.channels)
                    channel[i] = 1.0f;
                alpha[i] = 0.0f;
                translucent = true;
                continue;
            }
            auto unpremultiply = [a](int c) { return std::min(255, (c * 255 + a / 2) / a) / 255.0f; };
            out.channels[0][i] = unpremultiply(qRed(p));
            out.channels[1][i] = unpremultiply(qGreen(p));
            out.channels[2][i] = unpremultiply(qBlue(p));
            if (a < 255) {
                alpha[i] = a / 255.0f;
                translucent = true;
            }
        }
    }
    if (translucent)
        out.alpha = std::move(alpha);
    return out;
}

RgbImage ImageProcessor::applyTone(const RgbImage &base, const ToneSettings &tone)
{
    RgbImage out;
    out.width = base.width;
    out.height = base.height;
    for (std::size_t c = 0; c < 3; ++c) {
        GrayImage plane(base.width, base.height);
        plane.pixels = base.channels[c];
        plane.alpha = base.alpha;
        out.channels[c] = applyTone(plane, tone).pixels;
    }
    return out;
}

QImage ImageProcessor::toQImage(const RgbImage &encoded)
{
    if (encoded.isNull())
        return {};
    QImage image(encoded.width, encoded.height, QImage::Format_RGB32);
    auto byte = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    for (int y = 0; y < encoded.height; ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < encoded.width; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * encoded.width + x;
            line[x] = qRgb(byte(encoded.channels[0][i]), byte(encoded.channels[1][i]), byte(encoded.channels[2][i]));
        }
    }
    return image;
}

GrayImage ImageProcessor::applyTone(const GrayImage &base, const ToneSettings &tone)
{
    GrayImage out = base;
    out.alpha.clear();
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
    // Transparent areas and padding are empty canvas (no thread), whatever the tone.
    if (!base.alpha.empty()) {
        for (std::size_t i = 0; i < out.pixels.size(); ++i)
            out.pixels[i] = base.alpha[i] * out.pixels[i] + (1.0f - base.alpha[i]);
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
