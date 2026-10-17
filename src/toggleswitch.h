#pragma once
#include <QAbstractButton>

class ToggleSwitch final : public QAbstractButton {
    Q_OBJECT
public:
    explicit ToggleSwitch(QWidget* parent = nullptr);
    QSize sizeHint() const override { return {64, 36}; }
protected:
    void paintEvent(QPaintEvent*) override;
    bool hitButton(const QPoint& point) const override { return rect().contains(point); }
};
