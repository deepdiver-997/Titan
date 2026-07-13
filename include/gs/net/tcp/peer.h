#pragma once

#include "gs/net/i_peer.h"

#include <boost/asio.hpp>

#include <array>
#include <memory>

namespace gs {

// TCP implementation of IPeer. One per remote node.
// Continuously reads length-prefixed messages, calls recv_callback.
class TcpPeer : public IPeer,
                public std::enable_shared_from_this<TcpPeer> {
public:
    explicit TcpPeer(boost::asio::ip::tcp::socket socket);
    ~TcpPeer() override;

    void start();
    void send(const std::vector<uint8_t>& data) override;
    void close() override;
    void set_recv_callback(RecvCallback cb) override { _recv_cb = std::move(cb); }
    std::string remote_addr() const override { return _addr; }

private:
    void read_header();
    void read_body(uint32_t length);
    void do_write(std::vector<uint8_t> data);

    boost::asio::ip::tcp::socket _socket;
    std::string _addr;
    std::array<uint8_t, 4> _header_buf{};
    std::vector<uint8_t> _body_buf;
    RecvCallback _recv_cb;
    bool _closed = false;
};

}  // namespace gs
