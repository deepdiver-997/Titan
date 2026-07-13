#include "gs/debug/snapshot.h"
#include "gs/actor/actor_system.h"
#include "third_party/bthread_timer/timer.h"

#include <cstring>
#include <fstream>
#include <iostream>

namespace gs::debug {

namespace {

// Context passed through bthread_timer's void* arg.
struct CaptureCtx {
    std::string tag;
    std::string file;
    int line;
    uint64_t tick_counter;
};

}  // namespace

// ============================================================================
// SnapshotWriter
// ============================================================================

void SnapshotWriter::write_bytes(const void* data, size_t len) {
    const auto* p = static_cast<const uint8_t*>(data);
    _buf.insert(_buf.end(), p, p + len);
}

void SnapshotWriter::write_u8(uint8_t v) { write_bytes(&v, 1); }
void SnapshotWriter::write_u32(uint32_t v) { write_bytes(&v, 4); }
void SnapshotWriter::write_u64(uint64_t v) { write_bytes(&v, 8); }

void SnapshotWriter::write_string(const std::string& s) {
    uint16_t len = static_cast<uint16_t>(s.size());
    write_bytes(&len, 2);
    write_bytes(s.data(), len);
}

// ============================================================================
// SnapshotReader
// ============================================================================

SnapshotReader::SnapshotReader(const std::vector<uint8_t>& data)
    : _data(data) {}

SnapshotReader::SnapshotReader(std::vector<uint8_t>&& data)
    : _data(std::move(data)) {}

void SnapshotReader::read_bytes(void* data, size_t len) {
    if (_offset + len > _data.size()) return;
    std::memcpy(data, _data.data() + _offset, len);
    _offset += len;
}

uint8_t SnapshotReader::read_u8() {
    uint8_t v = 0;
    read_bytes(&v, 1);
    return v;
}

uint32_t SnapshotReader::read_u32() {
    uint32_t v = 0;
    read_bytes(&v, 4);
    return v;
}

uint64_t SnapshotReader::read_u64() {
    uint64_t v = 0;
    read_bytes(&v, 8);
    return v;
}

std::string SnapshotReader::read_string() {
    uint16_t len = 0;
    read_bytes(&len, 2);
    std::string s(len, '\0');
    if (len > 0) read_bytes(&s[0], len);
    return s;
}

// ============================================================================
// SnapshotManager
// ============================================================================

SnapshotManager& SnapshotManager::instance() {
    static SnapshotManager mgr;
    return mgr;
}

void SnapshotManager::capture(const char* tag, const char* file, int line) {
    if (!_actor_system) return;

    if (_timer) {
        // Schedule a one-shot DONT_COUNT_TIME task on the bthread_timer.
        auto ctx = std::make_unique<CaptureCtx>();
        ctx->tag = tag;
        ctx->file = file;
        ctx->line = line;
        ctx->tick_counter = 0;
        _timer->schedule(capture_trampoline, ctx.release(),
                         std::chrono::steady_clock::now(),
                         bthread_timer::TASK_FLAG_DONT_COUNT_TIME);
    } else {
        // No timer set — fall back to synchronous (e.g. in tests).
        do_capture(tag, file, line, 0);
    }
}

void SnapshotManager::capture_at(const char* tag, const char* file,
                                  int line, uint64_t tick_counter) {
    if (!_actor_system) return;

    if (_timer) {
        auto ctx = std::make_unique<CaptureCtx>();
        ctx->tag = tag;
        ctx->file = file;
        ctx->line = line;
        ctx->tick_counter = tick_counter;
        _timer->schedule(capture_trampoline, ctx.release(),
                         std::chrono::steady_clock::now(),
                         bthread_timer::TASK_FLAG_DONT_COUNT_TIME);
    } else {
        do_capture(tag, file, line, tick_counter);
    }
}

void SnapshotManager::capture_trampoline(void* arg) {
    auto ctx = std::unique_ptr<CaptureCtx>(static_cast<CaptureCtx*>(arg));
    instance().do_capture(ctx->tag, ctx->file, ctx->line, ctx->tick_counter);
}

void SnapshotManager::do_capture(const std::string& tag,
                                  const std::string& file,
                                  int line, uint64_t tick_counter) {
    ServerSnapshot snap;
    snap.tick_counter = tick_counter;

    // Unique_lock on debug mutex — safe here because we're on the
    // bthread_timer thread with virtual time frozen, so no
    // process_group() is running.
    _actor_system->capture_all(snap.actors);

    _snapshots.push_back(std::move(snap));

    if (_callback) {
        _callback(_snapshots.back());
    }
}

void SnapshotManager::save(const std::string& path) const {
    // Write all snapshots to a single file.
    // Format: [TITANSNAP_ batch header][snapshot 0][snapshot 1]...
    if (_snapshots.empty()) return;
    // For now, just write the last snapshot (most common use case).
    auto copy = _snapshots.back();
    write_snapshot(copy, path);
}

// ============================================================================
// I/O: binary serialization
// ============================================================================

static void write_u32(std::ofstream& os, uint32_t v) {
    os.write(reinterpret_cast<const char*>(&v), 4);
}

static uint32_t read_u32(std::ifstream& is) {
    uint32_t v = 0;
    is.read(reinterpret_cast<char*>(&v), 4);
    return v;
}

static void write_u64(std::ofstream& os, uint64_t v) {
    os.write(reinterpret_cast<const char*>(&v), 8);
}

static uint64_t read_u64(std::ifstream& is) {
    uint64_t v = 0;
    is.read(reinterpret_cast<char*>(&v), 8);
    return v;
}

void write_snapshot(ServerSnapshot& snap, const std::string& path) {
    std::ofstream os(path, std::ios::binary);
    // Magic + version
    os.write("TITANSNAP", 8);
    write_u32(os, 1);

    write_u64(os, snap.tick_counter);
    write_u32(os, static_cast<uint32_t>(snap.actors.size()));

    for (auto& entry : snap.actors) {
        write_u64(os, entry.actor_id);
        uint16_t name_len = static_cast<uint16_t>(entry.name.size());
        os.write(reinterpret_cast<const char*>(&name_len), 2);
        os.write(entry.name.data(), name_len);
        os.put(entry.active ? 1 : 0);
        write_u32(os, static_cast<uint32_t>(entry.user_data.size()));
        os.write(reinterpret_cast<const char*>(entry.user_data.data()),
                 entry.user_data.size());
    }
}

ServerSnapshot read_snapshot(const std::string& path) {
    ServerSnapshot snap;
    std::ifstream is(path, std::ios::binary);

    char magic[8];
    is.read(magic, 8);
    uint32_t version = read_u32(is);
    (void)version;

    snap.tick_counter = read_u64(is);
    uint32_t actor_count = read_u32(is);

    for (uint32_t i = 0; i < actor_count; ++i) {
        ActorStateEntry entry;
        entry.actor_id = read_u64(is);
        uint16_t name_len = 0;
        is.read(reinterpret_cast<char*>(&name_len), 2);
        entry.name.resize(name_len);
        if (name_len > 0) is.read(&entry.name[0], name_len);
        entry.active = (is.get() != 0);
        uint32_t user_data_len = read_u32(is);
        entry.user_data.resize(user_data_len);
        if (user_data_len > 0)
            is.read(reinterpret_cast<char*>(entry.user_data.data()),
                    user_data_len);
        snap.actors.push_back(std::move(entry));
    }
    return snap;
}

void write_events(const std::vector<RecordedEvent>& events,
                   const std::string& path) {
    std::ofstream os(path, std::ios::binary);
    os.write("TITANEVTS", 8);
    write_u32(os, 1);  // version
    write_u32(os, static_cast<uint32_t>(events.size()));

    for (auto& ev : events) {
        os.put(static_cast<uint8_t>(ev.type));
        write_u64(os, ev.tick_counter);
        write_u64(os, ev.entity_id);
        write_u32(os, static_cast<uint32_t>(ev.data.size()));
        if (!ev.data.empty())
            os.write(reinterpret_cast<const char*>(ev.data.data()),
                     ev.data.size());
    }
}

std::vector<RecordedEvent> read_events(const std::string& path) {
    std::vector<RecordedEvent> events;
    std::ifstream is(path, std::ios::binary);

    char magic[8];
    is.read(magic, 8);
    uint32_t version = read_u32(is);
    (void)version;

    uint32_t count = read_u32(is);
    events.reserve(count);

    for (uint32_t i = 0; i < count; ++i) {
        RecordedEvent ev;
        ev.type = static_cast<RecordedEvent::Type>(is.get());
        ev.tick_counter = read_u64(is);
        ev.entity_id = read_u64(is);
        uint32_t data_len = read_u32(is);
        ev.data.resize(data_len);
        if (data_len > 0)
            is.read(reinterpret_cast<char*>(ev.data.data()), data_len);
        events.push_back(std::move(ev));
    }
    return events;
}

}  // namespace gs::debug
