#include "net/CncLanSession.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

int main() {
    promptemon::net::CncLanSession first;
    promptemon::net::CncLanSession second;

    if (!first.start()) {
        std::cerr << "first LAN session failed to start\n";
        return 1;
    }
    if (!second.start()) {
        std::cerr << "second LAN session failed to start\n";
        return 2;
    }
    if (first.localPort() == second.localPort()) {
        std::cerr << "sessions bound the same C&C LAN port\n";
        return 3;
    }

    first.setLocalJumpOffset(1.25f);
    second.setLocalJumpOffset(0.0f);

    bool firstSawSecond = false;
    bool secondSawFirstJump = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);

    while (std::chrono::steady_clock::now() < deadline) {
        first.update(.01f);
        second.update(.01f);

        for (const auto& peer : first.peers()) {
            if (peer.id == second.localId()) firstSawSecond = true;
        }
        for (const auto& peer : second.peers()) {
            if (peer.id == first.localId() && std::fabs(peer.jumpOffset - 1.25f) < .001f) {
                secondSawFirstJump = true;
            }
        }

        if (firstSawSecond && secondSawFirstJump) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!firstSawSecond) {
        std::cerr << "first session did not discover second session\n";
        return 4;
    }
    if (!secondSawFirstJump) {
        std::cerr << "second session did not receive first session jump state\n";
        return 5;
    }

    std::cout << "LAN smoke OK: ports " << first.localPort() << " and " << second.localPort() << '\n';
    return 0;
}
