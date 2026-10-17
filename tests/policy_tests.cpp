#include "policy.h"
#include <iostream>
#include <stdexcept>

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    try {
        int attempts = 0, pauses = 0;
        auto never = [] { return false; };
        auto failed = policy::confirmInternet([&](int) { ++attempts; return false; },
            [&] { ++pauses; return true; }, never);
        require(failed == policy::ProbeResult::Offline && attempts == 3 && pauses == 2,
            "Must confirm three failed rounds before resetting an adapter");
        attempts = pauses = 0;
        auto recovered = policy::confirmInternet([&](int round) { ++attempts; return round == 2; },
            [&] { ++pauses; return true; }, never);
        require(recovered == policy::ProbeResult::Online && attempts == 2 && pauses == 1,
            "Transient failure must not reset Ethernet");
        attempts = 0;
        auto cancelled = policy::confirmInternet([&](int) { ++attempts; return false; },
            [] { return false; }, never);
        require(cancelled == policy::ProbeResult::Cancelled && attempts == 1,
            "A cancelled wait must not be treated as network failure");
        attempts = 0;
        auto stopped = policy::confirmInternet([&](int) { ++attempts; return false; },
            [] { return true; }, [] { return true; });
        require(stopped == policy::ProbeResult::Cancelled && attempts == 0,
            "Stopping must prevent further probes and recovery");
        require(policy::decide(false, false, false) == policy::Action::WaitForTun,
            "Missing TUN must not trigger an Ethernet reset");
        require(policy::decide(true, true, false) == policy::Action::StartHotspot,
            "A closed hotspot alone must not reset Ethernet");
        require(policy::decide(true, false, true) == policy::Action::RestartEthernet,
            "Loss of Internet must recover upstream even if hotspot stays on");
        require(policy::decide(true, true, true) == policy::Action::Healthy, "Healthy state");
        require(policy::retryDelaySeconds(1) == 60 && policy::retryDelaySeconds(2) == 120
            && policy::retryDelaySeconds(3) == 240 && policy::retryDelaySeconds(4) == 300
            && policy::retryDelaySeconds(1000) == 300, "Recovery retries must back off and remain bounded");
        std::cout << "Recovery policy: all checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
