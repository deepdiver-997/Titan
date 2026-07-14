# Titan Architecture

Detailed design document for interview reference.

## 1. Actor Model

### Double-Buffered Mailbox

```
Actor = {
    cur_msgs: deque<Message>    // consumed this tick (not thread-safe)
    next_mailbox: Mailbox       // thread-safe, for next tick
}

Mailbox = {
    mutex + deque<Message>
    swap_all() → atomically take all, leave empty
}
```

### Tick Flow

```
tick N:
  1. swap_all_mailboxes()     → next_mailbox contents move to cur_msgs
  2. Parse TCP data → push_now() into cur_msgs (same-tick consumption)
  3. process_all()            → each cur_msg dispatched to on_message()
     During on_message, inter-Actor send() pushes to next_mailbox
     (visible in tick N+1 — world appears frozen for this tick)

tick N+1:
  1. swap_all_mailboxes()     → now cur_msgs has tick N's mid-tick messages
  ...
```

Key property: **mid-tick inter-Actor messages are deferred to the next tick**.
This ensures every Actor sees a consistent world snapshot during processing —
no entity can react to another entity's actions within the same tick.

### Scene = Actor

Each map shard (Scene) is an Actor. This means:
- All AOI operations within a scene are lock-free (serial execution)
- Cross-scene communication uses `ActorSystem::send()` (message passing, next-tick delivery)
- Scene lifecycle is managed by the ActorSystem

## 2. AOI System

### IAoi Interface

```cpp
class IAoi {
    virtual void add_entity(const AoiEntity&) = 0;
    virtual void remove_entity(EntityId) = 0;
    virtual bool move_entity(EntityId, const Vec2&) = 0;
    virtual const AoiEntity* get_entity(EntityId) const = 0;
    virtual void set_callback(AoiCallback) = 0;
};
```

Default implementation: `NineGridAoi`. Other algorithms (CrossLinkAoi for
large sparse worlds, TowerAoi for 2.5D) implement the same interface.
Scene holds `unique_ptr<IAoi>` — inject a custom AOI via the constructor,
defaults to `make_default_aoi()`.

### Nine-Grid Algorithm (NineGridAoi)

The world is divided into fixed-size cells (16m x 16m, matching Minecraft's
chunk size). Each entity is registered in the cell containing its center.

An entity's view covers `(2*radius+1)^2` cells centered on its current cell.
Default radius = 2 -> 25 cells.

### Move Sequence

```
entity.move(new_pos):
    old_grid = world_to_grid(old_pos)
    new_grid = world_to_grid(new_pos)

    if old_grid != new_grid:
        grid[old_grid].remove(entity)
        grid[new_grid].add(entity)

    old_visible = compute_visible(old_grid, radius)
    new_visible = compute_visible(new_grid, radius)

    entered = new_visible - old_visible
    left    = old_visible - new_visible

    notify(entity, entered, left)
```

### Optimization: Diff-Only Push

Only entities whose visibility changed receive updates. Entities that stayed
in view with no position change receive nothing.

## 3. Timer Architecture

Titan uses a **three-tier timer design** — a common interview topic.

### 3.1 TimingWheel — A Data Structure, Not a Timer

```
TimingWheel is a passive data structure:
  slots_[0..3][0..wheel_size] — linked lists of tasks
  tick_count_ — current tick counter
  addTask(delay_ms, cb) — insert into appropriate slot
  cancelTask(id) — lazy cancel
  tick() — advance by 1 tick, fire expired tasks (MUST be called externally)
```

**What it is**: A 4-level hierarchical slot array. Like a clock without a battery
— it has gears and hands, but won't advance on its own. Someone must call `tick()`
to push the second hand forward.

**What it is NOT**: A timer. It has no thread, no sleep/wake mechanism, no concept
of wall-clock time. The `tick_interval_ms` parameter only converts delay_ms to
tick count; it doesn't drive anything.

**Why use it for games**: Game servers are already tick-driven. You don't need a
separate timer thread — just call `tick()` inside your game tick loop. O(1)
add/cancel, no syscall overhead, naturally aligned with the game clock.

### 3.2 steady_timer — The Battery

