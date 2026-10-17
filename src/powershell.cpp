#include "powershell.h"
#include <QUuid>
#include <stdexcept>

namespace net::powershell {
QString restartAdapterScript(const QString& adapterId) {
    const QUuid id(adapterId);
    if (id.isNull()) throw std::invalid_argument("Invalid physical adapter GUID");
    return QStringLiteral(
        "$ErrorActionPreference='Stop'; "
        "$ProgressPreference='SilentlyContinue'; "
        "[Console]::OutputEncoding=[System.Text.UTF8Encoding]::new($false); "
        "try { "
        "$targetGuid=[guid]'%1'; "
        "$a=@(Get-NetAdapter -IncludeHidden -ErrorAction Stop | Where-Object { "
        "$candidateGuid=[guid]::Empty; "
        "$_.HardwareInterface -and "
        "[guid]::TryParse([string]$_.InterfaceGuid,[ref]$candidateGuid) -and "
        "$candidateGuid -eq $targetGuid }); "
        "if($a.Count -ne 1){throw '找不到唯一匹配的物理有线网卡，请刷新网卡后重试。'}; "
        "$a[0] | Restart-NetAdapter -Confirm:$false -ErrorAction Stop; "
        "[Console]::Out.WriteLine('ADAPTER_RESTARTED'); exit 0 "
        "} catch { [Console]::Error.WriteLine($_.Exception.Message); exit 1 }")
        .arg(id.toString());
}

QStringList arguments(const QString& script) {
    const QByteArray utf16(reinterpret_cast<const char*>(script.utf16()), script.size() * 2);
    return {"-NoLogo", "-NoProfile", "-NonInteractive", "-OutputFormat", "Text",
        "-EncodedCommand", QString::fromLatin1(utf16.toBase64())};
}
}
