#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace promptemon::net {

struct LanPeerState {
    std::uint32_t id = 0;
    std::uint8_t slot = 0;
    float jumpOffset = 0.0f;
};

class CncLanSession {
public:
    CncLanSession() = default;
    ~CncLanSession();

    CncLanSession(const CncLanSession&) = delete;
    CncLanSession& operator=(const CncLanSession&) = delete;

    bool start();
    void stop();
    bool update(float dtSeconds);

    void setLocalJumpOffset(float offset) { localJumpOffset_ = offset; }

    bool running() const { return socket_ != invalidSocket(); }
    std::uint32_t localId() const { return localId_; }
    std::uint8_t localSlot() const { return localSlot_; }
    std::uint16_t localPort() const { return localPort_; }
    std::vector<LanPeerState> peers() const;

private:
    struct PeerRecord {
        LanPeerState state;
        std::uint64_t lastSeenMs = 0;
        std::uint32_t sequence = 0;
    };

    static std::uintptr_t invalidSocket();
    void sendState(std::uint8_t type);
    void receivePackets();
    void expirePeers();

    std::uintptr_t socket_ = invalidSocket();
    bool wsaStarted_ = false;
    std::uint32_t localId_ = 0;
    std::uint8_t localSlot_ = 0;
    std::uint16_t localPort_ = 0;
    std::uint32_t sequence_ = 0;
    float localJumpOffset_ = 0.0f;
    float sendAccumulator_ = 0.0f;
    std::unordered_map<std::uint32_t, PeerRecord> peers_;
};

} // namespace promptemon::net
