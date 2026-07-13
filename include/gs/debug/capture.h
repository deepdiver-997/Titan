#pragma once

// ---- Debug API — zero overhead when TITAN_DEBUG is not defined ------------
//
// Usage in user code (place in main(), before server.run()):
//   #ifdef TITAN_DEBUG
//   gs::debug::SnapshotManager::instance().set_actor_system(&sys);
//   gs::debug::SnapshotManager::instance().set_timer(&server.tick_timer());
//   gs::debug::Recorder::instance().start();
//   #endif
//
//   server.schedule_tick(16, [&]() {
//       auto buffers = transport->swap_all_buffers();
//
//       RECORD_TCP_PACKET(pid, buf.data(), buf.size(), tick);
//
//       parse_input(buffers, sys, scene_mgr);
//       sys.swap_all();
//
//       SNAPSHOT("post_input");   // async capture on timer thread
//   });

#ifdef TITAN_DEBUG

#include "gs/debug/recorder.h"
#include "gs/debug/snapshot.h"

// ---- Snapshot -------------------------------------------------------------
// Schedule async capture of all Actor state on the bthread_timer thread
// with TASK_FLAG_DONT_COUNT_TIME (virtual time freezes during capture).
// Safe to call from any context — no deadlock with process_group().
//   SNAPSHOT("my_tag")  → records with __FILE__:__LINE__
#define SNAPSHOT(tag)                                                          \
    do {                                                                       \
        ::gs::debug::SnapshotManager::instance().capture(                      \
            (tag), __FILE__, __LINE__);                                        \
    } while (0)

// ---- Recording -------------------------------------------------------------
// Record a TCP packet or peer message for deterministic replay.
// These are designed to be called inside tick callbacks.
#define RECORD_TCP_PACKET(player_id, data_ptr, len, tick)                      \
    do {                                                                       \
        ::gs::debug::Recorder::instance().record_tcp_packet(                   \
            (player_id), (data_ptr), (len), (tick));                           \
    } while (0)

#define RECORD_PEER_MSG(node_id, data_ptr, len, tick)                          \
    do {                                                                       \
        ::gs::debug::Recorder::instance().record_peer_message(                 \
            (node_id), (data_ptr), (len), (tick));                             \
    } while (0)

#else
// ---- Release build: zero overhead -----------------------------------------
#define SNAPSHOT(tag)             ((void)0)
#define RECORD_TCP_PACKET(...)    ((void)0)
#define RECORD_PEER_MSG(...)      ((void)0)

#endif  // TITAN_DEBUG