```
boost::asio::steady_timer (16ms interval)
  → callback: timing_wheel.tick()  // advance the clock
  → self-reschedule
io_context.run()  // unified event loop
```

`steady_timer` is Boost.Asio's wall-clock timer — the "battery" that drives the
TimingWheel. Every 16ms it fires, calls `timing_wheel.tick()`, and reschedules
itself. Since it lives on the same `io_context` as the TCP acceptor and sockets,
there's no need for manual `while+sleep` — `io_context.run()` runs the entire
game server as a single event loop.

### 3.3 bthread_timer — A True Timer

```
bthread_timer (brpc-style):
  kqueue() + EVFILT_USER
  dedicated timer thread
  lock-free task pool (128K slots)
  min-heap execution
```

Unlike the TimingWheel, `bthread_timer` IS a real timer. It has its own thread
that sleeps/wakes via kqueue, high-precision (us) timeout, and runs callbacks
on the timer thread.

**Why it's in the project**: Used for network connection timeout detection.
The timer callback checks if a connection has been idle too long and closes it.
This is posted to the timer thread, not the game tick — keeping network
housekeeping separate from game logic.

### 3.4 Why Three Timers?

| Timer | Type | Thread | Best For |
|-------|------|--------|----------|
| TimingWheel | Data structure | None (driven by steady_timer) | Game logic: skill CD, buff duration, NPC respawn — O(1) add/cancel |
| steady_timer | Asio timer | io_context thread | Drives the game tick at 60Hz, unified with network I/O |
| bthread_timer | True timer | Dedicated thread | Network timeouts, heartbeat — us precision, non-blocking |

This demonstrates the ability to **choose the right timer for each use case**
rather than blindly using one for everything.

### 3.5 Comparison: TimingWheel vs bthread_timer

```
TimingWheel (skynet-style):          bthread_timer (brpc-style):
┌────────────────────────┐           ┌────────────────────────┐
│ slots[0]: [●][ ][ ][●]│           │        min-heap        │
│ slots[1]: [ ][●][ ][ ] │           │         / \            │
│ slots[2]: [ ][ ][ ][ ] │           │       ●     ●          │
│ slots[3]: [●][ ][ ][ ] │           │      / \   / \         │
│                        │           │     ●   ● ●   ●        │
│ addTask → O(1) append  │           │ addTask → O(log n)     │
│ cancel  → O(1) mark    │           │ cancel  → O(1) CAS     │
│ tick()  → O(k) slot    │           │ run()   → kevent()     │
│           traversal     │           │           dedicated    │
│ No thread of its own   │           │           thread       │
└────────────────────────┘           └────────────────────────┘
```

Key distinction:
- TimingWheel: tick() is **called by you** (from steady_timer or main loop)
- bthread_timer: run() is **internal** (dedicated thread with kqueue sleep/wake)

## 4. Main Loop Architecture

### Event Loop

```
main thread: signal polling (sleep 200ms)
io_thread:   io_context.run()
  │
  ├── tcp::acceptor::async_accept → new connection → async_read loop
  ├── tcp::socket::async_read → append to RecvBuffer
  ├── tcp::socket::async_write → send responses
  └── steady_timer(N ms) per wheel → tick callbacks → process_group
```

Same pattern as ProtoRelay: async start + signal polling main loop.

### Project Layering

```
include/gs/           ← 框架层：Actor、ActorSystem、TitanServer、网络
examples/common/      ← 业务基础设施：Scene、SceneManager (AOI + 实体管理)
examples/tank_battle/ ← 示例项目：BattleScene、Tank、Bullet
```

Scene is NOT part of the core framework. It provides AOI + entity storage for
spatial simulations. Subclasses add game-specific logic (tank movement, combat).

### Game Tick Phases

```
steady_timer fires every 16ms:
  Phase 1 — collect input:
    conn_mgr.swap_all_buffers()  → raw byte streams keyed by player_id
    parse each buffer → push_now() to Scene Actor's cur_msgs

  Phase 2 — swap mailboxes:
    actor_system.swap_all()  → next_mailbox → cur_msgs

  Phase 3 — tick game systems:
    TickManager.tick() → move@30Hz, skill@60Hz, AOI@10Hz

  Phase 4 — execute:
    actor_system.process_all() → Scene.on_message() → AOI.move_player()

  Phase 5 — sync:
    Player.send_callback → encode → conn.send()
```

