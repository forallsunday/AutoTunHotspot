#include "icons.h"
#include <QPainter>
#include <QSvgRenderer>

QImage hotspotImage(int size, bool healthy) {
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    QSvgRenderer svg(QStringLiteral(":/icons/hotspot.svg"));
    svg.render(&painter, QRectF(size * .04, size * .04, size * .92, size * .92));
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    if (healthy) {
        QLinearGradient gradient(0, 0, size, size);
        gradient.setColorAt(0, QColor("#086BFF"));
        gradient.setColorAt(1, QColor("#00BDA5"));
        painter.fillRect(image.rect(), gradient);
    } else painter.fillRect(image.rect(), QColor("#929BAA"));
    return image;
}
QIcon hotspotIcon(bool healthy) {
    QIcon icon;
    for (const int size : {16, 20, 24, 32, 48, 64, 128, 256})
        icon.addPixmap(QPixmap::fromImage(hotspotImage(size, healthy)));
    return icon;
}
