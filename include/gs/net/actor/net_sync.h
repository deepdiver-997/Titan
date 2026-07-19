#pragma once

#include "gs/actor/actor.h"
#include "gs/common/types.h"
#include "gs/net/i_server.h"

#include <functional>
#include <string>
#include <vector>

namespace gs {

// ---- Message types ---------------------------------------------------------

// A new raw transport connection arrived. Route this to your network
// Actor's mailbox so it can be bound to a Session via SessionManager.
struct NewConnectionMsg : public Message {
    std::shared_ptr<IConnection> conn;
};

// Send data to a specific player. Route this to your network Actor.
struct ClientBoundMsg : public Message {
    EntityId target_player;
    uint8_t channel = 0;    // 0=reliable, 1=unreliable
    std::string data;
};

// ---- Network Helper Actors --------------------------------------------------

// Lightweight network output actor.
//
// Mode 1 — IServer* (legacy): calls server->send_to(eid, data).
//   NetSyncActor(ActorId id, IServer* server)
//
// Mode 2 — SendCallback (flexible):
//   NetSyncActor(ActorId id, [&](EntityId eid, uint8_t ch, auto& data) {
//       session_mgr.find(entity_map[eid])->send(ch, data);
//   });
//
// For multiple output frequencies, create several NetSyncActors in
// different tick groups, each with its own callback.
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
