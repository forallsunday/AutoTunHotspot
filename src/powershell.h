#pragma once
#include <QString>
#include <QStringList>

namespace net::powershell {
// Shared by the recovery path and the process-level regression tests.
QString restartAdapterScript(const QString& adapterId);
QStringList arguments(const QString& script);
}
