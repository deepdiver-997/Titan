# Deterministic Record / Replay

Titan's debug framework provides a **record + snapshot + reload** system for
deterministic replay of game server sessions. This enables debugging,
post-mortem analysis, and disaster recovery.

## Architecture Overview

```
                          ┌──────────────────────┐
 Live mode                │  bthread_timer       │
 (normal operation)       │    └─ wheel ticks    │
                          │    └─ parse_input    │
                          │    └─ process_group  │
                          └──────────────────────┘
                                    │
                    RECORD_TCP_PACKET / SNAPSHOT
                                    ▼
                          ┌──────────────────────┐
 Recorded output          │  session.trace       │
                          │  checkpoint.snap     │
                          └──────────────────────┘
                                    │
                          reload_state() on restart
                                    ▼
                          ┌──────────────────────┐
 Replay mode              │  virtual tick loop   │
 (pre-startup)            │    └─ send events    │
                          │    └─ swap_all       │
                          │    └─ process_group  │
                          └──────────────────────┘
```

### Key concepts

- **RecordedEvent**: stores `(type, tick_counter, entity_id, data)`.
  Types: `TcpPacketIn`, `PeerActorMsg`, `ActorSpawned`, `MailboxPush`.
- **ServerSnapshot**: stores `(tick_counter, actors[])` where each actor
  entry holds `(actor_id, name, active, user_data)`.
- **Master tick** (`_master_tick`): monotonic counter incremented on every
  wheel tick during live mode. All recorded events and snapshots are tagged
  with the master tick when captured.

## Recording

Recording is opt-in via the `TITAN_DEBUG` compile flag. Release builds have
zero overhead.

### What gets recorded

| Data | Trigger | Macro / API |
|------|---------|-------------|
| TCP packets | when `swap_all_buffers()` runs | `RECORD_TCP_PACKET(...)` |
| Inter-node messages | when `PeerManager` receives | `RECORD_PEER_MSG(...)` |
| Mailbox pushes | every `Mailbox::push()` under TITAN_DEBUG | automatic via `Mailbox::set_owner()` |
| Actor spawns | when `ActorSystem::spawn()` is called | `record_actor_spawned()` |
| State snapshots | user-triggered, periodic, or on-demand | `SNAPSHOT("tag")` |

### Usage (main.cpp)

```cpp
#ifdef TITAN_DEBUG
gs::debug::SnapshotManager::instance().set_actor_system(&sys);
gs::debug::SnapshotManager::instance().set_timer(&server.tick_timer());
gs::debug::Recorder::instance().start();
#endif

server.schedule_tick(16, [&]() {
    auto buffers = transport->swap_all_buffers();
    RECORD_TCP_PACKET(pid, buf.data(), buf.size(), server.master_tick());
    parse_input(buffers, sys, scene_mgr);
    sys.swap_all();
    SNAPSHOT("post_input");  // async, on timer thread
});
```

## Snapshot System

The `SnapshotManager` captures all Actor states asynchronously on the
bthread_timer thread with `TASK_FLAG_DONT_COUNT_TIME`. This means:

- Virtual time freezes during capture (no wheel ticks advance).
- No lock contention with `process_group()` (the timer thread runs the
  snapshot task between ticks).
- Safe to call from any tick callback (no deadlock).

### Snapshot format (binary)

```
[TITANSNAP magic 8B][version 4B]
[tick_counter 8B][actor_count 4B]
for each actor:
  [actor_id 8B][name_len 2B][name...][active 1B]
  [user_data_len 4B][user_data...]
```

## Disaster Recovery (`reload_state`)

When a server restarts after a crash, `TitanServer::reload_state()`:
1. Loads a `ServerSnapshot` and vector of `RecordedEvent`s
2. Calls `restore_from_snapshot()` to reconstruct all Actors
3. Discards events with `tick < snapshot.tick_counter`
4. Replays remaining events tick-by-tick (only `swap_all + process_group`,
   no tick callbacks, no network I/O)
5. Sets `_master_tick = max_event_tick + 1` for live continuity

### Usage

```cpp
int main() {
    ServerConfig config;
    TitanServer server(config);
    server.init();

    // --- Optional disaster recovery ----------------------------------------
    bool need_recovery = /* check for checkpoint files */;
    if (need_recovery) {
        auto snap = read_snapshot("checkpoint.snap");
        auto events = read_events("session.trace");
        server.reload_state(snap, events);
        // Actors are now restored. Do NOT call sys.spawn() again.
    } else {
        // Fresh start: create actors and register ticks.
        sys.spawn(std::make_unique<MyActor>(100), grp);
        server.schedule_tick(16, [&]() { /* ... */ });
    }

    // --- Start live operation ----------------------------------------------
    transport->start();
    server.run();
}
```

### Important notes

- `reload_state()` must be called **after** `server.init()` but **before**
  `server.run()` and `transport->start()`.
- After `reload_state()`, the ActorSystem already has all actors restored
  from the snapshot. Do NOT call `sys.spawn()` again — actors would be
  duplicated.
- Tick callbacks (`schedule_tick`) should be registered **before** calling
  `reload_state()`, so the server's tick infrastructure is ready for live
  operation when `run()` starts.

## Replay Tests

Four integration tests in `tests/replay/deterministic_replay.test.cpp`:

| Test | Description |
|------|-------------|
| actor processing + snapshot capture | Full record pipeline |
| snapshot captures actor state | `capture_all()` with mixed actor types |
| events before snapshot are skipped | `reload_state` filters old events |
| snapshot too old for events | Events beyond replayed range are harmless |

## Current Limitations

- **Message reconstruction**: `MailboxPush` events carry metadata but not
  full message payloads. Adding `Message::debug_serialize()` would enable
  true event replay.
- **Type-safe restore**: `restore_from_snapshot()` uses a placeholder actor.
  A factory pattern is needed to restore concrete actor types.
- **Client sync**: After disaster recovery, clients must reconnect and
  receive a full state sync — Titan does not attempt seamless failover.
