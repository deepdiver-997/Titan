#pragma once

#include "gs/actor/actor.h"
#include "gs/common/types.h"
#include "gs/net/i_server.h"
#include "gs/net/protocol/session_manager.h"

#include <functional>
#include <string>
#include <unordered_map>

namespace gs {

// ---- Messages --------------------------------------------------------------

// A new raw transport connection arrived. The NetSyncActor should bind
// it to a Session by reading the first packet's protocol header.
// Posted from the transport callback via ActorSystem::send().
struct NewConnectionMsg : public Message {
    std::shared_ptr<IConnection> conn;
};

// Send data to a specific player. Other Actors send this via
// send_deferred(). The NetSyncActor routes through Session channels.
struct ClientBoundMsg : public Message {
    EntityId target_player;
    uint8_t channel = 0;    // 0=reliable (TCP), 1=unreliable (UDP)
    std::string data;
};

// ---- NetSyncActor ----------------------------------------------------------

// Actor that owns all network output and Session lifecycle.
// Thread-confined: runs on the actor thread (via process_group),
// so SessionManager operations need no locks.
//
// Two send modes:
//   1. Session mode: routes through _entity_map → Session.send(channel).
//      Requires map_entity() calls from session_callback.
//   2. Legacy IServer mode: calls server->send_to(eid, data) directly,
//      channel ignored. Created via the IServer* constructor.
class NetSyncActor : public Actor {
public:
    using SessionCallback = std::function<void(Session&)>;

    // ---- Constructors ------------------------------------------------------

    // Session mode (default). Call map_entity() to link players.
    NetSyncActor(ActorId id);

    // Legacy: send via IServer, no session/channel support.
    NetSyncActor(ActorId id, IServer* server);

    // ---- Session management ------------------------------------------------

    SessionManager& session_mgr() { return _session_mgr; }

    // Link an EntityId to a Session for send routing.
    void map_entity(EntityId eid, SessionId sid) {
        _entity_map[eid] = sid;
    }

    // Callback when a new Session is bound (at least one channel ready).
    // The user should create their player entity and call map_entity() here.
    void set_session_callback(SessionCallback cb) {
        _session_mgr.set_session_callback(std::move(cb));
    }

protected:
    void on_message(Message& msg) override;

private:
    void send_via_session(EntityId target, uint8_t channel,
                          const std::vector<uint8_t>& data);

    SessionManager _session_mgr;
    std::unordered_map<EntityId, SessionId> _entity_map;
    IServer* _legacy_server = nullptr;  // non-null = legacy mode
};

}  // namespace gs
