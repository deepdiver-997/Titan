#include "gs/net/protocol/session.h"

#include <cstring>

namespace gs {

// ---- helpers ---------------------------------------------------------------

static uint16_t read_be16(const uint8_t* p) {
    return (static_cast<uint16_t>(p[0]) << 8) | p[1];
}

static void write_be16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v & 0xFF);
}

// ---- Session ---------------------------------------------------------------

void Session::attach(int channel, std::shared_ptr<IConnection> conn) {
    if (channel < 0 || channel > 1) return;
    if (_conns[channel] && _conns[channel] != conn) {
        _conns[channel]->close();
    }
    _conns[channel] = std::move(conn);
}

bool Session::detach(IConnection* conn) {
    for (int i = 0; i < 2; ++i) {
        if (_conns[i].get() == conn) {
            _conns[i].reset();
            return true;
        }
    }
    return false;
}

bool Session::send(int channel, const std::vector<uint8_t>& payload) {
    if (channel < 0 || channel > 1) return false;
    auto& conn = _conns[channel];
    if (!conn || conn->is_closed()) return false;

    // Build packet: header + payload.
    std::vector<uint8_t> packet(sizeof(SessionHeader) + payload.size());
    auto* hdr = reinterpret_cast<SessionHeader*>(packet.data());
    hdr->session_id = _id;
    hdr->channel = static_cast<uint8_t>(channel);
    write_be16(reinterpret_cast<uint8_t*>(&hdr->payload_len_be),
               static_cast<uint16_t>(payload.size()));
    if (!payload.empty()) {
        std::memcpy(packet.data() + sizeof(SessionHeader),
                    payload.data(), payload.size());
    }
    conn->send(packet);
    return true;
}

std::vector<Session::RecvPacket> Session::drain_all() {
    std::vector<RecvPacket> result;
    for (int ch = 0; ch < 2; ++ch) {
        auto& conn = _conns[ch];
        if (!conn) continue;

        auto raw = conn->swap_recv_buffer();
        size_t offset = 0;
        while (offset + sizeof(SessionHeader) <= raw.size()) {
            auto* hdr = reinterpret_cast<SessionHeader*>(raw.data() + offset);
            uint16_t plen = read_be16(
                reinterpret_cast<uint8_t*>(&hdr->payload_len_be));

            // session_id mismatch → skip (might be from a stale bind).
            if (hdr->session_id != _id) {
                offset += sizeof(SessionHeader) + plen;
                continue;
            }

            offset += sizeof(SessionHeader);
            if (offset + plen > raw.size()) break;  // incomplete packet

            RecvPacket pkt;
            pkt.channel = hdr->channel;
            if (plen > 0) {
                pkt.payload.assign(raw.data() + offset,
                                   raw.data() + offset + plen);
            }
            result.push_back(std::move(pkt));
            offset += plen;
        }
    }
    return result;
}

std::vector<Session::RecvPacket> Session::drain_framed() {
    std::vector<RecvPacket> result;
    for (int ch = 0; ch < 2; ++ch) {
        auto& conn = _conns[ch];
        if (!conn) continue;

        auto framed = conn->swap_recv_buffer();

        // Strip TcpConnection's [4B len][body] framing.
        std::vector<uint8_t> raw;
        size_t off = 0;
        while (off + 4 <= framed.size()) {
            uint32_t flen =
                (static_cast<uint32_t>(framed[off]) << 24) |
                (static_cast<uint32_t>(framed[off+1]) << 16) |
                (static_cast<uint32_t>(framed[off+2]) << 8) |
                static_cast<uint32_t>(framed[off+3]);
            off += 4;
            if (off + flen > framed.size()) break;
            raw.insert(raw.end(), framed.begin() + off,
                       framed.begin() + off + flen);
            off += flen;
        }

        // Parse SessionHeader packets from unframed bytes.
        size_t offset = 0;
        while (offset + sizeof(SessionHeader) <= raw.size()) {
            auto* hdr = reinterpret_cast<SessionHeader*>(raw.data() + offset);
            uint16_t plen = read_be16(
                reinterpret_cast<uint8_t*>(&hdr->payload_len_be));

            if (hdr->session_id != _id) {
                offset += sizeof(SessionHeader) + plen;
                continue;
            }

            offset += sizeof(SessionHeader);
            if (offset + plen > raw.size()) break;

            RecvPacket pkt;
            pkt.channel = hdr->channel;
            if (plen > 0) {
                pkt.payload.assign(raw.data() + offset,
                                   raw.data() + offset + plen);
            }
            result.push_back(std::move(pkt));
            offset += plen;
        }
    }
    return result;
}

void Session::close() {
    for (int i = 0; i < 2; ++i) {
        if (_conns[i]) {
            _conns[i]->close();
            _conns[i].reset();
        }
    }
}

}  // namespace gs
