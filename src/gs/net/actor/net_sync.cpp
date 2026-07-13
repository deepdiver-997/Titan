#include "gs/net/actor/net_sync.h"
#include "gs/net/message.h"

namespace gs {

NetSyncActor::NetSyncActor(ActorId id, IServer* server)
    : Actor(id), _server(server) {}

void NetSyncActor::on_message(Message& msg) {
    auto* client_msg = dynamic_cast<ClientBoundMsg*>(&msg);
    if (!client_msg) return;

    std::vector<uint8_t> bytes(client_msg->data.begin(),
                               client_msg->data.end());
    _server->send_to(client_msg->target_player, encode_message(bytes));
}

}  // namespace gs
