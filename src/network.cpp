#include <winsock2.h>
#include <ws2tcpip.h>
#include "network.h"
#include "powershell.h"
#include <windows.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <QProcess>
#include <QThread>
#include <QUuid>
#include <memory>
#include <vector>

using namespace winrt::Windows::Networking::Connectivity;
using namespace winrt::Windows::Networking::NetworkOperators;
using namespace winrt::Windows::Foundation;

namespace net {
namespace {
QString normalizedId(const QString& value) { return QUuid(value).toString().toLower(); }
bool tunName(const QString& text) {
    return text.contains("mihomo", Qt::CaseInsensitive) || text.contains("clash", Qt::CaseInsensitive)
        || text.contains("metacubex", Qt::CaseInsensitive);
}
template<class Async>
bool waitAsync(const Async& operation, int timeoutMs, const Cancelled& cancelled) {
    for (int elapsed = 0; operation.Status() == AsyncStatus::Started; elapsed += 100) {
        if (cancelled() || elapsed >= timeoutMs) {
            operation.Cancel();
            if (cancelled()) return false;
            throw std::runtime_error("Windows network operation timed out");
        }
        QThread::msleep(100);
    }
    return !cancelled();
}
}

QString errorText(const winrt::hresult_error& e) {
    return QString("%1 (HRESULT 0x%2)").arg(QString::fromWCharArray(e.message().c_str()))
        .arg(static_cast<quint32>(e.code().value), 8, 16, QChar('0'));
}

QVector<Adapter> adapters() {
    ULONG size = 16 * 1024;
    std::vector<unsigned char> storage(size);
    ULONG result = ERROR_BUFFER_OVERFLOW;
    for (int i = 0; i < 3 && result == ERROR_BUFFER_OVERFLOW; ++i) {
        storage.resize(size);
        result = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS,
            nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data()), &size);
    }
    if (result == ERROR_NO_DATA) return {};
    if (result != NO_ERROR) throw std::runtime_error("GetAdaptersAddresses failed");
    QVector<Adapter> out;
    for (auto a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data()); a; a = a->Next) {
        MIB_IF_ROW2 row{};
        row.InterfaceLuid = a->Luid;
        if (GetIfEntry2(&row) != NO_ERROR) continue;
        Adapter item;
        item.id = normalizedId(QString::fromLatin1(a->AdapterName));
        item.name = QString::fromWCharArray(a->FriendlyName);
        item.description = QString::fromWCharArray(a->Description);
        item.index = a->IfIndex;
        item.up = a->OperStatus == IfOperStatusUp;
        item.physical = row.InterfaceAndOperStatusFlags.HardwareInterface != 0;
        item.ethernet = a->IfType == IF_TYPE_ETHERNET_CSMACD && item.physical;
        item.gateway = a->FirstGatewayAddress != nullptr;
        MIB_IPINTERFACE_ROW ip{};
        ip.Family = AF_INET;
        ip.InterfaceLuid = a->Luid;
        if (GetIpInterfaceEntry(&ip) == NO_ERROR) item.metric = ip.Metric;
        out.push_back(item);
    }
    return out;
}

QString profileId(const Profile& profile) {
    if (!profile || !profile.NetworkAdapter()) return {};
    auto text = winrt::to_hstring(profile.NetworkAdapter().NetworkAdapterId());
    return normalizedId(QString::fromWCharArray(text.c_str()));
}

