#include "powershell.h"
#include <QCoreApplication>
#include <QProcess>
#include <iostream>
#include <stdexcept>

namespace {
const QString target = "{4679d35b-7e44-461a-94dc-6906492b2e70}";

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void check(const QString& candidates, bool success, int restartCount,
           const QString& failureText = {}, bool failRestart = false) {
    // Disable module autoloading: these tests can only call our in-process mocks,
    // never the real NetAdapter cmdlets, even on an administrator's machine.
    const auto mocks = QStringLiteral(
        "Import-Module Microsoft.PowerShell.Utility -ErrorAction Stop; "
        "$PSModuleAutoLoadingPreference='None'; "
        "$script:candidates=@(%1); "
        "function global:Get-NetAdapter { [CmdletBinding()]param([switch]$IncludeHidden) "
        "Write-Progress -Activity '模拟首次模块加载' -Status '准备中'; $script:candidates }; "
        "function global:Restart-NetAdapter { [CmdletBinding(SupportsShouldProcess=$true)] "
        "param([Parameter(ValueFromPipeline=$true)]$InputObject) process { "
        "[Console]::Out.WriteLine('MOCK_RESTART:'+[string]$InputObject.InterfaceGuid); %2 } }; ")
        .arg(candidates, failRestart ? QStringLiteral("throw '模拟驱动拒绝重启。';") : QString());
    QProcess process;
    process.start(qEnvironmentVariable("SystemRoot", "C:/Windows")
        + "/System32/WindowsPowerShell/v1.0/powershell.exe",
        net::powershell::arguments(mocks + net::powershell::restartAdapterScript(target)));
    require(process.waitForStarted(5000), "Could not start Windows PowerShell 5.1");
    if (!process.waitForFinished(15000)) {
        process.kill(); process.waitForFinished(3000);
        throw std::runtime_error("Mock PowerShell command timed out");
    }
    const auto out = QString::fromUtf8(process.readAllStandardOutput());
    const auto err = QString::fromUtf8(process.readAllStandardError());
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != (success ? 0 : 1)) {
        std::cerr << out.toUtf8().constData() << err.toUtf8().constData();
        throw std::runtime_error("Unexpected PowerShell exit status");
    }
    require(out.count("MOCK_RESTART:") == restartCount, "Wrong number of mocked adapter resets");
    require(out.contains("ADAPTER_RESTARTED") == success, "Incorrect recovery success marker");
    require(!out.contains("CLIXML") && !err.contains("CLIXML") && !err.contains("<Objs"),
        "PowerShell progress or errors must not leak CLIXML into logs");
    if (success) require(err.isEmpty(), "Successful recovery unexpectedly wrote an error");
    else require(err.contains(failureText), "Readable UTF-8 error was not preserved");
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        const auto missing = QStringLiteral("找不到唯一匹配的物理有线网卡");
        // Regression: Windows returns this property as a braced, uppercase string.
        check("[pscustomobject]@{InterfaceGuid='{4679D35B-7E44-461A-94DC-6906492B2E70}';HardwareInterface=$true}", true, 1);
        check("[pscustomobject]@{InterfaceGuid='4679d35b-7e44-461a-94dc-6906492b2e70';HardwareInterface=$true}", true, 1);
        check("[pscustomobject]@{InterfaceGuid=[guid]'4679d35b-7e44-461a-94dc-6906492b2e70';HardwareInterface=$true}", true, 1);
        check("[pscustomobject]@{InterfaceGuid='{4679D35B-7E44-461A-94DC-6906492B2E70}';HardwareInterface=$false}", false, 0, missing);
        check("[pscustomobject]@{InterfaceGuid='{00000000-0000-0000-0000-000000000001}';HardwareInterface=$true}", false, 0, missing);
        check("[pscustomobject]@{InterfaceGuid='invalid';HardwareInterface=$true};"
              "[pscustomobject]@{InterfaceGuid=$null;HardwareInterface=$true}", false, 0, missing);
        check("[pscustomobject]@{InterfaceGuid='{4679D35B-7E44-461A-94DC-6906492B2E70}';HardwareInterface=$true};"
              "[pscustomobject]@{InterfaceGuid='4679d35b-7e44-461a-94dc-6906492b2e70';HardwareInterface=$true}", false, 0, missing);
        check("[pscustomobject]@{InterfaceGuid='{4679D35B-7E44-461A-94DC-6906492B2E70}';HardwareInterface=$true}",
              false, 1, QStringLiteral("模拟驱动拒绝重启。"), true);
        bool rejected = false;
        try { net::powershell::restartAdapterScript("invalid-guid"); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Invalid adapter GUID must be rejected before starting PowerShell");
        std::cout << "PowerShell adapter recovery: all mocked-process checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
