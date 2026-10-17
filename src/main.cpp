#include "mainwindow.h"
#include "network.h"
#include <windows.h>
#include <shellapi.h>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <QScreen>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

namespace {
int diagnose(const QString& path) {
    QJsonObject report;
    try {
        net::Apartment apartment;
        const auto snapshot = net::snapshot();
        report["error"] = snapshot.error;
        report["administrator"] = net::isAdministrator();
        QJsonArray ethernet, tunnels;
        for (const auto& a : snapshot.ethernet) ethernet.append(QJsonObject{
            {"id", a.id}, {"name", a.name}, {"description", a.description},
            {"up", a.up}, {"gateway", a.gateway}, {"interfaceIndex", int(a.index)}});
        for (const auto& t : snapshot.tunnels) tunnels.append(QJsonObject{
            {"id", t.id}, {"profile", t.name}, {"adapter", t.adapterName}, {"detectedAsClash", t.detected}});
        report["ethernet"] = ethernet;
        report["tunnels"] = tunnels;
        report["defaultEthernet"] = net::defaultEthernet(snapshot.ethernet);
        auto tun = net::findTun();
        report["tunFound"] = bool(tun);
        if (tun) {
            auto manager = net::Manager::CreateFromConnectionProfile(tun);
            report["hotspotState"] = int(manager.TetheringOperationalState());
            report["idleTimeoutEnabled"] = net::Manager::IsNoConnectionsTimeoutEnabled();
        }
    } catch (const winrt::hresult_error& e) { report["error"] = net::errorText(e); }
      catch (const std::exception& e) { report["error"] = QString::fromUtf8(e.what()); }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return 2;
    file.write(QJsonDocument(report).toJson());
    return report["error"].toString().isEmpty() ? 0 : 1;
}
}

int main(int argc, char** argv) {
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    // Qt Widgets exposes signals with registered value types across worker threads.
    QApplication app(argc, argv);
    app.setApplicationName("AutoTunHotspot");
    app.setOrganizationName("AutoTunHotspot");
    app.setApplicationVersion(QString::fromLatin1(AUTOTUNHOTSPOT_VERSION));
    app.setStyle("Fusion");
    QFont uiFont(QStringLiteral("Microsoft YaHei UI"));
    uiFont.setPixelSize(12);
    app.setFont(uiFont);
    app.setQuitOnLastWindowClosed(false);
    const auto arguments = app.arguments();
    if (arguments.size() >= 3 && arguments[1] == "--diagnose") {
        // QApplication initializes COM on the GUI thread. WinRT networking runs in its own MTA.
        return QtConcurrent::run([path = arguments[2]] { return diagnose(path); }).result();
    }
    const bool preview = arguments.contains("--preview") || arguments.contains("--smoke-test");
    if (preview && arguments.size() != 3) return 2;

    // Only the real application elevates. Preview, tests and diagnostics cannot change networking.
    if (!preview && !net::isAdministrator()) {
        const auto executable = QDir::toNativeSeparators(QCoreApplication::applicationFilePath()).toStdWString();
        const auto directory = QDir::toNativeSeparators(QCoreApplication::applicationDirPath()).toStdWString();
        SHELLEXECUTEINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = SEE_MASK_FLAG_NO_UI;
        info.lpVerb = L"runas";
        info.lpFile = executable.c_str();
        info.lpDirectory = directory.c_str();
        info.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&info)) {
            QMessageBox::information(nullptr, QStringLiteral("需要管理员权限"),
                QStringLiteral("控制热点和重启网卡需要管理员权限。此次未获得授权，程序已退出。"));
            return 1;
        }
        return 0;
    }

    QLocalServer server;
    if (!preview && !server.listen("AutoTunHotspot.Windows.v1")) {
        QLocalSocket socket;
        socket.connectToServer("AutoTunHotspot.Windows.v1");
        if (socket.waitForConnected(1500)) {
            socket.write("show"); socket.waitForBytesWritten(1500);
            return 0;
        }
        QMessageBox::warning(nullptr, QStringLiteral("无法启动"), QStringLiteral("无法建立单实例通信通道，请检查是否已有程序正在运行。"));
        return 1;
    }
    MainWindow window(preview);
    QObject::connect(&server, &QLocalServer::newConnection, &window, [&] {
        while (auto* socket = server.nextPendingConnection()) {
            QObject::connect(socket, &QLocalSocket::readyRead, &window, [&, socket] {
                socket->readAll(); window.showWindow(); socket->disconnectFromServer();
            });
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            if (socket->bytesAvailable()) { socket->readAll(); window.showWindow(); socket->disconnectFromServer(); }
        }
    });
    window.show();
    if (preview) {
        QTimer::singleShot(2000, &window, [&] {
            if (arguments[1] == "--smoke-test") app.exit(window.smokeTest(arguments[2]) ? 0 : 1);
            else {
                QJsonObject metrics{
                    {"windowWidth", window.width()}, {"windowHeight", window.height()},
                    {"devicePixelRatio", window.devicePixelRatioF()},
                    {"windowsScale", GetDpiForWindow(reinterpret_cast<HWND>(window.winId())) / 96.0},
                    {"fontPixelSize", window.font().pixelSize()}
                };
                if (const auto* screen = window.screen()) {
                    metrics["availableWidth"] = screen->availableGeometry().width();
                    metrics["availableHeight"] = screen->availableGeometry().height();
                    metrics["logicalDpi"] = screen->logicalDotsPerInch();
                }
                QFile report(arguments[2] + ".json");
                if (report.open(QIODevice::WriteOnly)) report.write(QJsonDocument(metrics).toJson());
                app.exit(window.grab().save(arguments[2]) ? 0 : 1);
            }
        });
    }
    return app.exec();
}
