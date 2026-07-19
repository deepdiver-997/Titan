#include "gs/net/actor/net_sync.h"
#include "gs/net/message.h"

namespace gs {

NetSyncActor::NetSyncActor(ActorId id, IServer* server)
    : Actor(id)
{
    _send_cb = [server](EntityId eid, uint8_t /*channel*/,
                        const std::vector<uint8_t>& data) {
        server->send_to(eid, data);
    };
}

NetSyncActor::NetSyncActor(ActorId id, SendCallback send_cb)
    : Actor(id), _send_cb(std::move(send_cb)) {}

void NetSyncActor::on_message(Message& msg) {
    // Handle NewConnectionMsg (can be ignored — just an example).
    if (dynamic_cast<NewConnectionMsg*>(&msg)) {
        return;  // User handles binding in their own actor or callback.
    }

    auto* client_msg = dynamic_cast<ClientBoundMsg*>(&msg);
    if (!client_msg || !_send_cb) return;

    std::vector<uint8_t> bytes(client_msg->data.begin(),
                               client_msg->data.end());
    _send_cb(client_msg->target_player, client_msg->channel,
             encode_message(bytes));
}

}  // namespace gs
