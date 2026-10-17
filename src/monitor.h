#pragma once
#include "network.h"
#include <QThread>
#include <atomic>

class Monitor final : public QThread {
    Q_OBJECT
public:
    Monitor(QString ethernetId, QString tunId, QObject* parent = nullptr);
    void requestStop() { stopped_.store(true); }
signals:
    void logMessage(const QString& text);
    void statusChanged(const QString& text, bool healthy);
    void checkedAt(const QString& time);
protected:
    void run() override;
private:
    QString ethernetId_, tunId_;
    std::atomic_bool stopped_{false};
};
