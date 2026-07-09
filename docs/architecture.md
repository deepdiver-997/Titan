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

### Nine-Grid Algorithm

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
io_context.run()  ← single thread, blocks here
  │
  ├── tcp::acceptor::async_accept → new connection → start async_read
  ├── tcp::socket::async_read → append to RecvBuffer
  ├── tcp::socket::async_write → send responses to clients
  └── steady_timer(16ms) → game tick callback
```

No separate I/O thread. No `while+sleep`. The entire server runs on a single
`io_context` event loop — network I/O and game ticks are interleaved naturally.

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
  │                          │                          │
  │     ┌──────────┐        │                          │
  │     │ mirror   │········│·····> (entity enters     │
  │     │ zone     │        │       Scene B's AOI)     │
  │     │ (2 cells)│        │                          │
  │     └──────────┘        │                          │
  │                          │                          │
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

## 7. Comparison with Production Systems

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
