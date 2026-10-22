#include "net/CncUdpTransport.h"

#include "GameNetwork/udp.h"

#include <limits>

namespace promptemon::net {

struct CncUdpTransport::Impl {
    UDP udp;
};

CncUdpTransport::CncUdpTransport() = default;

CncUdpTransport::~CncUdpTransport() {
    stop();
}

bool CncUdpTransport::start(std::uint16_t port) {
    stop();

    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
    winsockStarted_ = true;

    if (LOBYTE(data.wVersion) != 2 || HIBYTE(data.wVersion) != 2) {
        stop();
        return false;
    }

    impl_ = std::make_unique<Impl>();
    if (impl_->udp.Bind(0u, port) != UDP::OK) {
        stop();
        return false;
    }

    if (!impl_->udp.AllowBroadcasts(true)) {
        stop();
        return false;
    }

    UnsignedInt boundIp = 0;
    UnsignedShort boundPort = 0;
    if (impl_->udp.getLocalAddr(boundIp, boundPort) != UDP::OK) {
        stop();
        return false;
    }

    localPort_ = static_cast<std::uint16_t>(boundPort);
    return true;
}

void CncUdpTransport::stop() {
    impl_.reset();
    localPort_ = 0;
    if (winsockStarted_) {
        WSACleanup();
        winsockStarted_ = false;
    }
}

int CncUdpTransport::send(const void* data,
                          std::size_t size,
                          std::uint32_t hostOrderIp,
                          std::uint16_t port) {
    if (!impl_ || !data || size == 0 || size > std::numeric_limits<UnsignedInt>::max()) return -1;
    return impl_->udp.Write(
        static_cast<const unsigned char*>(data),
        static_cast<UnsignedInt>(size),
        static_cast<UnsignedInt>(hostOrderIp),
        static_cast<UnsignedShort>(port));
}

int CncUdpTransport::receive(void* data,
                             std::size_t capacity,
                             std::uint32_t& hostOrderIp,
                             std::uint16_t& port) {
    hostOrderIp = 0;
    port = 0;
    if (!impl_ || !data || capacity == 0 || capacity > std::numeric_limits<UnsignedInt>::max()) return -1;

    sockaddr_in source{};
    const int received = impl_->udp.Read(
        static_cast<unsigned char*>(data),
        static_cast<UnsignedInt>(capacity),
        &source);
    if (received > 0) {
        hostOrderIp = static_cast<std::uint32_t>(ntohl(source.sin_addr.s_addr));
        port = static_cast<std::uint16_t>(ntohs(source.sin_port));
    }
    return received;
}

bool CncUdpTransport::running() const {
    return impl_ != nullptr;
}

} // namespace promptemon::net
