#pragma once

#include "gs/common/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gs {

// Simple length-prefixed binary protocol:
//   [4-byte big-endian length][payload bytes]
//
// Message types (1-byte type field at start of payload):
enum class MsgType : uint8_t {
    Login = 0x01,
    Move = 0x02,
    Chat = 0x03,
    AoiEvent = 0x04,
    Disconnect = 0x05,
};

// ---- Internal Actor messages (not network protocol!) ---------------------
// Concrete Message subclass: player wants to move.
struct MoveMessage : public Message {
    EntityId player_id;
    Vec2 new_pos;
};

// ---- Incoming network messages (client → server) -------------------------
struct LoginRequest {
    std::string player_name;
};

struct MoveRequest {
    float x;
    float y;
};

struct ChatRequest {
    std::string text;
};

// ---- Outgoing network messages (server → client) -------------------------
struct AoiEvent {
    uint8_t event_type;  // 0=enter, 1=leave, 2=move
    uint64_t entity_id;
    float x;
    float y;
};

// ---- Serialization helpers ----------------------------------------------
std::vector<uint8_t> encode_message(const std::vector<uint8_t>& payload);
std::vector<uint8_t> encode_aoi_event(const AoiEvent& ev);

}  // namespace gs
