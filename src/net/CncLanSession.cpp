#include "net/CncLanSession.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <random>

namespace promptemon::net {
namespace {

constexpr std::uint16_t kBasePort = 8088;
constexpr std::uint8_t kMaxSlots = 8;
constexpr std::uint16_t kPromptemonMagic = 0x504D;
constexpr std::uint8_t kProtocolVersion = 1;
constexpr std::uint8_t kStatePacket = 1;
constexpr std::uint8_t kGoodbyePacket = 2;
constexpr float kSendInterval = 1.0f / 30.0f;
constexpr std::uint64_t kPeerTimeoutMs = 2500;
constexpr std::uint32_t kBroadcastIp = 0xFFFFFFFFu;
constexpr std::uint32_t kLoopbackIp = 0x7F000001u;

#pragma pack(push, 1)
struct LanPacket {
    std::uint32_t crc = 0;
    std::uint16_t magic = kPromptemonMagic;
    std::uint8_t version = kProtocolVersion;
    std::uint8_t type = kStatePacket;
    std::uint32_t playerId = 0;
    std::uint32_t sequence = 0;
    std::uint16_t port = 0;
    std::uint8_t slot = 0;
    std::uint8_t reserved = 0;
    float jumpOffset = 0.0f;
};
#pragma pack(pop)

std::uint64_t nowMs() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

std::uint32_t packetChecksum(const LanPacket& packet) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(&packet) + sizeof(packet.crc);
    const std::size_t count = sizeof(packet) - sizeof(packet.crc);
    std::uint32_t hash = 2166136261u;
    for (std::size_t i = 0; i < count; ++i) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

} // namespace

CncLanSession::~CncLanSession() {
    stop();
}

bool CncLanSession::start() {
    stop();

    bool bound = false;
    for (std::uint8_t slot = 0; slot < kMaxSlots; ++slot) {
        const std::uint16_t port = static_cast<std::uint16_t>(kBasePort + slot);
        if (transport_.start(port)) {
            localSlot_ = slot;
            localPort_ = transport_.localPort();
            bound = true;
            break;
        }
    }
    if (!bound) return false;

    std::random_device rd;
    localId_ = (static_cast<std::uint32_t>(rd()) << 1u) ^
               static_cast<std::uint32_t>(nowMs()) ^
               (static_cast<std::uint32_t>(localPort_) << 16u);
    if (localId_ == 0) localId_ = 1;

    sequence_ = 0;
    localJumpOffset_ = 0.0f;
    sendAccumulator_ = kSendInterval;
    peers_.clear();
    sendState(kStatePacket);
    return true;
}

void CncLanSession::stop() {
    if (transport_.running()) {
        sendState(kGoodbyePacket);
        transport_.stop();
    }
    peers_.clear();
    localId_ = 0;
    localPort_ = 0;
    localSlot_ = 0;
    sequence_ = 0;
    sendAccumulator_ = 0.0f;
}

bool CncLanSession::update(float dtSeconds) {
    if (!running()) return false;

    const std::size_t before = peers_.size();
    receivePackets();
    expirePeers();

    sendAccumulator_ += std::max(0.0f, dtSeconds);
    if (sendAccumulator_ >= kSendInterval) {
        sendAccumulator_ = 0.0f;
        sendState(kStatePacket);
    }
    return peers_.size() != before;
}

void CncLanSession::sendState(std::uint8_t type) {
    if (!running()) return;

    LanPacket packet{};
    packet.type = type;
    packet.playerId = localId_;
    packet.sequence = ++sequence_;
    packet.port = localPort_;
    packet.slot = localSlot_;
    packet.jumpOffset = localJumpOffset_;
    packet.crc = packetChecksum(packet);

    const std::array<std::uint32_t, 2> targets = {kBroadcastIp, kLoopbackIp};
    for (const std::uint32_t target : targets) {
        for (std::uint8_t slot = 0; slot < kMaxSlots; ++slot) {
            const std::uint16_t port = static_cast<std::uint16_t>(kBasePort + slot);
            if (target == kLoopbackIp && port == localPort_) continue;
            transport_.send(&packet, sizeof(packet), target, port);
        }
    }
}

void CncLanSession::receivePackets() {
    for (int iteration = 0; iteration < 128; ++iteration) {
        LanPacket packet{};
        std::uint32_t sourceIp = 0;
        std::uint16_t sourcePort = 0;
        const int received = transport_.receive(
            &packet, sizeof(packet), sourceIp, sourcePort);
        if (received <= 0) break;
        if (received != static_cast<int>(sizeof(packet))) continue;
        if (packet.magic != kPromptemonMagic || packet.version != kProtocolVersion) continue;
        if (packet.crc != packetChecksum(packet)) continue;
        if (packet.playerId == 0 || packet.playerId == localId_) continue;
        if (packet.slot >= kMaxSlots) continue;

        if (packet.type == kGoodbyePacket) {
            peers_.erase(packet.playerId);
            continue;
        }
        if (packet.type != kStatePacket) continue;

        auto& peer = peers_[packet.playerId];
        if (peer.lastSeenMs != 0 && packet.sequence < peer.sequence) continue;
        peer.state.id = packet.playerId;
        peer.state.slot = packet.slot;
        peer.state.jumpOffset = packet.jumpOffset;
        peer.sequence = packet.sequence;
        peer.lastSeenMs = nowMs();
    }
}

void CncLanSession::expirePeers() {
    const std::uint64_t now = nowMs();
    for (auto it = peers_.begin(); it != peers_.end();) {
        if (now - it->second.lastSeenMs > kPeerTimeoutMs) it = peers_.erase(it);
        else ++it;
    }
}

std::vector<LanPeerState> CncLanSession::peers() const {
    std::vector<LanPeerState> result;
    result.reserve(peers_.size());
    for (const auto& entry : peers_) result.push_back(entry.second.state);
    std::sort(result.begin(), result.end(), [](const LanPeerState& a, const LanPeerState& b) {
        if (a.slot != b.slot) return a.slot < b.slot;
        return a.id < b.id;
    });
    return result;
}

} // namespace promptemon::net
