#include "mainwindow.h"
#include "monitor.h"
#include "toggleswitch.h"
#include "icons.h"
#include <QtConcurrent/QtConcurrentRun>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QCursor>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QScreen>
#include <QSignalBlocker>
#include <QTextBrowser>
#include <QTextDocument>
#include <QUrl>

namespace {
QPixmap headerPixmap(const QWidget* widget, bool healthy) {
    const auto ratio = widget->devicePixelRatioF();
    auto pixmap = QPixmap::fromImage(hotspotImage(qRound(40 * ratio), healthy));
    pixmap.setDevicePixelRatio(ratio);
    return pixmap;
}
}

MainWindow::MainWindow(bool preview) : preview_(preview) {
    setWindowTitle(QStringLiteral("TUN 热点保活"));
    auto* screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();
    const auto available = screen ? screen->availableGeometry() : QRect(0, 0, 1280, 720);
    const int maxWidth = qMax(320, available.width() - 32);
    const int maxHeight = qMax(320, available.height() - 64);
    setMinimumSize(qMin(540, maxWidth), qMin(480, maxHeight));
    resize(qMin(620, maxWidth), qMin(580, maxHeight));
    move(available.center() - rect().center());
    setWindowIcon(hotspotIcon(false));
    dataDir_ = QApplication::applicationDirPath() + "/data";
    if (!preview_) {
        QDir().mkpath(dataDir_);
        settings_ = std::make_unique<QSettings>(dataDir_ + "/settings.ini", QSettings::IniFormat);
        settings_->setIniCodec("UTF-8");
        logFile_.setFileName(dataDir_ + "/hotspot.log");
        if (logFile_.size() > 2 * 1024 * 1024) {
            QFile::remove(dataDir_ + "/hotspot.previous.log");
            QFile::rename(logFile_.fileName(), dataDir_ + "/hotspot.previous.log");
        }
        logFile_.open(QIODevice::Append | QIODevice::Text);
    }
    setStyleSheet(QStringLiteral(
        "QMainWindow { background: #F3F6FB; }"
        "QWidget { font-family: 'Microsoft YaHei UI'; color: #202D43; font-size: 12px; }"
        "QFrame#card { background: white; border: 1px solid #E2E8F1; border-radius: 10px; }"
        "QLabel#title { font-size: 20px; font-weight: 700; color: #162A48; }"
        "QLabel#muted { color: #78849A; }"
        "QLabel#section { font-size: 13px; font-weight: 600; }"
        "QComboBox { background: #F8FAFD; border: 1px solid #DCE4EF; border-radius: 6px; padding: 5px 8px; min-height: 18px; }"
        "QComboBox:disabled { color: #8792A4; background: #F2F4F7; }"
        "QComboBox::drop-down { border: none; width: 26px; }"
        "QComboBox::down-arrow { image: url(:/icons/chevron.svg); width: 12px; height: 12px; }"
        "QPushButton { background: white; border: 1px solid #DCE4EF; border-radius: 6px; padding: 5px 10px; }"
        "QPushButton:hover { background: #EBF3FF; border-color: #B1CDFA; }"
        "QPushButton:disabled { color: #A3ACBA; }"
        "QTextBrowser { background: #F8FAFD; border: 1px solid #E5EBF3; border-radius: 6px; padding: 6px; font-family: 'Consolas', 'Microsoft YaHei UI'; font-size: 11px; }"
        "QMenu { background: white; padding: 5px; } QMenu::item { padding: 7px 25px; }"
        "QMenu::item:selected { background: #EAF3FF; }"));

    auto* body = new QWidget(this);
    setCentralWidget(body);
    auto* root = new QVBoxLayout(body);
    root->setContentsMargins(18, 16, 18, 12);
    root->setSpacing(10);
    auto* header = new QHBoxLayout;
    headerIcon_ = new QLabel;
    headerIcon_->setPixmap(headerPixmap(this, false));
    header->addWidget(headerIcon_);
    auto* titles = new QVBoxLayout;
    auto* title = new QLabel(QStringLiteral("TUN 热点保活")); title->setObjectName("title");
    auto* subtitle = new QLabel(QStringLiteral("连接 Mihomo，让 Windows 热点持续在线")); subtitle->setObjectName("muted");
    titles->addWidget(title); titles->addWidget(subtitle); header->addLayout(titles); header->addStretch();
    auto* badge = new QLabel(QStringLiteral("CLASH VERGE"));
    badge->setStyleSheet("color: #2874D6; background: #E6F0FF; padding: 5px 8px; border-radius: 8px; font-size: 10px; font-weight: 600;");
    badge->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    header->addWidget(badge); root->addLayout(header);

    auto card = [](QWidget* parent) { auto* f = new QFrame(parent); f->setObjectName("card"); return f; };
    auto* networkCard = card(body);
    auto* networkLayout = new QVBoxLayout(networkCard);
    networkLayout->setContentsMargins(14, 12, 14, 12);
    auto* networkHeader = new QHBoxLayout;
    auto* netTitle = new QLabel(QStringLiteral("网络来源")); netTitle->setObjectName("section");
    refresh_ = new QPushButton(QStringLiteral("刷新网卡"));
    networkHeader->addWidget(netTitle); networkHeader->addStretch(); networkHeader->addWidget(refresh_);
    networkLayout->addLayout(networkHeader);
    auto* form = new QFormLayout;
    form->setVerticalSpacing(8);
    ethernet_ = new QComboBox; ethernet_->setMinimumContentsLength(18);
    ethernet_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    tun_ = new QComboBox;
    tun_->addItem(QStringLiteral("自动检测 Clash / Mihomo"), QString());
    form->addRow(QStringLiteral("有线网卡"), ethernet_);
    form->addRow(QStringLiteral("TUN 虚拟网卡"), tun_);
    networkLayout->addLayout(form);
    tunStatus_ = new QLabel(QStringLiteral("正在检测网络适配器…"));
    tunStatus_->setObjectName("muted"); tunStatus_->setWordWrap(true);
    networkLayout->addWidget(tunStatus_); root->addWidget(networkCard);

    auto* controlCard = card(body);
    auto* controlLayout = new QHBoxLayout(controlCard);
    controlLayout->setContentsMargins(16, 12, 16, 12);
    auto* controlText = new QVBoxLayout;
    auto* controlTitle = new QLabel(QStringLiteral("热点与自动恢复")); controlTitle->setObjectName("section");
    state_ = new QLabel(QStringLiteral("保活未开启"));
    state_->setWordWrap(true);
    state_->setStyleSheet("color: #78849A;");
    controlText->addWidget(controlTitle); controlText->addWidget(state_);
    controlLayout->addLayout(controlText); controlLayout->addStretch();
    toggle_ = new ToggleSwitch;
    toggle_->setEnabled(!preview_);
    controlLayout->addWidget(toggle_); root->addWidget(controlCard);

    auto* info = new QHBoxLayout;
    auto* cadence = new QLabel(QStringLiteral("30 秒检测一次  ·  连续 3 轮失败后恢复")); cadence->setObjectName("muted");
    lastCheck_ = new QLabel(QStringLiteral("上次检测：—")); lastCheck_->setObjectName("muted");
    info->addWidget(cadence); info->addStretch(); info->addWidget(lastCheck_); root->addLayout(info);

    auto* logCard = card(body);
    auto* logLayout = new QVBoxLayout(logCard); logLayout->setContentsMargins(12, 10, 12, 12);
    auto* logHeader = new QHBoxLayout;
    auto* logTitle = new QLabel(QStringLiteral("运行日志")); logTitle->setObjectName("section");
    auto* openLog = new QPushButton(QStringLiteral("打开日志目录"));
    auto* clearLog = new QPushButton(QStringLiteral("清空显示"));
    logHeader->addWidget(logTitle); logHeader->addStretch(); logHeader->addWidget(openLog); logHeader->addWidget(clearLog);
    logLayout->addLayout(logHeader);
    logs_ = new QTextBrowser;
    logs_->setOpenExternalLinks(false);
    logs_->document()->setMaximumBlockCount(1000);
    logLayout->addWidget(logs_); root->addWidget(logCard, 1);
    auto* footer = new QLabel(QStringLiteral("关闭窗口后继续在托盘运行  ·  右键托盘图标 → 退出"));
    footer->setObjectName("muted"); footer->setAlignment(Qt::AlignCenter); root->addWidget(footer);

    tray_ = new QSystemTrayIcon(hotspotIcon(false), this);
    auto* trayMenu = new QMenu(this);
    trayMenu->addAction(QStringLiteral("显示"), this, &MainWindow::showWindow);
    trayMenu->addSeparator();
    trayMenu->addAction(QStringLiteral("退出"), this, &MainWindow::requestExit);
    tray_->setContextMenu(trayMenu);
    tray_->setToolTip(QStringLiteral("TUN 热点保活 · 保活未开启"));
    connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick) showWindow();
    });
    if (!preview_) tray_->show();

    connect(refresh_, &QPushButton::clicked, this, &MainWindow::refreshAdapters);
    connect(clearLog, &QPushButton::clicked, logs_, &QTextBrowser::clear);
    connect(openLog, &QPushButton::clicked, this, [this] { if (!preview_) QDesktopServices::openUrl(QUrl::fromLocalFile(dataDir_)); });
    connect(toggle_, &QAbstractButton::toggled, this, &MainWindow::toggleMonitoring);
    connect(&watcher_, &QFutureWatcher<net::Snapshot>::finished, this, [this] {
        applySnapshot(watcher_.result());
        refresh_->setEnabled(!monitor_ && !quitting_);
    });
    connect(ethernet_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { saveSelection(); });
    connect(tun_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        saveSelection(); refreshAdapters();
    });
    refreshTimer_.setInterval(10000);
    connect(&refreshTimer_, &QTimer::timeout, this, [this] { if (!monitor_) refreshAdapters(); });
    refreshTimer_.start();
    appendLog(QStringLiteral("程序已就绪。启用开关后开启热点并自动恢复；关闭窗口会隐藏到托盘。"));
    appendLog(QStringLiteral("检测目标：阿里 DNS 223.5.5.5 / 腾讯 DNS 119.29.29.29，仅判断国内网络连通性。"));
    if (preview_) appendLog(QStringLiteral("只读预览模式：不会修改网卡、热点或系统设置。"));
    else if (!logFile_.isOpen()) appendLog(QStringLiteral("无法写入日志文件，请将程序放在可写目录；窗口日志仍可使用。"));
    QTimer::singleShot(0, this, &MainWindow::refreshAdapters);
}

