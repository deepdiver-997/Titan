#pragma once

#include "gs/entity/entity.h"

#include <functional>
#include <string>
#include <vector>

namespace gs {

// Player entity — represents a connected client.
//
// Each Player is linked to a TCP connection. When AOI events occur
// (enter/leave/move), the Player routes them to the client.
class Player : public Entity {
public:
    Player(EntityId id, const std::string& name, const Vec2& pos);

    // Called when another entity enters this player's AOI view.
    void on_entity_enter_view(EntityId other_id);

    // Called when another entity leaves this player's AOI view.
    void on_entity_leave_view(EntityId other_id);

    // Set a callback for sending data to the connected client.
    using SendCallback = std::function<void(const std::string& data)>;
    void set_send_callback(SendCallback cb) { _send_cb = std::move(cb); }

    // View tracking.
    const std::vector<EntityId>& visible_entities() const { return _visible_ids; }

private:
    void send_to_client(const std::string& data);

    SendCallback _send_cb;
    std::vector<EntityId> _visible_ids;
};

}  // namespace gs
