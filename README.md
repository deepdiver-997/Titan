# Titan Game Server

A self-contained C++ game server framework. Built as a portfolio project for
game server engineering positions.

## Highlights

- **Actor model** — dual-buffered mailbox + exec() RPC, no coroutines
- **IAoi interface** — NineGridAoi (default), pluggable CrossLink/Tower algorithms
- **Per-frequency TimingWheels** — independent 16ms/33ms/100ms wheels, zero modulo math
- **Layered tick groups** — move@30Hz, skill@60Hz, bullet@125Hz, AOI@10Hz
- **Multi-threaded** — thread-pool + barrier synchronization per tick group
- **Distributed routing** — PeerManager gossip + position registry, transparent Actor::send()
- **Hot reload** — TcpServer::drain() + REDIRECT, zero-downtime rolling update
- **Raylib tank battle** — playable demo with WASD movement, spacebar fire

## Architecture

```
┌─ main.cpp (assembly) ──────────────────────────────┐
│  TitanServer                                        │
│    ├── ActorSystem + tick groups                    │
│    ├── Multi-wheel timers (16/33/8/100ms)           │
│    ├── Debug console (list/pause/step/drain)        │
│    └── Thread pool (4 workers)                      │
│                                                     │
│  PeerManager (gossip + routes)   ←→  remote nodes  │
│  TcpServer (client connections)  ←→  clients       │
│  Scene (AOI + entities)                             │
│  NetSyncActor (output isolation)                    │
│  BattleScene (tank + bullet logic)                  │
└─────────────────────────────────────────────────────┘
```

### Data flow

```
TCP → RecvBuffer swap → parse_input → push_now → Actor.cur_msgs
  → process_group → swap_all → process_all → exec() → AOI diff
  → push_outgoing → NetSyncActor → TcpServer.send_to → client
```

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
| Timers | TimingWheel (passive) + steady_timer (battery) | Game is tick-driven, no extra timer thread needed |
| Network output | NetSyncActor (isolated) | Actors don't know about TCP sockets |
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
│   ├── net/                 # TcpServer, TcpPeer, IPeer, NetSyncActor, Message
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