MainWindow::~MainWindow() {
    if (monitor_) { monitor_->requestStop(); monitor_->wait(); }
}

void MainWindow::appendLog(const QString& message) {
    const QString line = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") + "  " + message;
    const bool atBottom = logs_->verticalScrollBar()->value() >= logs_->verticalScrollBar()->maximum() - 4;
    logs_->append(line.toHtmlEscaped());
    if (atBottom) logs_->verticalScrollBar()->setValue(logs_->verticalScrollBar()->maximum());
    if (logFile_.isOpen()) {
        if (logFile_.size() > 2 * 1024 * 1024) {
            logFile_.close();
            QFile::remove(dataDir_ + "/hotspot.previous.log");
            QFile::rename(logFile_.fileName(), dataDir_ + "/hotspot.previous.log");
            logFile_.open(QIODevice::WriteOnly | QIODevice::Text);
        }
        logFile_.write((line + '\n').toUtf8()); logFile_.flush();
    }
}

void MainWindow::updateStatus(const QString& message, bool healthy) {
    state_->setText(message);
    state_->setStyleSheet(healthy ? "color: #078C70;" : "color: #78849A;");
    tray_->setToolTip(QStringLiteral("TUN 热点保活 · ") + message);
    if (healthy_ != healthy) {
        healthy_ = healthy;
        const auto icon = hotspotIcon(healthy);
        tray_->setIcon(icon); setWindowIcon(icon);
        headerIcon_->setPixmap(headerPixmap(this, healthy));
    }
}

