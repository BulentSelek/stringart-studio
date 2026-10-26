#include "core/Compositing.h"
#include "core/ImageProcessor.h"

#include "SyntheticImages.h"

#include <QTest>

#include <cmath>

using namespace sa;

class TestImageProcessor : public QObject
{
    Q_OBJECT

private slots:
    void grayLevels();
    void centerCropKeepsAspect();
    void explicitCrop();
    void toneAdjustments();
    void originalUnchanged();
    void transparentFlattensToWhite();
    void histogram();
    void nullInput();
    void rotation();
    void effectiveCropKeepsAspect();
    void nonSquareOutput();
    void cropBeyondImageIsEmptyCanvas();
    void containingCrop();
};

void TestImageProcessor::grayLevels()
{
    const GrayImage white = ImageProcessor::prepareBase(synthetic::solid(32, 255), 16, 16);
    const GrayImage black = ImageProcessor::prepareBase(synthetic::solid(32, 0), 16, 16);
    const GrayImage gray = ImageProcessor::prepareBase(synthetic::solid(32, 128), 16, 16);
    QCOMPARE(white.width, 16);
    QCOMPARE(white.height, 16);
    for (float v : white.pixels)
        QVERIFY(std::abs(v - 1.0f) < 1e-4f);
    for (float v : black.pixels)
        QVERIFY(std::abs(v) < 1e-4f);
    for (float v : gray.pixels)
        QVERIFY(std::abs(v - 128.0f / 255.0f) < 2e-3f);

    // Pure red has Rec. 709 linear luminance 0.2126.
    QImage red(8, 8, QImage::Format_RGB32);
    red.fill(Qt::red);
    const GrayImage r = ImageProcessor::prepareBase(red, 8, 8);
    QVERIFY(std::abs(r.at(4, 4) - linearToSrgb(0.2126f)) < 1e-3f);
}

void TestImageProcessor::centerCropKeepsAspect()
{
    // Left half black, right half white, wide image: the centred square crop
    // must contain both halves equally.
    QImage wide(400, 100, QImage::Format_RGB32);
    wide.fill(Qt::white);
    for (int y = 0; y < 100; ++y)
        for (int x = 0; x < 200; ++x)
            wide.setPixel(x, y, qRgb(0, 0, 0));
    const GrayImage out = ImageProcessor::prepareBase(wide, 50, 50);
    QVERIFY(out.at(5, 25) < 0.05f);
    QVERIFY(out.at(45, 25) > 0.95f);
}

void TestImageProcessor::explicitCrop()
{
    QImage image(100, 100, QImage::Format_RGB32);
    image.fill(Qt::white);
    for (int y = 0; y < 50; ++y)
        for (int x = 0; x < 50; ++x)
            image.setPixel(x, y, qRgb(0, 0, 0));
    const GrayImage topLeft = ImageProcessor::prepareBase(image, 10, 10, QRectF(0.0, 0.0, 0.5, 0.5));
    for (float v : topLeft.pixels)
        QVERIFY(v < 0.01f);
}

void TestImageProcessor::toneAdjustments()
{
    GrayImage base(3, 1);
    base.pixels = {0.0f, 0.5f, 1.0f};

    const GrayImage inverted = ImageProcessor::applyTone(base, {0.0, 1.0, 1.0, true});
    QCOMPARE(inverted.pixels, (std::vector<float>{1.0f, 0.5f, 0.0f}));

    const GrayImage brighter = ImageProcessor::applyTone(base, {0.2, 1.0, 1.0, false});
    QVERIFY(std::abs(brighter.pixels[0] - 0.2f) < 1e-6f);
    QVERIFY(std::abs(brighter.pixels[1] - 0.7f) < 1e-6f);
    QCOMPARE(brighter.pixels[2], 1.0f);

    const GrayImage contrast = ImageProcessor::applyTone(base, {0.0, 2.0, 1.0, false});
    QCOMPARE(contrast.pixels, (std::vector<float>{0.0f, 0.5f, 1.0f}));
    GrayImage quarter(1, 1);
    quarter.pixels = {0.25f};
    QCOMPARE(ImageProcessor::applyTone(quarter, {0.0, 2.0, 1.0, false}).pixels[0], 0.0f);

    const GrayImage gamma = ImageProcessor::applyTone(base, {0.0, 1.0, 2.0, false});
    QVERIFY(std::abs(gamma.pixels[1] - std::sqrt(0.5f)) < 1e-6f);
}