Snapshot snapshot() {
    Snapshot result;
    try {
        const auto all = adapters();
        for (const auto& a : all) if (a.ethernet) result.ethernet.push_back(a);
        for (const auto& p : NetworkInformation::GetConnectionProfiles()) {
            const auto id = profileId(p);
            const auto name = QString::fromWCharArray(p.ProfileName().c_str());
            for (const auto& a : all) {
                if (a.id != id || !a.up || a.physical) continue;
                // Exclude Wi-Fi Direct hotspot interfaces, loopback and known unrelated virtual NICs.
                if (a.description.contains("Wi-Fi Direct", Qt::CaseInsensitive)
                    || a.description.contains("Loopback", Qt::CaseInsensitive)) continue;
                result.tunnels.push_back({id, name, a.name,
                    tunName(name + ' ' + a.name + ' ' + a.description)});
            }
        }
    } catch (const winrt::hresult_error& e) { result.error = errorText(e); }
      catch (const std::exception& e) { result.error = QString::fromUtf8(e.what()); }
    return result;
}

QString defaultEthernet(const QVector<Adapter>& list) {
    // Prefer a connected physical Ethernet interface with a default gateway, then metric.
    const Adapter* best = nullptr;
    for (const auto& a : list) {
        const int rank = (a.up ? 2 : 0) + (a.gateway ? 1 : 0);
        const int bestRank = best ? (best->up ? 2 : 0) + (best->gateway ? 1 : 0) : -1;
        if (!best || rank > bestRank || (rank == bestRank && a.metric < best->metric)) best = &a;
    }
    return best ? best->id : QString();
}

Profile findTun(const QString& preferredId) {
    const auto all = adapters();
    for (const auto& p : NetworkInformation::GetConnectionProfiles()) {
        const auto id = profileId(p);
        const auto name = QString::fromWCharArray(p.ProfileName().c_str());
        for (const auto& a : all) {
            if (a.id != id || !a.up || a.physical) continue;
            if ((!preferredId.isEmpty() && id == normalizedId(preferredId))
                || (preferredId.isEmpty() && tunName(name + ' ' + a.name + ' ' + a.description)))
                return p;
        }
    }
    return nullptr;
}

bool pause(int milliseconds, const Cancelled& cancelled) {
    for (int elapsed = 0; elapsed < milliseconds; elapsed += 100) {
        if (cancelled()) return false;
        QThread::msleep(static_cast<unsigned long>(std::min(100, milliseconds - elapsed)));
    }
    return !cancelled();
}

bool pingDomestic(const Cancelled& cancelled, QString* endpoint) {
    HANDLE handle = IcmpCreateFile();
    if (handle == INVALID_HANDLE_VALUE) return false;
    struct Cleanup { HANDLE h; ~Cleanup() { IcmpCloseHandle(h); } } cleanup{handle};
    const char payload[] = "AutoTunHotspot";
    std::vector<unsigned char> reply(sizeof(ICMP_ECHO_REPLY) + sizeof(payload) + 16);
    for (const auto* ip : {L"223.5.5.5", L"119.29.29.29"}) {
        if (cancelled()) return false;
        IN_ADDR address{};
        InetPtonW(AF_INET, ip, &address);
        const DWORD count = IcmpSendEcho(handle, address.S_un.S_addr,
            const_cast<char*>(payload), sizeof(payload), nullptr,
            reply.data(), static_cast<DWORD>(reply.size()), 1500);
        if (count && reinterpret_cast<ICMP_ECHO_REPLY*>(reply.data())->Status == IP_SUCCESS) {
            if (endpoint) *endpoint = QString::fromWCharArray(ip);
            return true;
        }
    }
    return false;
}

