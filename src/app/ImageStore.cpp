#include "ImageStore.h"

#include <QMutexLocker>

void ImageStore::set(const QString &key, const QImage &image)
{
    const QMutexLocker lock(&m_mutex);
    m_images.insert(key, image);
}

QImage ImageStore::get(const QString &key) const
{
    const QMutexLocker lock(&m_mutex);
    return m_images.value(key);
}

StudioImageProvider::StudioImageProvider(std::shared_ptr<const ImageStore> store)
    : QQuickImageProvider(QQuickImageProvider::Image), m_store(std::move(store))
{
}

QImage StudioImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    QImage image = m_store->get(id.section(u'/', 0, 0));
    if (image.isNull()) {
        image = QImage(1, 1, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
    }
    if (size)
        *size = image.size();
    if (requestedSize.isValid() && (requestedSize.width() < image.width() || requestedSize.height() < image.height()))
        image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return image;
}
