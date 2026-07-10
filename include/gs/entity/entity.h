#pragma once

#include "gs/common/types.h"

#include <string>

namespace gs {

class Actor;

// Base class for all game entities.
//
// RPC model: client sends [entity_id][func_id][args], server calls
// entity->exec(func_id, args, len). Subclasses map func_id to methods.
// func_id namespace is per-entity-type (no global conflict).
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

    // RPC entry point. Subclasses dispatch func_id to methods.
    // @param self  the Actor that owns this entity (for send_deferred etc.)
    virtual void exec(int func_id, const void* args, size_t len,
                      Actor& self) {
        (void)func_id; (void)args; (void)len; (void)self;
    }

private:
    EntityId _id;
    EntityType _type;
    Vec2 _position;
    std::string _name;
};

}  // namespace gs
