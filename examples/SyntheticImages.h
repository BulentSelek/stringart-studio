#pragma once

#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

// Procedural test images shared by tests and benchmarks. They avoid shipping
// third-party photographs and are fully deterministic.
namespace sa::synthetic {

// A stylised grayscale portrait: light background, dark hair and shoulders,
// shaded face with eyes, brows, nose and mouth.
inline QImage portrait(int size)
{
    QImage image(size, size, QImage::Format_RGB32);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size / 100.0, size / 100.0);

    QLinearGradient background(0, 0, 100, 100);
    background.setColorAt(0, QColor(235, 235, 235));
    background.setColorAt(1, QColor(190, 190, 190));
    p.fillRect(QRectF(0, 0, 100, 100), background);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(40, 40, 40)); // shoulders
    p.drawEllipse(QRectF(10, 78, 80, 50));
    p.setBrush(QColor(30, 30, 30)); // hair
    p.drawEllipse(QRectF(24, 12, 52, 62));

    QRadialGradient skin(QPointF(46, 44), 26);
    skin.setColorAt(0, QColor(205, 205, 205));
    skin.setColorAt(1, QColor(120, 120, 120));
    p.setBrush(skin);
    p.drawEllipse(QRectF(30, 22, 40, 52)); // face
    p.drawRect(QRectF(43, 66, 14, 16));    // neck

    p.setBrush(QColor(25, 25, 25));
    p.drawEllipse(QRectF(37, 41, 9, 4)); // eyes
    p.drawEllipse(QRectF(54, 41, 9, 4));
    p.setPen(QPen(QColor(35, 35, 35), 2.0));
    p.drawLine(QPointF(36, 37), QPointF(46, 36)); // brows
    p.drawLine(QPointF(54, 36), QPointF(64, 37));
    p.setPen(QPen(QColor(90, 90, 90), 1.5));
    p.drawLine(QPointF(50, 46), QPointF(48, 56)); // nose
    p.drawLine(QPointF(48, 56), QPointF(52, 56));
    p.setPen(QPen(QColor(60, 60, 60), 2.0));
    QPainterPath mouth;
    mouth.moveTo(42, 63);
    mouth.quadTo(50, 67, 58, 63);
    p.setBrush(Qt::NoBrush);
    p.drawPath(mouth);
    p.end();
    return image;
}

// Uniform image of the given gray level.
inline QImage solid(int size, int gray)
{
    QImage image(size, size, QImage::Format_RGB32);
    image.fill(QColor(gray, gray, gray));
    return image;
}

// Dark disk on white, a simple shape for small reconstruction tests.
inline QImage disk(int size)
{
    QImage image(size, size, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::black);
    p.drawEllipse(QRectF(size * 0.3, size * 0.3, size * 0.4, size * 0.4));
    p.end();
    return image;
}

} // namespace sa::synthetic
