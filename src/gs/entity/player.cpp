#include "gs/entity/player.h"

#include <algorithm>

namespace gs {

Player::Player(EntityId id, const std::string& name, const Vec2& pos)
    : Entity(id, EntityType::Player) {
    set_name(name);
    set_position(pos);
}

void Player::track_enter(EntityId other_id) {
    auto it = std::find(_visible_ids.begin(), _visible_ids.end(), other_id);
    if (it == _visible_ids.end()) {
        _visible_ids.push_back(other_id);
    }
}

void Player::track_leave(EntityId other_id) {
    auto it = std::find(_visible_ids.begin(), _visible_ids.end(), other_id);
    if (it != _visible_ids.end()) {
        _visible_ids.erase(it);
    }
}

}  // namespace gs
