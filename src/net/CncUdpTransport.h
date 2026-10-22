#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace promptemon::net {

class CncUdpTransport {
public:
    CncUdpTransport();
    ~CncUdpTransport();

    CncUdpTransport(const CncUdpTransport&) = delete;
    CncUdpTransport& operator=(const CncUdpTransport&) = delete;

    bool start(std::uint16_t port);
    void stop();

    int send(const void* data, std::size_t size, std::uint32_t hostOrderIp, std::uint16_t port);
    int receive(void* data, std::size_t capacity, std::uint32_t& hostOrderIp, std::uint16_t& port);

    bool running() const;
    std::uint16_t localPort() const { return localPort_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool winsockStarted_ = false;
    std::uint16_t localPort_ = 0;
};

} // namespace promptemon::net
