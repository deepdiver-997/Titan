#pragma once

#include "gs/debug/trace_event.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace bthread_timer {
class Timer;
}  // namespace bthread_timer

namespace gs {

class ActorSystem;

namespace debug {

// ---- SnapshotWriter / SnapshotReader ------------------------------------
//
// Lightweight binary serializer — no external dependency.
// Used by Actor::capture_state() and Actor::restore_state().
class SnapshotWriter {
public:
    void write_bytes(const void* data, size_t len);
    void write_u8(uint8_t v);
    void write_u32(uint32_t v);
    void write_u64(uint64_t v);
    void write_string(const std::string& s);

    const std::vector<uint8_t>& data() const { return _buf; }
    std::vector<uint8_t> take_data() { return std::move(_buf); }

private:
    std::vector<uint8_t> _buf;
};

class SnapshotReader {
public:
    explicit SnapshotReader(const std::vector<uint8_t>& data);
    explicit SnapshotReader(std::vector<uint8_t>&& data);

    void read_bytes(void* data, size_t len);
    uint8_t read_u8();
    uint32_t read_u32();
    uint64_t read_u64();
    std::string read_string();
    bool exhausted() const { return _offset >= _data.size(); }

private:
    std::vector<uint8_t> _data;
    size_t _offset = 0;
};

// ---- SnapshotManager — singleton -----------------------------------------
//
// Manages user-triggered snapshots of the entire ActorSystem.
// Snapshots are executed **asynchronously** on the bthread_timer thread
// with TASK_FLAG_DONT_COUNT_TIME, so virtual time freezes during capture
// and there is no deadlock risk with process_group().
//
// Usage:
//   SnapshotManager::instance().set_actor_system(&sys);
//   SnapshotManager::instance().set_timer(&server.tick_timer());
//
//   // Inside a tick callback:
//   SNAPSHOT("after_input_collect");
//
// When TITAN_DEBUG is off, SNAPSHOT() compiles to nothing.
class SnapshotManager {
public:
    static SnapshotManager& instance();

    // Take a snapshot of all Actor state at the current point.
    // Schedules a one-shot TASK_FLAG_DONT_COUNT_TIME task on the
    // bthread_timer. The actual capture runs asynchronously on the
    // timer thread — no deadlock risk with process_group().
    void capture(const char* tag, const char* file, int line);

    // Same as capture(), with an explicit tick counter value.
    void capture_at(const char* tag, const char* file, int line,
                    uint64_t tick_counter);

    // Register a callback to be invoked on every snapshot.
    // The callback receives the latest snapshot for external processing.
    using SnapshotCallback = std::function<void(const ServerSnapshot&)>;
    void set_callback(SnapshotCallback cb) { _callback = std::move(cb); }

    // Set the bthread_timer for async snapshot scheduling.
    // Must be called before any capture() calls.
    void set_timer(bthread_timer::Timer* t) { _timer = t; }

    // Set the ActorSystem to snapshot.
    void set_actor_system(ActorSystem* sys) { _actor_system = sys; }

    // Save all captured snapshots to a file.
    void save(const std::string& path) const;

    // Access the most recent snapshot.
    const ServerSnapshot* last_snapshot() const {
        return _snapshots.empty() ? nullptr : &_snapshots.back();
    }

    const std::vector<ServerSnapshot>& snapshots() const { return _snapshots; }
    void clear() { _snapshots.clear(); }

private:
    SnapshotManager() = default;

    // Trampoline for bthread_timer C-callback.
    static void capture_trampoline(void* arg);
    void do_capture(const std::string& tag, const std::string& file,
                    int line, uint64_t tick_counter);

    bthread_timer::Timer* _timer = nullptr;
    ActorSystem* _actor_system = nullptr;
    std::vector<ServerSnapshot> _snapshots;
    SnapshotCallback _callback;
};

}  // namespace debug
}  // namespace gs
