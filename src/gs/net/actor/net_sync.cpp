#include "gs/net/actor/net_sync.h"
#include "gs/common/logger.h"
#include "gs/net/message.h"

namespace gs {

NetSyncActor::NetSyncActor(ActorId id)
    : Actor(id) {}

NetSyncActor::NetSyncActor(ActorId id, IServer* server)
    : Actor(id), _legacy_server(server) {}

void NetSyncActor::on_message(Message& msg) {
    // ---- New connection ----------------------------------------------------
    if (auto* nc = dynamic_cast<NewConnectionMsg*>(&msg)) {
        _session_mgr.add_connection(std::move(nc->conn));
        _session_mgr.bind_pending();  // immediate bind (same thread)
        return;
    }

    // ---- Client-bound message ---------------------------------------------
    auto* client_msg = dynamic_cast<ClientBoundMsg*>(&msg);
    if (!client_msg) return;

    std::vector<uint8_t> bytes(client_msg->data.begin(),
                               client_msg->data.end());
    auto encoded = encode_message(bytes);

    if (_legacy_server) {
        // Legacy mode: direct IServer send (channel ignored).
        _legacy_server->send_to(client_msg->target_player, encoded);
        return;
    }

    // Session mode: route via entity→session map.
    send_via_session(client_msg->target_player, client_msg->channel, encoded);
}

void NetSyncActor::send_via_session(EntityId target, uint8_t channel,
                                     const std::vector<uint8_t>& data) {
    auto it = _entity_map.find(target);
    if (it == _entity_map.end()) {
        LOG_NET_WARN("send_via_session: no session for entity {}", target);
        return;
    }
    auto* session = _session_mgr.find(it->second);
    if (!session || !session->is_alive()) {
        LOG_NET_WARN("send_via_session: session {} dead for entity {}",
                     it->second, target);
        _entity_map.erase(it);
        return;
    }
    session->send(channel, data);
}

}  // namespace gs
