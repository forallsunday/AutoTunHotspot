#pragma once
#include <QString>
#include <QVector>
#include <QMetaType>
#include <functional>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Networking.Connectivity.h>
#include <winrt/Windows.Networking.NetworkOperators.h>

namespace net {
using Profile = winrt::Windows::Networking::Connectivity::ConnectionProfile;
using Manager = winrt::Windows::Networking::NetworkOperators::NetworkOperatorTetheringManager;
using Cancelled = std::function<bool()>;
using Log = std::function<void(const QString&)>;

struct Apartment {
    Apartment() { winrt::init_apartment(winrt::apartment_type::multi_threaded); }
    ~Apartment() { winrt::uninit_apartment(); }
    Apartment(const Apartment&) = delete;
    Apartment& operator=(const Apartment&) = delete;
};
struct Adapter {
    QString id, name, description;
    unsigned long index = 0;
    bool up = false, physical = false, ethernet = false, gateway = false;
    unsigned long metric = 0xffffffff;
};
struct Tun {
    QString id, name, adapterName;
    bool detected = false;
};
struct Snapshot {
    QVector<Adapter> ethernet;
    QVector<Tun> tunnels;
    QString error;
};
QVector<Adapter> adapters();
Snapshot snapshot();
QString defaultEthernet(const QVector<Adapter>& list);
Profile findTun(const QString& preferredId = {});
QString profileId(const Profile& profile);
QString errorText(const winrt::hresult_error& e);
bool pingDomestic(const Cancelled& cancelled, QString* endpoint = nullptr);
bool pause(int milliseconds, const Cancelled& cancelled);
bool restartEthernet(const QString& adapterId, const Cancelled& cancelled, const Log& log);
bool setHotspot(const Manager& manager, bool on, const Cancelled& cancelled, const Log& log, int* windowsStatus = nullptr);
bool setIdleTimeoutDisabled(bool disabled, const Cancelled& cancelled, const Log& log);
bool isAdministrator();
}
Q_DECLARE_METATYPE(net::Snapshot)
