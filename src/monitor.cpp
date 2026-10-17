#include "monitor.h"
#include "policy.h"
#include <QDateTime>
#include <chrono>

using namespace winrt::Windows::Networking::NetworkOperators;
using Clock = std::chrono::steady_clock;

Monitor::Monitor(QString ethernetId, QString tunId, QObject* parent)
    : QThread(parent), ethernetId_(std::move(ethernetId)), tunId_(std::move(tunId)) {}

void Monitor::run() {
    const auto cancelled = [this] { return stopped_.load(); };
    const auto log = [this](const QString& s) { emit logMessage(s); };
    net::Manager manager{nullptr};
    bool restoreTimeout = false, timeoutChanged = false, bound = false, upstreamResetNeeded = false;
    QString boundId;
    int failures = 0;
    auto nextRecovery = Clock::time_point::min();
    try {
        net::Apartment apartment;
        // All WinRT objects are created, used and released on this MTA worker.
        try {
            restoreTimeout = net::Manager::IsNoConnectionsTimeoutEnabled();
            if (restoreTimeout && !cancelled()) {
                timeoutChanged = true; // Also restore after a cancelled/timed-out asynchronous call.
                net::setIdleTimeoutDisabled(true, cancelled, log);
            }
        } catch (const winrt::hresult_error& e) {
            log(QStringLiteral("无法修改热点空闲超时，将继续定时保活：%1").arg(net::errorText(e)));
        } catch (const std::exception& e) {
            log(QStringLiteral("热点空闲超时设置失败：%1").arg(QString::fromUtf8(e.what())));
        }
        log(QStringLiteral("监控已开始：每 30 秒检测国内网络，连续 3 轮失败后修复。"));
        while (!cancelled()) {
            try {
                if (!bound) emit statusChanged(QStringLiteral("正在检查网络与热点…"), false);
                auto profile = net::findTun(tunId_);
                if (!profile) {
                    bound = false;
                    emit statusChanged(QStringLiteral("等待 Clash / Mihomo TUN"), false);
                    log(QStringLiteral("未找到可用的 TUN。请在 Clash Verge 中开启虚拟网卡；30 秒后重试。"));
                    if (!net::pause(30000, cancelled)) break;
                    continue;
                }
                const auto id = net::profileId(profile);
                if (!manager || boundId != id || !bound) {
                    manager = net::Manager::CreateFromConnectionProfile(profile);
                    boundId = id;
                    bound = false;
                    log(QStringLiteral("检测到 TUN：%1").arg(QString::fromWCharArray(profile.ProfileName().c_str())));
                }
                const auto internet = policy::confirmInternet([&](int round) {
                    QString endpoint;
                    const bool online = net::pingDomestic(cancelled, &endpoint);
                    if (!cancelled()) log(online
                        ? QStringLiteral("国内网络正常：%1 响应（第 %2/3 轮）。").arg(endpoint).arg(round)
                        : QStringLiteral("国内网络无响应（第 %1/3 轮）。").arg(round));
                    return online;
                }, [&] { return net::pause(2000, cancelled); }, cancelled);
                if (internet == policy::ProbeResult::Cancelled) break;
                emit checkedAt(QDateTime::currentDateTime().toString("HH:mm:ss"));
                const bool online = internet == policy::ProbeResult::Online;
                const bool hotspotOn = manager.TetheringOperationalState() == TetheringOperationalState::On;
                const auto action = upstreamResetNeeded ? policy::Action::RestartEthernet
                                                        : policy::decide(true, online, hotspotOn && bound);
                if (action == policy::Action::Healthy) {
                    emit statusChanged(QStringLiteral("热点运行中 · 国内网络正常"), true);
                    failures = 0;
                } else if (Clock::now() < nextRecovery) {
                    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(nextRecovery - Clock::now()).count() + 1;
                    emit statusChanged(QStringLiteral("网络或热点异常 · %1 秒后允许重试").arg(seconds), false);
                    log(QStringLiteral("恢复冷却中，约 %1 秒后允许重试，避免频繁重启网卡。").arg(seconds));
                } else {
                    bool ok = true;
                    const bool fullReset = action == policy::Action::RestartEthernet;
                    emit statusChanged(fullReset ? QStringLiteral("正在恢复有线网卡与热点…")
                                                 : QStringLiteral("正在开启 TUN 热点…"), false);
                    if (fullReset) {
                        log(online ? QStringLiteral("Windows 报告共享上游连接受限，执行脚本式网卡和热点恢复。")
                                   : QStringLiteral("连续 3 轮均无法连接国内目标，开始恢复有线网卡和热点。"));
                        // Shut down tethering before the upstream disappears.
                        if (hotspotOn) net::setHotspot(manager, false, cancelled, log);
                        bound = false;
                        ok = net::restartEthernet(ethernetId_, cancelled, log);
                        if (ok) {
                            upstreamResetNeeded = false;
                            log(QStringLiteral("等待有线网络恢复，最长 30 秒…"));
                            bool recovered = false;
                            const auto deadline = Clock::now() + std::chrono::seconds(30);
                            while (Clock::now() < deadline && !cancelled()) {
                                if (net::pingDomestic(cancelled)) { recovered = true; break; }
                                if (!net::pause(1000, cancelled)) break;
                            }
                            if (!recovered && !cancelled()) log(QStringLiteral("网络尚未响应，仍尝试恢复热点，随后继续监控。"));
                            profile = nullptr;
                            for (int i = 0; i < 15 && !cancelled(); ++i) {
                                profile = net::findTun(tunId_);
                                if (profile) break;
                                if (!net::pause(1000, cancelled)) break;
                            }
                            if (profile && !cancelled()) {
                                manager = net::Manager::CreateFromConnectionProfile(profile);
                                boundId = net::profileId(profile);
                            } else { log(QStringLiteral("等待 TUN 恢复超时。")); ok = false; }
                        }
                    }
                    if (ok && !cancelled()) {
                        // Restart an existing hotspot once to bind it to the selected TUN source.
                        if (manager.TetheringOperationalState() != TetheringOperationalState::Off)
                            ok = net::setHotspot(manager, false, cancelled, log);
                        if (ok && net::pause(1000, cancelled)) {
                            log(QStringLiteral("正在以 TUN 作为共享来源开启 Windows 移动热点…"));
                            int windowsStatus = -1;
                            ok = net::setHotspot(manager, true, cancelled, log, &windowsStatus);
                            if (windowsStatus == static_cast<int>(TetheringOperationStatus::NetworkLimitedConnectivity)) {
                                upstreamResetNeeded = true;
                                log(QStringLiteral("Windows 将共享来源标记为受限；冷却后将重启有线网卡再尝试开启热点。"));
                            }
                        } else ok = false;
                        bound = ok;
                    }
                    if (cancelled()) break;
                    const bool verifiedOnline = ok && net::pingDomestic(cancelled);
                    const bool healthy = verifiedOnline && manager.TetheringOperationalState() == TetheringOperationalState::On;
                    if (healthy) {
                        failures = 0;
                        log(QStringLiteral("热点已开启，国内网络验证通过。"));
                        emit statusChanged(QStringLiteral("热点运行中 · 国内网络正常"), true);
                        nextRecovery = Clock::now() + std::chrono::seconds(60);
                    } else {
                        const int delay = policy::retryDelaySeconds(++failures);
                        nextRecovery = Clock::now() + std::chrono::seconds(delay);
                        log(QStringLiteral("尚未恢复正常，将继续监测；%1 秒后允许再次修复。").arg(delay));
                        emit statusChanged(QStringLiteral("网络或热点尚未恢复 · 自动重试中"), false);
                    }
                }
            } catch (const winrt::hresult_error& e) {
                log(QStringLiteral("Windows 网络接口异常：%1").arg(net::errorText(e)));
                emit statusChanged(QStringLiteral("系统接口异常 · 等待重试"), false);
                bound = false;
                nextRecovery = Clock::now() + std::chrono::seconds(policy::retryDelaySeconds(++failures));
            } catch (const std::exception& e) {
                log(QStringLiteral("操作异常：%1").arg(QString::fromUtf8(e.what())));
                emit statusChanged(QStringLiteral("操作异常 · 等待重试"), false);
                nextRecovery = Clock::now() + std::chrono::seconds(policy::retryDelaySeconds(++failures));
            }
            if (!net::pause(30000, cancelled)) break;
        }
        // Deliberate shutdown: close the managed hotspot and restore changed system settings.
        emit statusChanged(QStringLiteral("正在停止热点与监控…"), false);
        const auto cleanupCancelled = [] { return false; };
        try {
            if (manager && manager.TetheringOperationalState() != TetheringOperationalState::Off) {
                log(QStringLiteral("正在关闭本次管理的移动热点…"));
                net::setHotspot(manager, false, cleanupCancelled, log);
            }
        } catch (const winrt::hresult_error& e) { log(QStringLiteral("关闭热点失败：%1").arg(net::errorText(e))); }
          catch (const std::exception& e) { log(QString::fromUtf8(e.what())); }
        try {
            if (timeoutChanged && restoreTimeout) net::setIdleTimeoutDisabled(false, cleanupCancelled, log);
        } catch (const winrt::hresult_error& e) { log(QStringLiteral("还原超时设置失败：%1").arg(net::errorText(e))); }
          catch (const std::exception& e) { log(QString::fromUtf8(e.what())); }
        manager = nullptr;
    } catch (const winrt::hresult_error& e) { log(net::errorText(e)); }
      catch (const std::exception& e) { log(QString::fromUtf8(e.what())); }
    emit statusChanged(QStringLiteral("已关闭"), false);
    log(QStringLiteral("监控已停止。"));
}
