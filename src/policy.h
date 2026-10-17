#pragma once
#include <algorithm>

namespace policy {
enum class ProbeResult { Online, Offline, Cancelled };

// A successful round is enough. Cancellation must never be interpreted as an outage.
template<class Probe, class Pause, class Cancelled>
ProbeResult confirmInternet(Probe probe, Pause pause, Cancelled cancelled) {
    for (int round = 1; round <= 3; ++round) {
        if (cancelled()) return ProbeResult::Cancelled;
        if (probe(round)) return cancelled() ? ProbeResult::Cancelled : ProbeResult::Online;
        if (cancelled()) return ProbeResult::Cancelled;
        if (round != 3 && !pause()) return ProbeResult::Cancelled;
    }
    return ProbeResult::Offline;
}

enum class Action { WaitForTun, RestartEthernet, StartHotspot, Healthy };
inline Action decide(bool tunPresent, bool internet, bool hotspotOn) {
    if (!tunPresent) return Action::WaitForTun;
    if (!internet) return Action::RestartEthernet;
    return hotspotOn ? Action::Healthy : Action::StartHotspot;
}
inline int retryDelaySeconds(int consecutiveFailures) {
    return std::min(300, 60 * (1 << std::clamp(consecutiveFailures - 1, 0, 3)));
}
}
