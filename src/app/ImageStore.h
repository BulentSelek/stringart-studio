#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>
#include <QString>

#include <memory>

// Thread-safe named image slots shared between the controller, the
// optimization worker and the QML image provider. Images are implicitly
// shared, so set/get only copy a reference, never the pixel buffer.
class ImageStore
{
public:
    void set(const QString &key, const QImage &image);
    [[nodiscard]] QImage get(const QString &key) const;

private:
    mutable QMutex m_mutex;
    QHash<QString, QImage> m_images;
};

// Serves "image://studio/<key>/<revision>". The revision only busts QML's
// cache; the image is looked up by key. Owned by the QML engine.
class StudioImageProvider final : public QQuickImageProvider
{
public:
    explicit StudioImageProvider(std::shared_ptr<const ImageStore> store);

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    std::shared_ptr<const ImageStore> m_store;
};