void MainWindow::refreshAdapters() {
    if (watcher_.isRunning() || monitor_ || quitting_) return;
    refresh_->setEnabled(false);
    watcher_.setFuture(QtConcurrent::run([] {
        try { net::Apartment apartment; return net::snapshot(); }
        catch (const winrt::hresult_error& e) { net::Snapshot s; s.error = net::errorText(e); return s; }
        catch (const std::exception& e) { net::Snapshot s; s.error = QString::fromUtf8(e.what()); return s; }
    }));
}

void MainWindow::applySnapshot(const net::Snapshot& snapshot) {
    if (monitor_ || quitting_) return;
    if (!snapshot.error.isEmpty()) {
        appendLog(QStringLiteral("TUN 检测失败：") + snapshot.error);
    }
    QString selected = ethernet_->currentData().toString();
    if (selected.isEmpty() && settings_) selected = settings_->value("ethernet").toString();
    const QString previousTun = tun_->currentData().toString();
    const QString chosenTun = settings_ ? settings_->value("tun", previousTun).toString() : previousTun;
    QSignalBlocker blockEthernet(ethernet_), blockTun(tun_);
    ethernet_->clear();
    for (const auto& a : snapshot.ethernet)
        ethernet_->addItem(a.name + (a.up ? QStringLiteral("  · 已连接") : QStringLiteral("  · 未连接")), a.id);
    if (ethernet_->findData(selected) < 0) selected = net::defaultEthernet(snapshot.ethernet);
    ethernet_->setCurrentIndex(ethernet_->findData(selected));
    tun_->clear();
    tun_->addItem(QStringLiteral("自动检测 Clash / Mihomo"), QString());
    QString automatic;
    for (const auto& t : snapshot.tunnels) {
        tun_->addItem(t.name + " (" + t.adapterName + ")", t.id);
        if (t.detected && automatic.isEmpty()) automatic = t.name;
    }
    int ti = tun_->findData(chosenTun);
    if (ti < 0 && !chosenTun.isEmpty()) {
        tun_->addItem(QStringLiteral("所选 TUN 暂不可用（等待恢复）"), chosenTun);
        ti = tun_->count() - 1;
    }
    tun_->setCurrentIndex(ti >= 0 ? ti : 0);
    const bool manual = !tun_->currentData().toString().isEmpty();
    const bool found = manual ? ti >= 1 && ti <= snapshot.tunnels.size() : !automatic.isEmpty();
    const auto description = manual ? tun_->currentText() : automatic;
    tunStatus_->setText(found ? QStringLiteral("● 已检测到 TUN：%1").arg(description)
        : QStringLiteral("● 未检测到可用 TUN；请开启 Clash Verge 虚拟网卡模式。"));
    tunStatus_->setStyleSheet(found ? "color: #078C70;" : "color: #9A6A20;");
    toggle_->setEnabled(!preview_ && ethernet_->count() > 0);
    if (ethernet_->count() == 0) tunStatus_->setText(QStringLiteral("未找到物理有线网卡，请连接网线后刷新。"));
    if (!snapshot.error.isEmpty()) {
        tunStatus_->setText(QStringLiteral("TUN 检测失败：") + snapshot.error);
        tunStatus_->setStyleSheet("color: #9A6A20;");
    }
}

