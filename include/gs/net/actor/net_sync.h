#pragma once

#include "gs/actor/actor.h"
#include "gs/common/types.h"
#include "gs/net/i_server.h"

#include <functional>
#include <string>

namespace gs {

class TitanServer;

// Message from a game Actor to NetSyncActor: "send this to player X".
// @param target_player  EntityId of the recipient
// @param channel        0=reliable (TCP/QUIC), 1=unreliable (UDP)
// @param data           serialized payload (user-level, not wire format)
struct ClientBoundMsg : public Message {
    EntityId target_player;
    uint8_t channel = 0;
    std::string data;
};

// Actor that owns the network output path. Other Actors send ClientBoundMsg
// to this Actor via send_deferred().
//
// Supports two send modes:
//   1. IServer* — legacy: calls server->send_to(eid, data) (channel ignored)
//   2. Callback — flexible: user provides a send function, e.g.
//      [&](EntityId eid, uint8_t ch, auto& data) {
//          server.send_to_entity(eid, ch, data);
//      }
class NetSyncActor : public Actor {
public:
    using SendCallback = std::function<void(EntityId, uint8_t,
                                            const std::vector<uint8_t>&)>;

    // Legacy: send via IServer (channel always 0).
    NetSyncActor(ActorId id, IServer* server);

    // Flexible: user-provided send callback.
    NetSyncActor(ActorId id, SendCallback send_cb);

protected:
    void on_message(Message& msg) override;

private:
    SendCallback _send_cb;
};

}  // namespace gs
