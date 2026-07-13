#pragma once

#include "gs/actor/actor.h"
#include "gs/common/types.h"
#include "gs/net/i_server.h"

#include <string>

namespace gs {

// Message from a game Actor to NetSyncActor: "send this to player X".
struct ClientBoundMsg : public Message {
    EntityId target_player;
    std::string data;
};

// Actor that owns the network output path. Other Actors send ClientBoundMsg
// to this Actor via send_deferred(). When its tick group fires, it drains
// its mailbox and calls server->send_to() for each message.
class NetSyncActor : public Actor {
public:
    NetSyncActor(ActorId id, IServer* server);

protected:
    void on_message(Message& msg) override;

private:
    IServer* _server;
};

}  // namespace gs
