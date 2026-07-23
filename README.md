# Titan Game Server

A self-contained C++ game server framework. Built as a portfolio project for
game server engineering positions.

## Highlights

- **Actor model** — dual-buffered mailbox + exec() RPC, no coroutines
- **IAoi interface** — NineGridAoi (default), pluggable CrossLink/Tower algorithms
- **Per-frequency TimingWheels** — independent 16ms/33ms/100ms wheels, zero modulo math
- **Layered tick groups** — move@30Hz, skill@60Hz, bullet@125Hz, AOI@10Hz
- **Multi-threaded** — thread-pool + barrier synchronization per tick group
- **Session protocol** — 2-connection wire format (reliable + unreliable), SessionManager
- **NetSubsystem + DirtySink** — incremental flush, only dirty channels visited each tick
- **Channel write modes** — Append (RPC, never lose data) / Overwrite (state sync, latest wins)
- **Distributed routing** — PeerManager gossip + position registry, transparent Actor::send()
- **Hot reload** — TcpServer::drain() + REDIRECT, zero-downtime rolling update
- **Raylib tank battle** — playable demo with WASD movement, spacebar fire

## Architecture

```
┌─ main.cpp (assembly) ──────────────────────────────┐
│  TitanServer                                        │
│    ├── ActorSystem + tick groups                    │
│    ├── Multi-wheel timers (16/33/8/100ms)           │
│    ├── NetSubsystem (flush groups)                  │
│    └── Thread pool (4 workers)                      │
│                                                     │
│  SessionManager (session lifecycle)                 │
│  Channel (per-entity output buffer, owns frequency) │
│  PeerManager (gossip + routes)   ←→  remote nodes  │
│  TcpServer (client connections)  ←→  clients       │
│  Scene (AOI + entities)                             │
│  BattleScene (tank + bullet logic)                  │
└─────────────────────────────────────────────────────┘
```

### Data flow

```
INPUT:
  TCP → RecvBuffer swap → bind_pending → Session.drain_framed()
    → parse_input → sys.send() → Actor mailbox

ACTOR TICK:
  process_group → exec() → AOI diff → ch->write(data)

OUTPUT (incremental):
  ch->write(data) → CAS dirty flag, enqueue weak_ptr into DirtySink
    → FlushGroup timer tick → swap dirty list → flush_and_reset() on alive channels
    → Session.send(conn_slot, data) → TcpConnection → client
```

### Output path details

`Channel` is constructed with a `DirtySink*` (its flush frequency group) and a
`Session` connection slot (0=reliable, 1=unreliable). On first `write()` after
a flush, the channel CAS-sets its dirty flag and pushes a `weak_ptr` into the
sink. `NetSubsystem`'s timer task swaps the sink's dirty list each tick and
calls `flush_and_reset()` only on channels that were written to.

Channel lifecycle is managed by the owning entity via `shared_ptr`. When the
entity destroys its Channel, `weak_ptr`s in the dirty list expire silently —
no unregistration needed.

## Build & Run

### Prerequisites

```sh
brew install boost cmake raylib
```

### Build

```sh
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(sysctl -n hw.ncpu)
```

### Run Tank Battle Demo

```sh
# Terminal 1: server
./examples/tank_battle/titan_tank_server

# Terminal 2: raylib client
./titan_tank_client 127.0.0.1 8888
# WASD move, SPACE fire, ESC quit

# Or terminal debug client
./terminal_tank 127.0.0.1 8888
# w/a/s/d move, f fire, q quit
```


## License

MIT License. See [LICENSE](LICENSE).

## References

The timer architecture design references:
- [brpc](https://github.com/apache/brpc) — TimerThread (multi-bucket, task pool, min-heap)
- [skynet](https://github.com/cloudwu/skynet) — hierarchical TimingWheel

External dependencies (Boost.Asio, spdlog, fmt, Catch2, raylib) are used via
package managers and are not included in this repository.

## Key Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Actor model | Dual-buffer mailbox + exec() RPC | World-frozen semantics, type-safe entity dispatch |
| AOI | IAoi interface, NineGridAoi default | Pluggable algorithm, cross-link list for large worlds |
| Tick dispatch | Per-frequency independent wheels | No "must be multiple of base", zero CPU waste |
| Timers | TimingWheel (passive) + bthread_timer (driver) | Deterministic tick, dedicated high-precision timer thread |
| Network output | Channel + NetSubsystem + DirtySink | App writes directly, incremental flush (only dirty channels) |
| Channel modes | Append (RPC) / Overwrite (state sync) | RPC data must not be lost; state sync only latest matters |
| Session safety | shared_ptr<Session> + weak_ptr | No use-after-free on concurrent remove(), no ABA |
| Distributed | PeerManager gossip + position registry | No central coordinator, lock-free route reads |
| Hot reload | Instance draining + REDIRECT | Zero-downtime, no code reload complexity |

## Project Structure

```
GameServer/
├── include/gs/              # Framework headers
│   ├── actor/               # Actor, Mailbox, ActorSystem, PeerManager
│   ├── aoi/                 # IAoi, NineGridAoi, AoiGrid
│   ├── common/              # Vec2, EntityId, Config
│   ├── entity/              # Entity base + Player
│   ├── net/                 # TcpServer, TcpPeer, Session, Channel, NetSubsystem
│   │   └── protocol/        # Session, SessionManager (2-connection wire protocol)
│   ├── scene/               # Scene, SceneManager (optional framework module)
│   └── server/              # TitanServer (top-level framework)
├── src/gs/                  # Framework implementation
├── examples/
│   └── tank_battle/         # Tank battle demo (server + raylib client)
├── client/                  # Terminal + raylib clients
├── third_party/
│   ├── bthread_timer/       # brpc-style kqueue timer
│   └── timing_wheel/        # skynet-style hierarchical wheel
├── docs/architecture.md     # Detailed design document
└── config/server.json       # Runtime configuration
```

## Production Gaps (Noted, Not Blocking)

The following are intentionally deferred — the project demonstrates **architecture
understanding**, not production readiness:

| Gap | Plan |
|-----|------|
| **spdlog** | Replace std::cout with dual-sink (console debug + file info) |
| **Config injection** | ServerConfig is hardcoded; add JSON/YAML file parsing |
| **Metrics/health HTTP** | ProtoRelay already has this; same pattern for Titan |
| **CI/CD** | GitHub Actions: cmake build + test |
| **IServer abstraction** | TcpServer could implement an IServer interface for QUIC/WebSocket |
| **Unified IConnection** | TcpConnection (client) and TcpPeer (node) share protocol logic, could merge |
| **Actor supervision** | Dead-actor detection + restart, crash-only philosophy |
| **Performance profiling** | Benchmark tick latency, message throughput under load |
