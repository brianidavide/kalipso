// SPDX-License-Identifier: AGPL-3.0-or-later
#include "icons.h"
#include <QPainter>
#include <QSvgRenderer>

namespace Icons {
namespace {
QPixmap tinted(QSvgRenderer &renderer, int size, const QColor &color) {
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    renderer.render(&painter);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(image.rect(), color);
    painter.end();
    return QPixmap::fromImage(image);
}
} // namespace

QIcon icon(const QString &name, const QColor &color, const QColor &disabled) {
    QSvgRenderer renderer(QStringLiteral(":/icons/%1.svg").arg(name));
    QIcon result;
    if (!renderer.isValid())
        return result;
    // Sizes for 100% to 300% displays; Qt picks the closest.
    for (const int size : {16, 20, 24, 32, 40, 48, 64, 72}) {
        result.addPixmap(tinted(renderer, size, color), QIcon::Normal);
        if (disabled.isValid())
            result.addPixmap(tinted(renderer, size, disabled), QIcon::Disabled);
    }
    return result;
}
} // namespace Icons
