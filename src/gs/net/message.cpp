#include "gs/net/message.h"

#include <cstring>

namespace gs {

std::vector<uint8_t> encode_message(const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> result;
    uint32_t len = static_cast<uint32_t>(payload.size());
    result.resize(4 + len);
    // Big-endian length prefix.
    result[0] = static_cast<uint8_t>((len >> 24) & 0xFF);
    result[1] = static_cast<uint8_t>((len >> 16) & 0xFF);
    result[2] = static_cast<uint8_t>((len >> 8) & 0xFF);
    result[3] = static_cast<uint8_t>(len & 0xFF);
    std::memcpy(result.data() + 4, payload.data(), len);
    return result;
}

std::vector<uint8_t> encode_aoi_event(const AoiEvent& ev) {
    // Payload: [type(1)][entity_id(8)][x(4)][y(4)] = 17 bytes
    std::vector<uint8_t> payload(17);
    payload[0] = static_cast<uint8_t>(MsgType::AoiEvent);
    payload[1] = ev.event_type;
    std::memcpy(payload.data() + 2, &ev.entity_id, 8);
    std::memcpy(payload.data() + 10, &ev.x, 4);
    std::memcpy(payload.data() + 14, &ev.y, 4);
    return encode_message(payload);
}

}  // namespace gs
