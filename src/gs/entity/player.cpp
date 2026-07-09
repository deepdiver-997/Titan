#include "gs/entity/player.h"

#include <algorithm>
#include <sstream>

namespace gs {

Player::Player(EntityId id, const std::string& name, const Vec2& pos)
    : Entity(id, EntityType::Player) {
    set_name(name);
    set_position(pos);
}

void Player::on_entity_enter_view(EntityId other_id) {
    auto it = std::find(_visible_ids.begin(), _visible_ids.end(), other_id);
    if (it == _visible_ids.end()) {
        _visible_ids.push_back(other_id);
        std::ostringstream oss;
        oss << "ENTER " << other_id;
        send_to_client(oss.str());
    }
}

void Player::on_entity_leave_view(EntityId other_id) {
    auto it = std::find(_visible_ids.begin(), _visible_ids.end(), other_id);
    if (it != _visible_ids.end()) {
        _visible_ids.erase(it);
        std::ostringstream oss;
        oss << "LEAVE " << other_id;
        send_to_client(oss.str());
    }
}

void Player::send_to_client(const std::string& data) {
    if (_send_cb) {
        _send_cb(data);
    }
}

}  // namespace gs