void TestImageProcessor::originalUnchanged()
{
    const QImage original = synthetic::portrait(128);
    const QImage copy = original.copy();
    const GrayImage base = ImageProcessor::prepareBase(original, 64, 64);
    const GrayImage toned = ImageProcessor::applyTone(base, {0.3, 1.5, 0.7, true});
    QCOMPARE(original, copy);
    QVERIFY(base.pixels != toned.pixels);
}

void TestImageProcessor::transparentFlattensToWhite()
{
    QImage image(10, 10, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    const GrayImage out = ImageProcessor::prepareBase(image, 10, 10);
    for (float v : out.pixels)
        QVERIFY(v > 0.999f);
}

void TestImageProcessor::histogram()
{
    GrayImage image(4, 1);
    image.pixels = {0.0f, 0.0f, 1.0f, 0.5f};
    const std::vector<int> bins = ImageProcessor::histogram(image);
    QCOMPARE(bins.size(), std::size_t{256});
    QCOMPARE(bins[0], 2);
    QCOMPARE(bins[255], 1);
    QCOMPARE(bins[128], 1);
}

void TestImageProcessor::nullInput()
{
    QVERIFY(ImageProcessor::prepareBase(QImage(), 16, 16).isNull());
    QVERIFY(ImageProcessor::toQImage(GrayImage()).isNull());
}

void TestImageProcessor::rotation()
{
    // Black left half: after a 180 degree turn the right half is black.
    QImage image(40, 40, QImage::Format_RGB32);
    image.fill(Qt::white);
    for (int y = 0; y < 40; ++y)
        for (int x = 0; x < 20; ++x)
            image.setPixel(x, y, qRgb(0, 0, 0));
    const GrayImage turned = ImageProcessor::prepareBase(image, 40, 40, {}, 180.0);
    QVERIFY(turned.at(35, 20) < 0.05f);
    QVERIFY(turned.at(5, 20) > 0.95f);
    // 90 degrees clockwise: the black left half moves to the top.
    const GrayImage quarter = ImageProcessor::prepareBase(image, 40, 40, {}, 90.0);
    QVERIFY(quarter.at(20, 5) < 0.05f);
    QVERIFY(quarter.at(20, 35) > 0.95f);
    // 45 degrees expands the canvas; uncovered corners are white.
    const QImage rotated = ImageProcessor::rotated(image, 45.0);
    QVERIFY(rotated.width() > 50);
    const GrayImage diagonal = ImageProcessor::prepareBase(image, 56, 56, {}, 45.0);
    QVERIFY(diagonal.at(0, 0) > 0.95f);
    QCOMPARE(ImageProcessor::rotated(image, 360.0), image);
}

void TestImageProcessor::effectiveCropKeepsAspect()
{
    // Requested 0.5 x 0.5 region of a 400 x 200 image is 200 x 100 px; a
    // square output takes the centred 100 x 100 part of it.
    const QRectF crop = ImageProcessor::effectiveCrop(QSize(400, 200), 64, 64, QRectF(0.0, 0.0, 0.5, 0.5));
    QVERIFY(std::abs(crop.width() * 400 - 100.0) < 1e-9);
    QVERIFY(std::abs(crop.height() * 200 - 100.0) < 1e-9);
    QVERIFY(std::abs(crop.center().x() - 0.25) < 1e-9);
    QVERIFY(std::abs(crop.center().y() - 0.25) < 1e-9);
    // Empty crop: whole image, aspect-fitted.
    const QRectF automatic = ImageProcessor::effectiveCrop(QSize(400, 200), 100, 50, {});
    QCOMPARE(automatic, QRectF(0, 0, 1, 1));
    // Crop outside the image falls back to the whole image.
    const QRectF outside = ImageProcessor::effectiveCrop(QSize(100, 100), 10, 10, QRectF(2, 2, 1, 1));
    QCOMPARE(outside, QRectF(0, 0, 1, 1));
}

void TestImageProcessor::nonSquareOutput()
{
    const GrayImage out = ImageProcessor::prepareBase(synthetic::portrait(300), 120, 60);
    QCOMPARE(out.width, 120);
    QCOMPARE(out.height, 60);
    QCOMPARE(out.pixels.size(), std::size_t{7200});
}

void TestImageProcessor::cropBeyondImageIsEmptyCanvas()
{
    // Black 40 x 40 image; crop twice its size, centred: the image fills the
    // middle half of a 40 x 40 output, the rest is padding.
    const QImage black = synthetic::solid(40, 0);
    const QRectF crop(-0.5, -0.5, 2.0, 2.0);
    QCOMPARE(ImageProcessor::effectiveCrop(black.size(), 40, 40, crop), crop);
    const GrayImage base = ImageProcessor::prepareBase(black, 40, 40, crop);
    QCOMPARE(base.alpha.size(), base.pixels.size());
    QCOMPARE(base.alpha[0], 0.0f);
    QCOMPARE(base.alpha[20 * 40 + 20], 1.0f);
    for (bool invert : {false, true}) {
        const GrayImage toned = ImageProcessor::applyTone(base, {0.0, 1.0, 1.0, invert});
        QVERIFY(toned.alpha.empty());
        QCOMPARE(toned.at(2, 2), 1.0f);   // padding: empty canvas, also when inverted
        QCOMPARE(toned.at(37, 20), 1.0f);
        QVERIFY(std::abs(toned.at(20, 20) - (invert ? 1.0f : 0.0f)) < 1e-3f); // image area
        QVERIFY(toned.at(15, 20) < 0.01f || invert);
    }
    // Opaque crops inside the image carry no alpha.
    QVERIFY(ImageProcessor::prepareBase(black, 20, 20).alpha.empty());
    // Rotation corners count as empty canvas even when inverted.
    const GrayImage rotated = ImageProcessor::applyTone(ImageProcessor::prepareBase(black, 56, 56, {}, 45.0),
                                                        {0.0, 1.0, 1.0, true});
    QVERIFY(rotated.at(0, 0) > 0.99f);
    QVERIFY(rotated.at(28, 28) > 0.99f); // inverted black image
}

void TestImageProcessor::containingCrop()
{
    // Rectangle frame: the whole 400 x 200 image fits a 2:1 canvas exactly
    // (plus the one-pixel pin margin).
    const QRectF rect = ImageProcessor::containingCrop(QSize(400, 200), 202, 101);
    QVERIFY(rect.width() >= 1.0 && rect.height() >= 1.0);
    QVERIFY(std::abs(rect.center().x() - 0.5) < 1e-9 && std::abs(rect.center().y() - 0.5) < 1e-9);
    QVERIFY(std::abs(rect.width() * 400 / (rect.height() * 200) - 2.0) < 1e-9);

    // Ellipse frame: every image corner lies inside the pin ellipse.
    for (const QSize size : {QSize(300, 200), QSize(200, 300), QSize(256, 256)}) {
        const int w = 300;
        const int h = 400;
        const QRectF c = ImageProcessor::containingCrop(size, w, h, true);
        // Image corners in output pixel coordinates.
        const double sx = w / (c.width() * size.width());
        const double sy = h / (c.height() * size.height());
        const double rx = w / 2.0 - 1.0;
        const double ry = h / 2.0 - 1.0;
        for (const QPointF corner : {QPointF(0, 0), QPointF(size.width(), size.height())}) {
            const double x = (corner.x() - c.x() * size.width()) * sx - w / 2.0;
            const double y = (corner.y() - c.y() * size.height()) * sy - h / 2.0;
            const double r = (x / rx) * (x / rx) + (y / ry) * (y / ry);
            QVERIFY2(r <= 1.0 + 1e-9 && r > 0.99, qPrintable(QString::number(r)));
        }
    }
}

QTEST_GUILESS_MAIN(TestImageProcessor)
#include "tst_imageprocessor.moc"