### Connection Management

```
TcpConnection {
    RecvBuffer _recv_buf;  // mutex + vector<uint8_t>
    // Network thread: _recv_buf.append()  ← async_read callback
    // Tick thread:     _recv_buf.swap_out() ← Phase 1
}

ConnectionManager {
    map<EntityId, Entry> _entries;  // all active connections
    swap_all_buffers() → batch swap all recv buffers
    send_to(player_id, data) → route response to specific connection
}
```

## 5. Cross-Scene Migration

### Double Registration

When an entity approaches a scene boundary:

```
Scene A                    boundary                    Scene B
  │                         │                          │
  │     ┌──────────┐        │                          │
  │     │ mirror   │········│·····> (entity enters     │
  │     │ zone     │        │       Scene B's AOI)     │
  │     │ (2 cells)│        │                          │
  │     └──────────┘        │                          │
  │                         │                          │
```

1. Entity within 2 cells of boundary -> registered as mirror in adjacent Scene
2. Entity moves past boundary -> fully migrated, mirror removed from old Scene
3. Hysteresis: entity must move 3 cells away from boundary before un-registering
   the mirror (prevents oscillation)

## 6. Network Protocol

Length-prefixed binary protocol:
```
[4-byte big-endian length][payload]
```

Payload starts with 1-byte message type:
- `0x01` Login
- `0x02` Move (x: float32, y: float32)
- `0x03` Chat
- `0x04` AOI Event (type: uint8, entity_id: uint64, x: float32, y: float32)

## 6. Debug & Deterministic Replay

The original stdin-based debug console has been replaced by a **programmatic
C++ API** with zero-overhead release builds (`#ifdef TITAN_DEBUG`). See
[`replay.md`](replay.md) for full documentation.

Key components:

- **SnapshotManager**: captures all Actor states asynchronously on the
  bthread_timer thread with `TASK_FLAG_DONT_COUNT_TIME` (virtual time
  freezes during capture). Safe to call from any tick callback.
- **Recorder**: records external inputs (TCP packets, peer messages) tagged
  with the current master tick for deterministic replay.
- **TitanServer::reload_state()**: disaster recovery — restores from a
  snapshot, replays recorded events tick-by-tick (`swap_all + process_group`),
  then advances `_master_tick` past the replayed range.

Tick control (pause/resume) is still available as a programmatic API. The
stdin console thread and `handle_command()` have been removed.

### Selective Wheel Control

Each wheel can be independently paused/resumed. This enables debugging scenarios like:
- Pause all wheels, resume only the move wheel → step through movement logic
- Pause the skill wheel while keeping AOI and net sync running
- Freeze the world, inspect state, single-step one tick group at a time

Implementation: each wheel's steady_timer callback checks a `_paused_wheels` set
before calling `wheel->tick()`.

### SIGINT Handling

SIGINT calls `server.stop()` directly (global server pointer). This stops io_context,
which unblocks `io_context.run()`. The debug console thread exits when stdin closes.

## 7. Logging (Planned)

Current: `std::cout` / `std::cerr` for everything. Debug output and normal logs are
interleaved, making it hard to follow either.

Plan: migrate to **spdlog** with dual sinks (same pattern as ProtoRelay):

```
spdlog  →  console_sink (debug level, colored)   → terminal 1: interactive debug
        →  file_sink (info level, rotating)       → tail -f titan.log
```

Usage from code:
```cpp
SPDLOG_DEBUG("AOI: player {} entered grid ({},{})", pid, gx, gy);
SPDLOG_INFO("tick {}: {} connections, {} timers", tick, conns, pending);
```

Workflow:
- **Terminal 1**: `./titan_server` — sees only debug output + interactive `list/pause/step` commands
- **Terminal 2**: `tail -f titan.log` — scrolls through normal execution logs, no debug noise

## 8. Distributed Actor Routing

### PeerManager — Gossip Discovery + Position Registry

Each node runs one PeerManager. Nodes discover each other via gossip:

