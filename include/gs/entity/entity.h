#pragma once

#include "gs/common/types.h"

#include <string>

namespace gs {

// Base class for all game entities.
class Entity {
public:
    explicit Entity(EntityId id, EntityType type)
        : _id(id), _type(type) {}

    virtual ~Entity() = default;

    EntityId id() const { return _id; }
    EntityType type() const { return _type; }

    const Vec2& position() const { return _position; }
    void set_position(const Vec2& pos) { _position = pos; }

    const std::string& name() const { return _name; }
    void set_name(const std::string& name) { _name = name; }

private:
    EntityId _id;
    EntityType _type;
    Vec2 _position;
    std::string _name;
};

}  // namespace gs
