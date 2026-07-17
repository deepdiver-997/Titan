#pragma once

#include "gs/net/i_connection.h"
#include "gs/net/protocol/fwd.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace gs {

// Protocol wire format:
//   [session_id 4B][channel 1B][payload_len 2B][payload...]
//
// session_id — SessionId (0 = unbound, assigned by SessionManager)
// channel    — 0 = reliable (TCP/QUIC), 1 = unreliable (UDP)
// payload_len — big-endian uint16, length of payload following
//
// The first message on a raw IConnection MUST be a bind request:
//   [0x00000000 4B][channel 1B][0x0000 2B]   — no payload
// After binding, the SessionManager sets session_id on outgoing packets.

#pragma pack(push, 1)
struct SessionHeader {
    uint32_t session_id;
    uint8_t  channel;
    uint16_t payload_len_be;  // big-endian
};
#pragma pack(pop)

// A logical client connection, composed of up to 2 transport channels:
//   channel 0: reliable   — entity commands, inventory, persistent state
//   channel 1: unreliable — position, velocity, real-time sync
//
// Either channel may be nullptr (not yet bound). A Session is alive
// as long as at least one channel is connected.
class Session {
public:
    Session() = default;
    explicit Session(SessionId id) : _id(id) {}

    SessionId id() const { return _id; }
    void set_id(SessionId id) { _id = id; }

    // Attach a connection to a channel slot (0 or 1).
    // If the slot already has a connection, the old one is closed.
    void attach(int channel, std::shared_ptr<IConnection> conn);

    // Detach a specific connection. If it matches a slot, that slot
    // is cleared. Returns true if anything was removed.
    bool detach(IConnection* conn);

    // Send data on a channel. Prepends the SessionHeader automatically.
    // Returns false if the channel is not attached.
    bool send(int channel, const std::vector<uint8_t>& payload);

    // Drain all recv buffers. Returns (channel, payload) pairs,
    // with the SessionHeader already stripped.
    // Packets with unknown session_id are silently dropped.
    struct RecvPacket {
        int channel;
        std::vector<uint8_t> payload;
    };
    std::vector<RecvPacket> drain_all();

    // True if at least one channel is connected.
    bool is_alive() const {
        return (_conns[0] && !_conns[0]->is_closed()) ||
               (_conns[1] && !_conns[1]->is_closed());
    }

    void close();

private:
    SessionId _id = INVALID_SESSION_ID;
    std::shared_ptr<IConnection> _conns[2];
};

}  // namespace gs