bool restartEthernet(const QString& adapterId, const Cancelled& cancelled, const Log& log) {
    // Resolve the stable GUID again before every reset. Never interpolate a NIC name into code.
    Adapter target;
    bool found = false;
    for (const auto& a : adapters()) if (a.id == normalizedId(adapterId) && a.ethernet) {
        target = a; found = true; break;
    }
    if (!found) { log(QStringLiteral("找不到所选物理有线网卡，取消重启。")); return false; }
    if (!isAdministrator()) { log(QStringLiteral("重启网卡需要管理员权限。")); return false; }
    if (cancelled()) return false;
    log(QStringLiteral("正在重启有线网卡：%1（接口 %2）").arg(target.name).arg(target.index));
    // Compare typed GUIDs in PowerShell too: InterfaceGuid is a string with braces on some drivers.
    const auto script = powershell::restartAdapterScript(adapterId);
    QProcess process;
    const QString powershell = qEnvironmentVariable("SystemRoot", "C:/Windows")
        + "/System32/WindowsPowerShell/v1.0/powershell.exe";
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
    });
    process.start(powershell, net::powershell::arguments(script));
    if (!process.waitForStarted(5000)) { log(QStringLiteral("无法启动系统 PowerShell。")); return false; }
    // Once a reset has begun, let it finish even if Stop is pressed, so we do not kill it between disable/enable.
    int elapsed = 0;
    while (!process.waitForFinished(100)) {
        elapsed += 100;
        if (elapsed >= 45000) {
            process.kill(); process.waitForFinished(3000);
            log(QStringLiteral("重启命令超过 45 秒未完成，请检查 Windows 网卡状态。"));
            return false;
        }
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const auto errors = process.readAllStandardError().trimmed();
        const auto output = errors.isEmpty() ? process.readAllStandardOutput().trimmed() : errors;
        log(QStringLiteral("重启网卡失败（退出码 %1）：%2").arg(process.exitCode())
            .arg(output.isEmpty() ? QStringLiteral("PowerShell 未返回错误详情。")
                                 : QString::fromUtf8(output).left(1500)));
        return false;
    }
    log(QStringLiteral("有线网卡重启完成。"));
    return !cancelled();
}

bool setHotspot(const Manager& manager, bool on, const Cancelled& cancelled, const Log& log, int* windowsStatus) {
    if (windowsStatus) *windowsStatus = -1;
    if (cancelled()) return false;
    for (int i = 0; manager.TetheringOperationalState() == TetheringOperationalState::InTransition; ++i) {
        if (i >= 100) { log(QStringLiteral("热点仍在切换状态，等待下一轮恢复。")); return false; }
        if (!pause(100, cancelled)) return false;
    }
    auto operation = on ? manager.StartTetheringAsync() : manager.StopTetheringAsync();
    if (!waitAsync(operation, 30000, cancelled)) return false;
    auto result = operation.GetResults();
    if (windowsStatus) *windowsStatus = static_cast<int>(result.Status());
    if (result.Status() != TetheringOperationStatus::Success) {
        log(QStringLiteral("热点%1失败：Windows 状态码 %2，%3")
            .arg(on ? QStringLiteral("开启") : QStringLiteral("关闭"))
            .arg(static_cast<int>(result.Status()))
            .arg(QString::fromWCharArray(result.AdditionalErrorMessage().c_str())));
        return false;
    }
    // Success must also be reflected in the current state.
    for (int i = 0; i < 50; ++i) {
        const auto actual = manager.TetheringOperationalState();
        if (actual == (on ? TetheringOperationalState::On : TetheringOperationalState::Off)) return true;
        if (!pause(100, cancelled)) return false;
    }
    log(QStringLiteral("Windows 操作已返回，但热点状态尚未达到预期。"));
    return false;
}

bool setIdleTimeoutDisabled(bool disabled, const Cancelled& cancelled, const Log& log) {
    auto operation = disabled ? Manager::DisableNoConnectionsTimeoutAsync() : Manager::EnableNoConnectionsTimeoutAsync();
    if (!waitAsync(operation, 10000, cancelled)) return false;
    operation.GetResults();
    log(disabled ? QStringLiteral("已关闭“无设备连接时自动关闭热点”超时。")
                 : QStringLiteral("已还原系统的热点空闲超时设置。"));
    return true;
}

bool isAdministrator() {
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    PSID group = nullptr;
    BOOL member = FALSE;
    if (AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &group)) {
        CheckTokenMembership(nullptr, group, &member);
        FreeSid(group);
    }
    return member != FALSE;
}
}