void MainWindow::saveSelection() {
    if (!settings_ || monitor_) return;
    settings_->setValue("ethernet", ethernet_->currentData());
    settings_->setValue("tun", tun_->currentData());
    settings_->sync();
}

void MainWindow::toggleMonitoring(bool enabled) {
    if (preview_) return;
    if (enabled) {
        const auto ethernetId = ethernet_->currentData().toString();
        if (ethernetId.isEmpty() || !net::isAdministrator()) {
            QSignalBlocker block(toggle_); toggle_->setChecked(false);
            appendLog(QStringLiteral("请选择物理有线网卡，并以管理员身份运行程序。")); return;
        }
        saveSelection();
        ethernet_->setEnabled(false); tun_->setEnabled(false); refresh_->setEnabled(false);
        updateStatus(QStringLiteral("正在开启热点与监控…"), false);
        appendLog(QStringLiteral("所选有线网卡：%1").arg(ethernet_->currentText()));
        monitor_ = new Monitor(ethernetId, tun_->currentData().toString(), this);
        connect(monitor_, &Monitor::logMessage, this, &MainWindow::appendLog);
        connect(monitor_, &Monitor::statusChanged, this, &MainWindow::updateStatus);
        connect(monitor_, &Monitor::checkedAt, this, [this](const QString& t) { lastCheck_->setText(QStringLiteral("上次检测：") + t); });
        connect(monitor_, &QThread::finished, this, [this] {
            auto* finished = monitor_; monitor_ = nullptr; finished->deleteLater();
            QSignalBlocker block(toggle_); toggle_->setChecked(false); toggle_->setEnabled(!quitting_);
            ethernet_->setEnabled(!quitting_); tun_->setEnabled(!quitting_); refresh_->setEnabled(!quitting_);
            if (quitting_) { tray_->hide(); qApp->quit(); } else refreshAdapters();
        });
        monitor_->start();
    } else if (monitor_) {
        toggle_->setEnabled(false);
        updateStatus(QStringLiteral("正在停止，请稍候…"), false);
        appendLog(QStringLiteral("收到关闭请求，正在等待当前操作结束并关闭热点。"));
        monitor_->requestStop();
    }
}

