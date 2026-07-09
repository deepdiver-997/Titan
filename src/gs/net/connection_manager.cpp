#include "gs/net/connection_manager.h"

namespace gs {

void ConnectionManager::add(EntityId player_id,
                            std::shared_ptr<TcpConnection> conn) {
    std::lock_guard<std::mutex> lk(_mutex);
    _entries[player_id] = {std::move(conn), player_id};
}

void ConnectionManager::remove(EntityId player_id) {
    std::lock_guard<std::mutex> lk(_mutex);
    _entries.erase(player_id);
}

std::unordered_map<EntityId, std::vector<uint8_t>>
ConnectionManager::swap_all_buffers() {
    std::unordered_map<EntityId, std::vector<uint8_t>> result;
    std::lock_guard<std::mutex> lk(_mutex);
    for (auto& [id, entry] : _entries) {
        if (entry.conn->is_closed()) continue;
        auto data = entry.conn->recv_buffer().swap_out();
        if (!data.empty()) {
            result[id] = std::move(data);
        }
    }
    return result;
}

void ConnectionManager::send_to(EntityId player_id,
                                const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lk(_mutex);
    auto it = _entries.find(player_id);
    if (it != _entries.end()) {
        it->second.conn->send(data);
    }
}

std::vector<ConnectionManager::Entry> ConnectionManager::snapshot() const {
    std::lock_guard<std::mutex> lk(_mutex);
    std::vector<Entry> result;
    result.reserve(_entries.size());
    for (const auto& [id, entry] : _entries) {
        result.push_back(entry);
    }
    return result;
}

size_t ConnectionManager::size() const {
    std::lock_guard<std::mutex> lk(_mutex);
    return _entries.size();
}

}  // namespace gs
