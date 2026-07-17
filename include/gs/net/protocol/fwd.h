#pragma once

#include "gs/common/types.h"

#include <cstdint>

namespace gs {

// SessionId distinguishes one connected client from another.
// Unlike EntityId (a game-level concept), SessionId is purely a
// transport identity — it labels the set of IConnections that belong
// to the same logical client.
//
// Allocation:
//   1 .. 2^31-1  =  normal sessions
//   0            =  INVALID_SESSION_ID
using SessionId = uint32_t;
constexpr SessionId INVALID_SESSION_ID = 0;

}  // namespace gs
