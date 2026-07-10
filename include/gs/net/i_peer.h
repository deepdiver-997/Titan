#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace gs {

// Abstract peer connection — swap TCP/QUIC/etc. without touching routing logic.
class IPeer {
public:
    using RecvCallback = std::function<void(const std::vector<uint8_t>&)>;

    virtual ~IPeer() = default;
    virtual void send(const std::vector<uint8_t>& data) = 0;
    virtual void close() = 0;
    virtual void set_recv_callback(RecvCallback cb) = 0;
    virtual std::string remote_addr() const = 0;
};

}  // namespace gs
