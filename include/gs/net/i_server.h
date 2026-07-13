#pragma once

#include "gs/common/types.h"
#include "gs/net/i_connection.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace gs {

// Abstract server — accepts client connections and manages the
// player_id → connection mapping.  Swap TCP/QUIC/WebSocket without
// touching game logic.
class IServer {
public:
    using ConnectionCallback =
        std::function<void(std::shared_ptr<IConnection>)>;

    virtual ~IServer() = default;

    // Start accepting connections.
    virtual void start() = 0;

    // Stop accepting, optionally close existing connections.
    virtual void close() = 0;

    // Callback invoked for each new connection.
    virtual void set_connection_callback(ConnectionCallback cb) = 0;

    // ---- Connection management -------------------------------------------

    virtual void register_conn(EntityId player_id,
                               std::shared_ptr<IConnection> conn) = 0;
    virtual void unregister_conn(EntityId player_id) = 0;

    // Batch-swap all recv buffers.  Returns non-empty buffers keyed by
    // player_id.  Implicitly cleans up closed connections.
    virtual std::unordered_map<EntityId, std::vector<uint8_t>>
        swap_all_buffers() = 0;

    // Send to a specific player.
    virtual void send_to(EntityId player_id,
                         const std::vector<uint8_t>& data) = 0;

    // Redirect all clients to another server.
    virtual void drain(const std::string& target_ip,
                       uint16_t target_port) = 0;

    // ---- Stats -----------------------------------------------------------

    virtual size_t conn_count() const = 0;
    virtual uint16_t port() const = 0;
};

}  // namespace gs