```
PeerManager:
  _peers:   map<"ip:port", IPeer>     // active TCP connections
  _routes:  map<ActorId, "ip:port">   // which node owns which Actor
  _known_nodes: set<"ip:port">        // all discovered nodes

Protocol (length-prefixed, same pattern as game messages):
  0xE0 = REGISTER_ACTOR(aid)      // "Actor X is on my node"
  0xE1 = UNREGISTER_ACTOR(aid)    // "Actor X is gone"
  0xE2 = NEW_NODE("ip:port")      // "Check out this new node"
  0xE3 = ACTOR_MSG(aid, payload)  // forwarded inter-Actor message
```

### Connection Lifecycle

```
Node A starts:  listen on :9000
Node B starts:  listen on :9001, connect_to_peer("A", 9000)
  → B → A: TCP connect
  → A accepts, adds B to _peers, gossips B's address to all others
  → B's on_new_peer callback fires → syncs all local Actors to A
  → A's on_new_peer callback fires → syncs all local Actors to B
  → Both nodes now have each other's Actor lists
```

### ActorSystem::send() — Transparent Local/Remote

```cpp
void ActorSystem::send(ActorId target, Message msg) {
    if (本地有) → 直接投递 _next_mailbox
    else       → _peer_mgr->send_to(target, msg)  // TCP to remote node
}
```

### Thread Safety

- `_routes` updates are per-IP single writer (each peer's TCP callback runs on its own strand)
- `_routes` reads from `send_to()` are lock-free (stale reads are benign)
- `_peers` insert/delete under mutex

## 9. Multi-Threaded Actor Execution

When a thread pool is set via `ActorSystem::set_thread_pool()`, `process_group()` posts each Actor to the pool instead of running serially.

```
process_group(grp_move):
  sync.version++          // new batch
  sync.pending = N        // actors in group

  for each actor:
    post to thread_pool:
      actor.process_all()
      sync.pending--       // atomic
      if pending == 0:     // last actor done
        sync.version++     // advance batch
        cv.notify_all()

  cv.wait(version > my_batch)  // barrier — wait for all actors
```

Each group has its own `GroupSync` (version, pending count, mutex, cv). Different groups run independently — no cross-group blocking.

## 10. Hot Reload via Instance Draining

Titan supports zero-downtime rolling updates:

```
1. New instance starts on port 9001, joins cluster via PeerManager
2. Old instance receives "drain" command:
   → Actor::set_draining(true)      // stop accepting new work
   → TcpServer::drain("ip", 9001)   // send REDIRECT to all clients
   → Clients disconnect and reconnect to new instance
3. Old instance exits when all clients have migrated
```

Wire format for REDIRECT (0xF0): `[ip_len 1B][ip str][port 2B]`

## 11. Client-Side AOI

Game clients maintain their own local entity table, updated by server-pushed
AOI events. This is conceptually identical to a quantitative trading system
maintaining a local order book from incremental exchange feeds:

```
Server pushes:  ENTER 42 150 200   →  client._entities[42] = {150, 200}
                MOVE  42 152 200   →  client._entities[42].x = 152
                LEAVE 42           →  client._entities.erase(42)
```

The client does NOT need an Actor abstraction — the game engine (Unity, raylib)
already provides the framework for input/update/render loops. Actors are a
server-side concurrency pattern.

## 12. Protocol Pluggability

The parsing layer is already pluggable via `TcpPeer::set_recv_callback()`.
Current implementation: `main.cpp`'s `_handlers` registry dispatches by
message type. Swapping the protocol means replacing the callback — TcpPeer
doesn't change.

```
TcpPeer::_recv_cb(raw_bytes)
  → parse_input() → _handlers[type]() → push_now() to Actor
```

### TcpConnection vs IPeer

- `TcpConnection`: client-facing (one per player), owns RecvBuffer
- `IPeer` / `TcpPeer`: node-facing (one per server), gossip + Actor forwarding

They share the same length-prefix + async read pattern but serve different
roles. Future: could unify under a single `IConnection` interface.

### IServer Abstraction (Future)

TcpServer could implement `IServer` for pluggable transports (TCP, QUIC,
WebSocket). Currently TcpServer directly manages `weak_ptr<TcpConnection>`.
Not urgent — single TCP transport covers the demo's needs.

## 13. Gateway — Registration + Health Check

The Gateway is a lightweight, stateless front-end. Clients connect to it first
to discover which node owns which Actor. After redirection, communication is
P2P (client ↔ game node directly).

### Design

```
                    ┌─ P2P Actor messages ──────┐
                    │                            │
  Client ──→ Gateway (8888)                  Node A (9000)
              │  route_table                    │  PeerManager
              │  health_check                   │
              ├── Node A: alive, actors=[1,2]   │
              └── Node B: alive, actors=[3]     │
```

Gateway does NOT carry data-plane traffic:
- Clients ask "where is scene_1?" → Gateway answers "node A:9000"
- Clients connect directly to the node
- Actor messages flow P2P between nodes (no Gateway hop)

### Protocol

```
Client → Gateway:    "LOOKUP scene_1"  (length-prefixed)
Gateway → Client:    "OK node_A:9000"  or "NOT_FOUND"

Node → Gateway:      "PING actors=1,2,3"
Gateway → Node:      "PONG"
```

Gateway health-checks nodes. 3 missed PINGs → node marked suspect.
5 missed → node declared dead. Gateway rebuilds dead node's Actors on
a surviving node by sending `SPAWN actor_id` to it.

### Why Not Consensus (Raft/Paxos)?

Titan uses **partition suicide** instead of distributed consensus because:

1. **No shared state** — each Actor is a singleton. No data replication means
   no conflict on recovery. Consensus solves "which replica is the truth?" —
   Titan has no replicas.

2. **Recovery = fresh creation** — when node A is declared dead, Actor X is
   rebuilt from scratch on node B. There's no state to merge.

3. **Simplicity** — Raft adds leader election, log replication, and quorum
   overhead. Partition suicide is: "can't reach the cluster? I must be the
   problem. Shut down."

When consensus IS needed:
- Distributed database (TiKV, etcd) — multiple replicas, writes must agree
- Financial ledgers — transaction ordering across nodes
- Configuration management — shared config that all nodes must agree on

None of these apply to Titan's Actor routing. The Gateway is the single
source of truth for routing — it's a SPOF, but a restartable one (just
re-gossip on startup).

