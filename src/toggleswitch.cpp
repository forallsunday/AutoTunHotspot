#include "toggleswitch.h"
#include <QPainter>

ToggleSwitch::ToggleSwitch(QWidget* parent) : QAbstractButton(parent) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(sizeHint());
    setAccessibleName(QStringLiteral("开启或关闭热点保活"));
    connect(this, &QAbstractButton::toggled, this, QOverload<>::of(&QWidget::update));
}
void ToggleSwitch::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(isChecked() ? QColor("#087CFF") : QColor("#B6BFCE"));
    const QRectF track(1, 1, width() - 2, height() - 2);
    const auto radius = track.height() / 2;
    const auto diameter = height() - 8;
    p.drawRoundedRect(track, radius, radius);
    p.setBrush(Qt::white);
    p.drawEllipse(QRectF(isChecked() ? width() - 4 - diameter : 4, 4, diameter, diameter));
    if (hasFocus()) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor("#6EAAFF"), 2));
        p.drawRoundedRect(track, radius, radius);
    }
}