void MainWindow::showWindow() { showNormal(); raise(); activateWindow(); }
void MainWindow::closeEvent(QCloseEvent* event) {
    event->ignore();
    if (quitting_) return;
    if (preview_ || QSystemTrayIcon::isSystemTrayAvailable()) {
        hide();
        if (!preview_ && !hiddenNotice_) {
            hiddenNotice_ = true;
            tray_->showMessage(QStringLiteral("TUN 热点保活"), QStringLiteral("程序继续在后台运行，双击托盘图标可显示窗口。"));
        }
    } else appendLog(QStringLiteral("系统托盘不可用，保持窗口可见；可从系统恢复托盘后继续隐藏。"));
}
void MainWindow::requestExit() {
    if (quitting_) return;
    quitting_ = true;
    refreshTimer_.stop();
    appendLog(QStringLiteral("正在退出程序…"));
    if (monitor_) {
        showWindow(); toggle_->setEnabled(false); monitor_->requestStop();
        updateStatus(QStringLiteral("正在关闭热点，完成后退出…"), false);
    } else { tray_->hide(); qApp->quit(); }
}

bool MainWindow::smokeTest(const QString& output) {
    QJsonObject result;
    result["initialSwitchOff"] = !toggle_->isChecked();
    result["logVisible"] = !logs_->toPlainText().isEmpty();
    result["initialIconGray"] = !healthy_;
    updateStatus(QStringLiteral("测试：热点正常"), true);
    const auto color = windowIcon().pixmap(32).toImage();
    updateStatus(QStringLiteral("测试：国内网络异常"), false);
    result["iconChangesWithInternet"] = color != windowIcon().pixmap(32).toImage();
    close(); result["closeOnlyHides"] = !isVisible();
    tray_->activated(QSystemTrayIcon::DoubleClick);
    result["trayDoubleClickRestoresWindow"] = isVisible();
    close();
    tray_->contextMenu()->actions().first()->trigger();
    result["trayShowActionRestoresWindow"] = isVisible();
    for (int i = 0; i < 1100; ++i) appendLog(QStringLiteral("测试日志 %1").arg(i));
    result["boundedLog"] = logs_->document()->blockCount() <= 1000;
    bool success = true;
    for (auto it = result.begin(); it != result.end(); ++it) success = success && it.value().toBool();
    result["success"] = success;
    QFile report(output);
    if (!report.open(QIODevice::WriteOnly)) return false;
    report.write(QJsonDocument(result).toJson());
    return success;
}