## 14. Production Engineering Gaps

Intentionally deferred. The project demonstrates architecture understanding,
not production readiness:

| Gap | Status | Plan |
|-----|--------|------|
| **spdlog logging** | std::cout/cerr | Dual-sink: console(debug) + file(info), ProtoRelay pattern |
| **Config from file** | ServerConfig hardcoded | JSON/YAML parsing (nlohmann/json) |
| **Metrics/health** | None | HTTP GET /health + /metrics, ProtoRelay already has this |
| **CI/CD** | None | GitHub Actions: cmake build + basic test |
| **Actor supervision** | None | Dead-actor detection, crash-only restart |
| **Performance profiling** | None | Benchmark tick latency, throughput under load |
| **IServer abstraction** | TcpServer concrete | IServer interface for QUIC/WS swap |
| **Unified IConnection** | TcpConnection + TcpPeer separate | Merge shared protocol logic |

## 15. Comparison with Production Systems

| Aspect | Titan Demo | Production (e.g. skynet) |
|--------|-----------|--------------------------|
| Actor scheduling | Single-thread serial (io_context) | Event-driven (epoll/kqueue per actor) |
| Game tick driver | steady_timer + TimingWheel | skynet.timeout + wheel |
| AOI | 9-grid, in-memory | 9-grid or cross-linked list |
| Networking | Raw TCP, length-prefixed | TCP + WebSocket, protobuf |
| Connection I/O | RecvBuffer swap (lock-free critical path) | Per-connection ring buffer |
| Persistence | None | Redis/MySQL for player state |
| Scene mgmt | Static partition | Dynamic split by load |
| Fault tolerance | None | Actor supervision + state recovery |

## Appendix: Document Index

Titan's documentation is split into focused files to keep `architecture.md`
from growing too large. Each file covers one subsystem in depth.

| Document | Covers |
|----------|--------|
| `architecture.md` | (this file) High-level architecture, Actor model, AOI, timers, networking, gateway, design trade-offs |
| `replay.md` | Deterministic record/replay, snapshot system, `reload_state` disaster recovery, limitations |
