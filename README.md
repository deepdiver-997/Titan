# Titan Game Server

A self-contained C++ game server framework demonstrating **seamless large-world
synchronization**. Built as a portfolio project for game server engineering
positions.

## Highlights

- **Actor model** — lightweight mailbox + worker-pool scheduling (no coroutines needed)
- **Nine-grid AOI** — enter/leave/update diff-based push, MC-style 16m cells
- **Layered tick system** — 60Hz base, move@30Hz, skill@60Hz, AOI@10Hz
- **Cross-scene migration** — double registration + hysteresis threshold
- **Dual timer architecture** — TimingWheel (game logic) + bthread_timer (network)
- **Boost.Asio TCP** — length-prefixed binary protocol, async I/O

## Architecture

```
Client (terminal simulator)
  │  TCP, length-prefixed binary protocol
  ▼
TcpServer (Boost.Asio)
  │
  ▼
ActorSystem (worker thread pool)
  │
  ├── Scene Actor (map shard 0)  ←→  Scene Actor (map shard 1)
  │     ├── AoiWorld (9-grid)         ├── AoiWorld (9-grid)
  │     ├── TickManager (layered)     ├── TickManager (layered)
  │     └── TimingWheel (timers)      └── TimingWheel (timers)
  │
  └── bthread_timer (network timeouts)
```

## Build & Run

### Prerequisites

- macOS (uses kqueue)
- C++17 compiler
- CMake 3.16+
- Boost (system component)
  ```sh
  brew install boost cmake
  ```

### Build

```sh
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(sysctl -n hw.ncpu)
```

### Run Demo

```sh
# Terminal 1: Start server
./build/src/gs/titan_server

# Terminal 2: Start 5 simulated players
./build/client/titan_client 127.0.0.1 8888 5
```

Or use the one-click script:
```sh
./scripts/run_demo.sh
```

## Key Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Actor model | mailbox + callback, no coroutines | Simpler, matches user's pthread styling |
| AOI algorithm | 9-grid with diff push | Industry standard, O(1) per-entity update |
| Game timer | Hierarchical timing wheel | O(1) add/cancel, tick-aligned, no extra thread |
| Network timer | kqueue min-heap timer | μs precision, suitable for connection timeouts |
| Cross-scene | Double registration + hysteresis | Prevents boundary oscillation |

## Project Structure

```
GameServer/
├── include/gs/        # Public headers
│   ├── actor/         # Actor system
│   ├── aoi/           # AOI (9-grid)
│   ├── common/        # Types, config
│   ├── entity/        # Player, NPC
│   ├── net/           # TCP server
│   ├── scene/         # Scene, scene manager
│   └── tick/          # Tick manager
├── src/gs/            # Implementation
├── client/            # Terminal client simulator
├── third_party/       # Bundled dependencies
│   ├── bthread_timer/ # brpc-style high-precision timer
│   └── timing_wheel/  # skynet-style hierarchical wheel
├── config/            # Runtime configuration
└── scripts/           # Demo scripts
```
