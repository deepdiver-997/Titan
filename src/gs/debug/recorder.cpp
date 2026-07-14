#include "gs/debug/recorder.h"
#include "gs/debug/snapshot.h"
#include "gs/actor/actor_system.h"
#include "gs/server/titan_server.h"

#include <algorithm>
#include <fstream>
#include <iostream>

namespace gs::debug {

// ============================================================================
// Recorder
// ============================================================================

Recorder& Recorder::instance() {
    static Recorder rec;
    return rec;
}

void Recorder::start() {
    _events.clear();
    _recording.store(true);
    std::cout << "[recorder] started recording\n";
}

void Recorder::stop() {
    _recording.store(false);
    std::cout << "[recorder] stopped (" << _events.size() << " events)\n";
}

void Recorder::clear() {
    _events.clear();
}

void Recorder::record_tcp_packet(uint64_t player_id, const uint8_t* data,
                                  size_t len, uint64_t tick_counter) {
    if (!_recording.load()) return;
    RecordedEvent ev;
    ev.type = RecordedEvent::TcpPacketIn;
    ev.tick_counter = tick_counter;
    ev.entity_id = player_id;
    ev.data.assign(data, data + len);
    _events.push_back(std::move(ev));
}

void Recorder::record_peer_message(uint64_t node_id, const uint8_t* data,
                                    size_t len, uint64_t tick_counter) {
    if (!_recording.load()) return;
    RecordedEvent ev;
    ev.type = RecordedEvent::PeerActorMsg;
    ev.tick_counter = tick_counter;
    ev.entity_id = node_id;
    ev.data.assign(data, data + len);
    _events.push_back(std::move(ev));
}

void Recorder::record_actor_spawned(uint64_t actor_id,
                                     const std::string& name) {
    if (!_recording.load()) return;
    RecordedEvent ev;
    ev.type = RecordedEvent::ActorSpawned;
    ev.tick_counter = 0;
    ev.entity_id = actor_id;
    ev.data.assign(name.begin(), name.end());
    _events.push_back(std::move(ev));
}

void Recorder::record_mailbox_push(uint64_t actor_id, uint32_t tick) {
    if (!_recording.load()) return;
    RecordedEvent ev;
    ev.type = RecordedEvent::MailboxPush;
    ev.tick_counter = tick;
    ev.entity_id = actor_id;
    _events.push_back(std::move(ev));
}

void Recorder::save(const std::string& path) const {
    write_events(_events, path);
}

void Recorder::load(const std::string& path) {
    _events = read_events(path);
}

// ============================================================================
// Replay (moved to TitanServer::reload_state)
// ============================================================================

}  // namespace gs::debug
