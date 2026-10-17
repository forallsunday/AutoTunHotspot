#pragma once
#include "network.h"
#include <QMainWindow>
#include <QFutureWatcher>
#include <QFile>
#include <QSettings>
#include <QSystemTrayIcon>
#include <QTimer>
#include <memory>

class QComboBox;
class QLabel;
class QTextBrowser;
class QPushButton;
class ToggleSwitch;
class Monitor;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(bool preview = false);
    ~MainWindow() override;
    void showWindow();
    void requestExit();
    bool smokeTest(const QString& output);
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    void refreshAdapters();
    void applySnapshot(const net::Snapshot& snapshot);
    void toggleMonitoring(bool enabled);
    void appendLog(const QString& message);
    void updateStatus(const QString& message, bool healthy);
    void saveSelection();
    QComboBox *ethernet_, *tun_;
    QLabel *tunStatus_, *state_, *lastCheck_, *headerIcon_;
    QTextBrowser* logs_;
    ToggleSwitch* toggle_;
    QPushButton* refresh_;
    QSystemTrayIcon* tray_;
    QFutureWatcher<net::Snapshot> watcher_;
    QTimer refreshTimer_;
    Monitor* monitor_ = nullptr;
    std::unique_ptr<QSettings> settings_;
    QFile logFile_;
    QString dataDir_;
    bool preview_ = false, quitting_ = false, hiddenNotice_ = false, healthy_ = false;
};
