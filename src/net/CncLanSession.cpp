#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include "net/CncLanSession.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <random>

namespace promptemon::net {
namespace {

constexpr std::uint16_t kBasePort = 8088;
constexpr std::uint8_t kMaxSlots = 8;
constexpr std::uint16_t kGeneralsMagic = 0xF00D;
constexpr std::uint8_t kProtocolVersion = 1;
constexpr std::uint8_t kStatePacket = 1;
constexpr std::uint8_t kGoodbyePacket = 2;
constexpr float kSendInterval = 1.0f / 30.0f;
constexpr std::uint64_t kPeerTimeoutMs = 2500;

#pragma pack(push, 1)
struct LanPacket {
    std::uint32_t crc = 0;
    std::uint16_t magic = kGeneralsMagic;
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

bool wouldBlock() {
    return WSAGetLastError() == WSAEWOULDBLOCK;
}

} // namespace

std::uintptr_t CncLanSession::invalidSocket() {
    return static_cast<std::uintptr_t>(INVALID_SOCKET);
}

CncLanSession::~CncLanSession() {
    stop();
}

bool CncLanSession::start() {
    stop();

    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
    wsaStarted_ = true;

    SOCKET socketHandle = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socketHandle == INVALID_SOCKET) {
        stop();
        return false;
    }
    socket_ = static_cast<std::uintptr_t>(socketHandle);

    BOOL broadcast = TRUE;
    ::setsockopt(socketHandle, SOL_SOCKET, SO_BROADCAST,
                 reinterpret_cast<const char*>(&broadcast), sizeof(broadcast));

    u_long nonBlocking = 1;
    if (::ioctlsocket(socketHandle, FIONBIO, &nonBlocking) != 0) {
        stop();
        return false;
    }

    bool bound = false;
    for (std::uint8_t slot = 0; slot < kMaxSlots; ++slot) {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        address.sin_port = htons(static_cast<u_short>(kBasePort + slot));
        if (::bind(socketHandle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0) {
            localSlot_ = slot;
            localPort_ = static_cast<std::uint16_t>(kBasePort + slot);
            bound = true;
            break;
        }
    }
    if (!bound) {
        stop();
        return false;
    }

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
    if (socket_ != invalidSocket()) {
        sendState(kGoodbyePacket);
        ::closesocket(static_cast<SOCKET>(socket_));
        socket_ = invalidSocket();
    }
    peers_.clear();
    localId_ = 0;
    localPort_ = 0;
    localSlot_ = 0;
    sequence_ = 0;
    sendAccumulator_ = 0.0f;
    if (wsaStarted_) {
        WSACleanup();
        wsaStarted_ = false;
    }
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

    const SOCKET socketHandle = static_cast<SOCKET>(socket_);
    const std::array<std::uint32_t, 2> targets = {
        htonl(INADDR_BROADCAST),
        htonl(INADDR_LOOPBACK)
    };

    for (const std::uint32_t target : targets) {
        for (std::uint8_t slot = 0; slot < kMaxSlots; ++slot) {
            const std::uint16_t port = static_cast<std::uint16_t>(kBasePort + slot);
            if (target == htonl(INADDR_LOOPBACK) && port == localPort_) continue;

            sockaddr_in destination{};
            destination.sin_family = AF_INET;
            destination.sin_addr.s_addr = target;
            destination.sin_port = htons(port);
            ::sendto(socketHandle,
                     reinterpret_cast<const char*>(&packet), sizeof(packet), 0,
                     reinterpret_cast<const sockaddr*>(&destination), sizeof(destination));
        }
    }
}

void CncLanSession::receivePackets() {
    const SOCKET socketHandle = static_cast<SOCKET>(socket_);
    for (int iteration = 0; iteration < 128; ++iteration) {
        LanPacket packet{};
        sockaddr_in source{};
        int sourceLength = sizeof(source);
        const int received = ::recvfrom(socketHandle,
                                        reinterpret_cast<char*>(&packet), sizeof(packet), 0,
                                        reinterpret_cast<sockaddr*>(&source), &sourceLength);
        if (received == SOCKET_ERROR) {
            if (wouldBlock()) break;
            break;
        }
        if (received != sizeof(packet)) continue;
        if (packet.magic != kGeneralsMagic || packet.version != kProtocolVersion) continue;
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
